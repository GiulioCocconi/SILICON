/*
  Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "yosys.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <boost/graph/adjacency_list.hpp>

#include <core/circuit.hpp>
#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/subcircuit.hpp>
#include <core/subcircuitDefinition.hpp>
#include <core/projectDocument.hpp>

namespace silicon::yosys {
namespace {

  struct ModuleInterface {
    std::string              moduleName;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
  };

  struct ForcedPort {
    std::string name;
    std::string direction;
    Bus         bus;
  };

  class DesignBuilder;

  [[nodiscard]] std::vector<std::optional<std::uint64_t>> busIds(const Bus& bus)
  {
    std::vector<std::optional<std::uint64_t>> ids;
    ids.reserve(bus.size());
    for (const auto& wire : bus)
      ids.push_back(wire ? std::optional(wire->getId()) : std::nullopt);
    return ids;
  }

  [[nodiscard]] std::string fallbackPortName(const bool input, const std::size_t index)
  {
    return std::format("{}_{}", input ? "input" : "output", index);
  }

  [[nodiscard]] std::vector<std::string>
  boundaryNamesFromDocument(const project::Document& document,
                            const std::vector<Bus>& interfaceBuses, const bool input)
  {
    struct Candidate {
      std::string                               name;
      std::vector<std::optional<std::uint64_t>> ids;
    };

    std::vector<Candidate> candidates;
    try {
      Json scene = Json::parse(document.sceneJson());
      if (scene.contains("circuit"))
        scene = scene["circuit"];

      if (const auto it = scene.find("components"); it != scene.end() && it->is_array()) {
        for (const auto& component : *it) {
          const std::string type = component.value("type", std::string());
          const bool        matches =
              input ? type == "DummyInputComponent" || type == "DummyBusInputComponent"
                           : type == "DummyOutputComponent" || type == "DummyBusOutputComponent";
          if (!matches)
            continue;

          const auto busesIt = component.find(input ? "outputs" : "inputs");
          if (busesIt == component.end() || !busesIt->is_array() || busesIt->empty()
              || !(*busesIt)[0].is_array()) {
            continue;
          }

          Candidate candidate;
          candidate.name = input ? "input" : "output";
          if (const auto properties = component.find("properties");
              properties != component.end() && properties->is_object()) {
            candidate.name = properties->value("name", candidate.name);
          }
          for (const auto& id : (*busesIt)[0]) {
            candidate.ids.push_back(
                id.is_null() ? std::nullopt : std::optional(id.get<std::uint64_t>()));
          }
          candidates.push_back(std::move(candidate));
        }
      }
    } catch (const nlohmann::json::exception&) {
      // The core subcircuit loader produces the authoritative diagnostics. A document
      // without usable graphical metadata simply receives deterministic fallback names.
    }

    std::vector<std::string> names;
    names.reserve(interfaceBuses.size());
    std::vector<bool> used(candidates.size(), false);
    for (std::size_t index = 0; index < interfaceBuses.size(); ++index) {
      const auto ids   = busIds(interfaceBuses[index]);
      auto       match = candidates.size();
      for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate) {
        if (!used[candidate] && candidates[candidate].ids == ids) {
          match = candidate;
          break;
        }
      }
      if (match != candidates.size()) {
        used[match] = true;
        names.push_back(candidates[match].name);
      } else {
        names.push_back(fallbackPortName(input, index));
      }
    }
    return names;
  }

}  // namespace

struct SerializationContext::Impl {
  DesignBuilder*               design = nullptr;
  Json*                        module = nullptr;
  std::map<std::uint64_t, int> wireSignals;
  std::set<std::string>        portNames;
  std::set<std::string>        cellNames;
  int                          nextSignal = 2;
  VertexDescriptor             vertex     = 0;
  std::string                  componentType;

  [[nodiscard]] int signalFor(const Wire_ptr& wire)
  {
    if (!wire)
      throw std::logic_error("Cannot allocate a numeric Yosys signal for a null wire");
    const auto id = wire->getId();
    if (const auto it = wireSignals.find(id); it != wireSignals.end())
      return it->second;
    const int signal = nextSignal++;
    wireSignals.emplace(id, signal);
    return signal;
  }

  [[nodiscard]] std::string uniquePortName(std::string            requested,
                                           const std::string_view direction)
  {
    if (requested.empty())
      requested = direction == "input" ? "input" : "output";
    if (portNames.insert(requested).second)
      return requested;

    for (std::size_t suffix = 2;; ++suffix) {
      auto candidate = std::format("{}_{}", requested, suffix);
      if (portNames.insert(candidate).second)
        return candidate;
    }
  }
};

namespace {

  class DesignBuilder {
  public:
    Json                                   modules = Json::object();
    std::map<std::string, ModuleInterface> subcircuitInterfaces;
    std::set<std::string>                  moduleNames;

    void serializeModule(const Circuit& circuit, const std::string& moduleName,
                         const std::vector<ForcedPort>& forcedPorts = {})
    {
      // Reserve names before recursion. Otherwise a child whose slug equals an active
      // parent could be emitted and silently overwritten when the parent finishes.
      if (!moduleNames.insert(moduleName).second)
        throw std::runtime_error(
            std::format("Duplicate Yosys module name '{}'", moduleName));

      Json                       module = Json{{"attributes", Json::object()},
                                               {"ports", Json::object()},
                                               {"cells", Json::object()},
                                               {"netnames", Json::object()}};
      SerializationContext::Impl impl;
      impl.design = this;
      impl.module = &module;
      SerializationContext context(impl);

      for (const auto& port : forcedPorts)
        context.addPort(port.name, port.direction, port.bus);

      const auto& graph = circuit.getGraph();
      for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
        const auto& component = graph[vertex].component;
        if (!component)
          continue;
        impl.vertex        = vertex;
        impl.componentType = component->typeName();
        component->serializeYosys(context);
      }

      for (const auto& [wireId, signal] : impl.wireSignals) {
        const auto name          = std::format("$wire${}", wireId);
        module["netnames"][name] = Json{{"hide_name", 1},
                                        {"bits", Json::array({signal})},
                                        {"attributes", Json::object()}};
      }

      modules[moduleName] = std::move(module);
    }

    [[nodiscard]] ModuleInterface ensureSubcircuit(const std::string_view slug)
    {
      const std::string slugString(slug);
      if (slugString.empty())
        throw std::runtime_error("Cannot export a subcircuit with an empty slug");
      if (const auto it = subcircuitInterfaces.find(slugString);
          it != subcircuitInterfaces.end()) {
        return it->second;
      }

      const auto* document = project::DocumentStore::active().find(
          project::subcircuitPathForSlug(slugString));
      if (!document)
        throw std::runtime_error(std::format("Unknown subcircuit slug '{}'", slugString));

      auto definition =
          subcircuits::loadDefinition(slugString, ComponentRegistry::instance());

      ModuleInterface interface;
      interface.moduleName = slugString;
      interface.inputNames =
          boundaryNamesFromDocument(*document, definition.inputs, true);
      interface.outputNames =
          boundaryNamesFromDocument(*document, definition.outputs, false);

      std::set<std::string> names;
      auto makeUnique = [&names](std::string name, const std::string_view fallback) {
        if (name.empty())
          name = fallback;
        if (names.insert(name).second)
          return name;
        for (std::size_t index = 2;; ++index) {
          auto candidate = std::format("{}_{}", name, index);
          if (names.insert(candidate).second)
            return candidate;
        }
      };
      for (std::size_t index = 0; index < interface.inputNames.size(); ++index)
        interface.inputNames[index] =
            makeUnique(interface.inputNames[index], fallbackPortName(true, index));
      for (std::size_t index = 0; index < interface.outputNames.size(); ++index)
        interface.outputNames[index] =
            makeUnique(interface.outputNames[index], fallbackPortName(false, index));

      // Publish the interface before walking the definition so repeated, non-recursive
      // instances reuse exactly the same module and connection names.
      subcircuitInterfaces.emplace(slugString, interface);

      std::vector<ForcedPort> ports;
      for (std::size_t index = 0; index < definition.inputs.size(); ++index)
        ports.push_back({interface.inputNames[index], "input", definition.inputs[index]});
      for (std::size_t index = 0; index < definition.outputs.size(); ++index)
        ports.push_back(
            {interface.outputNames[index], "output", definition.outputs[index]});

      serializeModule(definition.circuit, interface.moduleName, ports);
      return interface;
    }
  };

}  // namespace

Json SerializationContext::bits(const Bus& bus, const std::string_view nullValue) const
{
  // Both Silicon and Yosys enumerate vector connections least-significant bit first,
  // so no reversal is needed here. Yosys represents constants as JSON strings.
  Json result = Json::array();
  for (const auto& wire : bus) {
    if (wire)
      result.push_back(impl.signalFor(wire));
    else
      result.push_back(std::string(nullValue));
  }
  return result;
}

Json SerializationContext::allocateBits(const std::size_t width)
{
  Json result = Json::array();
  for (std::size_t bit = 0; bit < width; ++bit)
    result.push_back(impl.nextSignal++);
  return result;
}

Json SerializationContext::concatenate(const std::vector<Json>& vectors)
{
  Json result = Json::array();
  for (const auto& vector : vectors)
    for (const auto& bit : vector)
      result.push_back(bit);
  return result;
}

std::string SerializationContext::parameter(const std::uint64_t value,
                                            const std::size_t   width)
{
  if (width == 0)
    throw std::invalid_argument("Yosys parameter width must be positive");
  std::string result(width, '0');
  // write_json serializes RTLIL constants MSB-first even though connection arrays are
  // LSB-first. Keeping this distinction here avoids duplicating bit-order logic.
  for (std::size_t bit = 0; bit < std::min(width, std::size_t{64}); ++bit) {
    if ((value >> bit) & 1U)
      result[width - bit - 1] = '1';
  }
  return result;
}

void SerializationContext::addCell(const std::string_view suffix,
                                   const std::string_view type, Json parameters,
                                   Json portDirections, Json connections)
{
  std::string name = std::format("$silicon${}${}${}", impl.vertex, impl.componentType,
                                 suffix.empty() ? "" : "$" + std::string(suffix));
  if (!impl.cellNames.insert(name).second) {
    for (std::size_t index = 2;; ++index) {
      auto candidate = std::format("{}${}", name, index);
      if (impl.cellNames.insert(candidate).second) {
        name = std::move(candidate);
        break;
      }
    }
  }
  (*impl.module)["cells"][name] = Json{{"hide_name", 1},
                                       {"type", type},
                                       {"parameters", std::move(parameters)},
                                       {"attributes", Json::object()},
                                       {"port_directions", std::move(portDirections)},
                                       {"connections", std::move(connections)}};
}

void SerializationContext::addPort(std::string name, const std::string_view direction,
                                   const Bus& bus)
{
  if (direction != "input" && direction != "output" && direction != "inout")
    throw std::invalid_argument("Invalid Yosys port direction");
  name                          = impl.uniquePortName(std::move(name), direction);
  const Json portBits           = bits(bus);
  (*impl.module)["ports"][name] = Json{{"direction", direction}, {"bits", portBits}};
  (*impl.module)["netnames"][name] =
      Json{{"hide_name", 0}, {"bits", portBits}, {"attributes", Json::object()}};
}

void SerializationContext::addSubcircuitInstance(const std::string_view  slug,
                                                 const std::vector<Bus>& inputs,
                                                 const std::vector<Bus>& outputs)
{
  const auto interface = impl.design->ensureSubcircuit(slug);
  if (inputs.size() != interface.inputNames.size()
      || outputs.size() != interface.outputNames.size()) {
    throw std::runtime_error(std::format("Subcircuit '{}' interface mismatch: expected "
                                         "{} inputs and {} outputs, got {} and {}",
                                         slug, interface.inputNames.size(),
                                         interface.outputNames.size(), inputs.size(),
                                         outputs.size()));
  }

  Json portDirections = Json::object();
  Json connections    = Json::object();
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    portDirections[interface.inputNames[index]] = "input";
    connections[interface.inputNames[index]]    = bits(inputs[index]);
  }
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    portDirections[interface.outputNames[index]] = "output";
    connections[interface.outputNames[index]]    = bits(outputs[index]);
  }
  addCell("instance", interface.moduleName, Json::object(), std::move(portDirections),
          std::move(connections));
}

std::string serialize(const Circuit& circuit)
{
  DesignBuilder     builder;
  const std::string moduleName = circuit.getName().empty() ? "main" : circuit.getName();
  builder.serializeModule(circuit, moduleName);
  Json design = Json{{"creator", std::format("Silicon {}", SILICON_VERSION)},
                     {"modules", std::move(builder.modules)}};
  return design.dump(2);
}

}  // namespace silicon::yosys
