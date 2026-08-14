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
#include <cctype>
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

namespace SILICON::yosys {

using namespace SILICON::core;

namespace {

  struct ModuleInterface {
    std::string              moduleName;
    std::vector<CircuitPort> inputs;
    std::vector<CircuitPort> outputs;
  };

  struct ForcedPort {
    std::string name;
    std::string direction;
    Bus         bus;
  };

  class DesignBuilder;

  [[nodiscard]] std::string fallbackPortName(const bool input, const std::size_t index)
  {
    return std::format("{}_{}", input ? "input" : "output", index);
  }

  [[nodiscard]] std::string identifierPart(const std::string_view value)
  {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
      const bool valid = (character >= 'a' && character <= 'z')
                         || (character >= 'A' && character <= 'Z')
                         || (character >= '0' && character <= '9') || character == '_';
      if (valid)
        result += character;
      else if (result.empty() || result.back() != '_')
        result += '_';
    }
    if (result.empty())
      return "component";
    if (result.front() >= '0' && result.front() <= '9')
      result.insert(0, "component_");
    return result;
  }

  [[nodiscard]] std::string readableCellType(std::string type)
  {
    if (type.starts_with("SILICON_"))
      type.erase(0, std::string_view("SILICON_").size());
    while (!type.empty() && type.front() == '$')
      type.erase(type.begin());
    std::ranges::transform(type, type.begin(), [](const unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    return identifierPart(type);
  }

}  // namespace

struct SerializationContext::Impl {
  DesignBuilder*               design = nullptr;
  Json*                        module = nullptr;
  std::map<std::uint64_t, int> wireSignals;
  std::set<std::string>        portNames;
  std::set<std::string>        cellNames;
  std::set<std::string>        netNames;
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

      // When a cell directly drives a module output, name the instance after that
      // public signal. This is more useful in generated HDL than a graph vertex.
      Json                  renamedCells = Json::object();
      std::set<std::string> renamedCellNames;
      for (const auto& [cellName, cell] : module["cells"].items()) {
        std::string readableName = cellName;
        for (const auto& [portName, port] : module["ports"].items()) {
          for (const auto& [cellPort, direction] : cell.at("port_directions").items()) {
            if (direction == "output"
                && cell.at("connections").at(cellPort) == port.at("bits")) {
              readableName =
                  std::format("{}_{}", identifierPart(portName),
                              readableCellType(cell.at("type").get<std::string>()));
              break;
            }
          }
          if (readableName != cellName)
            break;
        }
        const auto baseName = readableName;
        for (std::size_t index = 2; !renamedCellNames.insert(readableName).second;
             ++index)
          readableName = std::format("{}_{}", baseName, index);
        renamedCells[readableName] = cell;
      }
      module["cells"] = std::move(renamedCells);

      // A component output that is also a module port should use the shorter port
      // name. Keeping both public aliases makes write_verilog emit a redundant wire
      // and assignment.
      for (const auto& [portName, port] : module["ports"].items()) {
        for (auto net = module["netnames"].begin(); net != module["netnames"].end();) {
          if (net.key() != portName && net.value().at("bits") == port.at("bits"))
            net = module["netnames"].erase(net);
          else
            ++net;
        }
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

      auto definition =
          loadSubcircuitDefinition(slugString, ComponentRegistry::instance());

      ModuleInterface interface;
      interface.moduleName = slugString;
      interface.inputs     = definition.inputs;
      interface.outputs    = definition.outputs;

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
      for (std::size_t index = 0; index < interface.inputs.size(); ++index)
        interface.inputs[index].name =
            makeUnique(interface.inputs[index].name, fallbackPortName(true, index));
      for (std::size_t index = 0; index < interface.outputs.size(); ++index)
        interface.outputs[index].name =
            makeUnique(interface.outputs[index].name, fallbackPortName(false, index));

      // Publish the interface before walking the definition so repeated, non-recursive
      // instances reuse exactly the same module and connection names.
      subcircuitInterfaces.emplace(slugString, interface);

      std::vector<ForcedPort> ports;
      for (const auto& port : interface.inputs)
        ports.push_back({port.name, "input", port.bus});
      for (const auto& port : interface.outputs)
        ports.push_back({port.name, "output", port.bus});

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

Json SerializationContext::inputBits(const Component& component, const std::size_t index,
                                     const std::size_t expectedWidth) const
{
  const auto& buses = component.inputBuses();
  if (index >= buses.size() || buses[index].size() != expectedWidth) {
    throw std::runtime_error(std::format(
        "Cannot export '{}': input bus {} must be a {}-bit bus", component.typeName(),
        index, expectedWidth));
  }

  const auto defaultState = component.unconnectedInputDefault(index);
  if (!defaultState && std::ranges::contains(buses[index], nullptr)) {
    throw std::runtime_error(std::format(
        "Cannot export '{}': input bus {} must be connected", component.typeName(),
        index));
  }

  const std::string_view defaultBit =
      !defaultState                 ? "x"
      : *defaultState == State::LOW ? "0"
      : *defaultState == State::HIGH ? "1"
                                     : "x";
  return bits(buses[index], defaultBit);
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
  const bool isConstant =
      type == "$pos" && connections.contains("A")
      && std::ranges::all_of(connections.at("A"),
                             [](const Json& bit) { return bit.is_string(); });
  const auto baseName =
      std::format("{}_{}", identifierPart(impl.componentType), impl.vertex);
  std::string name =
      suffix.empty() ? baseName : std::format("{}_{}", baseName, identifierPart(suffix));
  if (isConstant)
    name = "$" + name;
  if (!impl.cellNames.insert(name).second) {
    for (std::size_t index = 2;; ++index) {
      auto candidate = std::format("{}_{}", name, index);
      if (impl.cellNames.insert(candidate).second) {
        name = std::move(candidate);
        break;
      }
    }
  }

  // Yosys input connections may contain constants such as "x", but output
  // connections must be signals. Silicon represents an unused pin with a null wire,
  // which bits() encodes as "x" without knowing the port direction. Give every such
  // output bit an anonymous module-local signal so disconnected outputs remain valid
  // without becoming visible circuit ports.
  for (const auto& [port, direction] : portDirections.items()) {
    if (direction != "output" || !connections.contains(port))
      continue;
    for (auto& bit : connections.at(port)) {
      if (bit.is_string())
        bit = impl.nextSignal++;
    }
  }

  for (const auto& [port, direction] : portDirections.items()) {
    if (isConstant || direction != "output" || !connections.contains(port))
      continue;
    std::string netName = std::format("{}_{}", baseName, identifierPart(port));
    for (std::size_t index = 2; !impl.netNames.insert(netName).second; ++index)
      netName = std::format("{}_{}_{}", baseName, identifierPart(port), index);
    (*impl.module)["netnames"][netName] = Json{
        {"hide_name", 0}, {"bits", connections.at(port)}, {"attributes", Json::object()}};
  }

  (*impl.module)["cells"][name] = Json{{"hide_name", isConstant ? 1 : 0},
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
  name = impl.uniquePortName(std::move(name), direction);
  impl.netNames.insert(name);
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
  if (inputs.size() != interface.inputs.size()
      || outputs.size() != interface.outputs.size()) {
    throw std::runtime_error(std::format("Subcircuit '{}' interface mismatch: expected "
                                         "{} inputs and {} outputs, got {} and {}",
                                         slug, interface.inputs.size(),
                                         interface.outputs.size(), inputs.size(),
                                         outputs.size()));
  }

  Json portDirections = Json::object();
  Json connections    = Json::object();
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    portDirections[interface.inputs[index].name] = "input";
    connections[interface.inputs[index].name]    = bits(inputs[index]);
  }
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    portDirections[interface.outputs[index].name] = "output";
    connections[interface.outputs[index].name]    = bits(outputs[index]);
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

}  // namespace SILICON::yosys
