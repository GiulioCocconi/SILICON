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

#include "subcircuitDefinition.hpp"

#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/activeKeyGuard.hpp>
#include <core/projectDocument.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::core {

namespace {

  [[nodiscard]] std::unordered_map<std::uint64_t, Wire_ptr>
  wireMapForCircuit(const Circuit& circuit)
  {
    std::unordered_map<std::uint64_t, Wire_ptr> wiresById;
    auto collectBus = [&wiresById](const Bus& bus) {
      for (const auto& wire : bus) {
        if (wire)
          wiresById.emplace(wire->getId(), wire);
      }
    };

    for (const auto& [component, _vertex] : circuit.getComponentToVertex()) {
      if (!component)
        continue;
      for (const auto& bus : component->inputBuses())
        collectBus(bus);
      for (const auto& bus : component->outputBuses())
        collectBus(bus);
    }
    return wiresById;
  }

  [[nodiscard]] std::vector<CircuitPort>
  resolvePorts(const Circuit& target, const std::vector<CircuitPort>& ports)
  {
    auto                     wiresById = wireMapForCircuit(target);
    std::vector<CircuitPort> resolved;
    resolved.reserve(ports.size());

    for (const auto& port : ports) {
      std::vector<Wire_ptr> wires;
      wires.reserve(port.bus.size());
      for (const auto& wire : port.bus) {
        if (!wire) {
          wires.push_back(nullptr);
          continue;
        }

        const auto id = wire->getId();
        if (const auto it = wiresById.find(id); it != wiresById.end()) {
          wires.push_back(it->second);
        } else {
          auto interfaceWire = std::make_shared<Wire>(id, State::UNKNOWN);
          wiresById.emplace(id, interfaceWire);
          wires.push_back(std::move(interfaceWire));
        }
      }
      resolved.push_back({.name = port.name, .bus = Bus(std::move(wires))});
    }
    return resolved;
  }

}  // namespace

std::string extractCoreCircuitJson(std::string_view sceneJson)
{
  auto json = nlohmann::json::parse(sceneJson);
  if (json.contains("circuit"))
    json = json["circuit"];
  if (!json.is_object())
    throw std::runtime_error("Subcircuit document must contain a circuit object");
  return json.dump();
}

SubcircuitDefinition loadSubcircuitDefinition(const std::string_view   slug,
                                              const ComponentRegistry& registry)
{
  static thread_local std::vector<std::string> activeResolutionSlugs;
  ActiveKeyGuard activeSlug(activeResolutionSlugs, std::string(slug),
                            "Recursive subcircuit dependency detected: ");

  const auto  path     = SILICON::project::subcircuitPathForSlug(slug);
  const auto* document = SILICON::project::DocumentStore::active().find(path);
  if (!document)
    throw std::runtime_error(std::format("Unknown subcircuit slug '{}'", slug));

  const auto coreJson =
      document->coreCircuitJson().value_or(extractCoreCircuitJson(document->sceneJson()));
  auto       circuit = Circuit::deserialize(coreJson, registry);
  const auto portCircuit =
      Circuit::deserialize(extractCoreCircuitJson(document->sceneJson()), registry);
  auto inputs  = resolvePorts(circuit, portCircuit.getInputPorts());
  auto outputs = resolvePorts(circuit, portCircuit.getOutputPorts());

  return {.circuit = std::move(circuit),
          .inputs  = std::move(inputs),
          .outputs = std::move(outputs)};
}

}  // namespace SILICON::core
