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

#include "elaboration.hpp"

#include <format>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <boost/graph/graph_traits.hpp>

#include <core/serialization/component_registry.hpp>
#include <core/activeKeyGuard.hpp>
#include <core/subcircuit.hpp>
#include <core/subcircuitDefinition.hpp>

namespace {

using WirePtrMap = std::unordered_map<const Wire*, Wire_ptr>;

struct ElaborationContext {
  const ComponentRegistry& registry;
  Component_set            components;
};

[[nodiscard]] Wire_ptr mappedWire(const Wire_ptr& sourceWire, WirePtrMap& wireMap)
{
  if (!sourceWire)
    return nullptr;

  if (const auto it = wireMap.find(sourceWire.get()); it != wireMap.end())
    return it->second;

  auto mapped = std::make_shared<Wire>(sourceWire->getCurrentState());
  wireMap.emplace(sourceWire.get(), mapped);
  return mapped;
}

[[nodiscard]] Bus remapBus(const Bus& bus, WirePtrMap& wireMap)
{
  std::vector<Wire_ptr> wires;
  wires.reserve(bus.size());

  for (const auto& wire : bus)
    wires.push_back(mappedWire(wire, wireMap));

  return Bus(std::move(wires));
}

[[nodiscard]] std::vector<Bus> remapBuses(const std::vector<Bus>& buses,
                                          WirePtrMap&             wireMap)
{
  std::vector<Bus> remapped;
  remapped.reserve(buses.size());

  for (const auto& bus : buses)
    remapped.push_back(remapBus(bus, wireMap));

  return remapped;
}

void mapInterfaceWires(const std::vector<CircuitPort>& internalPorts,
                       const std::vector<Bus>& externalBuses, WirePtrMap& wireMap)
{
  for (std::size_t busIndex = 0; busIndex < internalPorts.size(); ++busIndex) {
    const auto& internalBus = internalPorts[busIndex].bus;
    for (std::size_t bit = 0; bit < internalBus.size(); ++bit) {
      const auto index = static_cast<unsigned short>(bit);
      if (internalBus[index] && externalBuses[busIndex][index])
        wireMap[internalBus[index].get()] = externalBuses[busIndex][index];
    }
  }
}

void validateInterface(const std::string_view moduleKey, const std::string_view direction,
                       const std::vector<CircuitPort>& definitionPorts,
                       const std::vector<Bus>& instanceBuses)
{
  if (definitionPorts.size() != instanceBuses.size()) {
    throw std::runtime_error(std::format(
        "Module '{}' {} bus count mismatch: definition has {}, instance has {}",
        moduleKey, direction, definitionPorts.size(), instanceBuses.size()));
  }

  for (std::size_t busIndex = 0; busIndex < definitionPorts.size(); ++busIndex) {
    if (definitionPorts[busIndex].bus.size() == instanceBuses[busIndex].size())
      continue;

    throw std::runtime_error(std::format(
        "Module '{}' {} bus {} width mismatch: definition has {}, instance has {}",
        moduleKey, direction, busIndex, definitionPorts[busIndex].bus.size(),
        instanceBuses[busIndex].size()));
  }
}

void copyProperties(const Component& source, const Component_ptr& target)
{
  for (const auto& [key, value] : source.getProperties()) {
    if (target->getProperty(key).has_value())
      target->setProperty(key, value);
  }
}

void appendCircuit(const Circuit& circuit, ElaborationContext& context,
                   WirePtrMap wireMap, std::vector<std::string>& activeSubcircuits);

void appendClonedComponent(const Component_ptr& component, ElaborationContext& context,
                           WirePtrMap& wireMap)
{
  auto cloned = context.registry.create(component->typeName());
  if (!cloned) {
    throw std::runtime_error(std::format("Failed to create unknown component type: {}",
                                         component->typeName()));
  }

  copyProperties(*component, cloned);

  auto inputs = remapBuses(component->getInputs(), wireMap);
  cloned->setInputs(inputs);

  auto outputs = remapBuses(component->getOutputs(), wireMap);
  cloned->setOutputs(outputs);

  context.components.insert(std::move(cloned));
}

void appendSubcircuitInstance(const Component_ptr& component,
                              ElaborationContext& context, WirePtrMap& parentWireMap,
                              std::vector<std::string>& activeSubcircuits)
{
  const auto slug = component->getPropertyValue<std::string>("slug");
  if (!slug)
    throw std::runtime_error("Subcircuit component is missing a string slug property");

  const auto key = std::format("subcircuit:{}", *slug);
  ActiveKeyGuard activeSubcircuit(activeSubcircuits, *slug,
                                  "Recursive subcircuit dependency detected: ");

  auto definition = silicon::subcircuits::loadSubcircuitDefinition(*slug,
                                                                   context.registry);
  validateInterface(key, "input", definition.inputs, component->getInputs());
  validateInterface(key, "output", definition.outputs, component->getOutputs());

  auto externalInputs  = remapBuses(component->getInputs(), parentWireMap);
  auto externalOutputs = remapBuses(component->getOutputs(), parentWireMap);

  WirePtrMap moduleWireMap;
  mapInterfaceWires(definition.inputs, externalInputs, moduleWireMap);
  mapInterfaceWires(definition.outputs, externalOutputs, moduleWireMap);

  appendCircuit(definition.circuit, context, std::move(moduleWireMap),
                activeSubcircuits);
}

void appendCircuit(const Circuit& circuit, ElaborationContext& context,
                   WirePtrMap wireMap, std::vector<std::string>& activeSubcircuits)
{
  const auto& graph = circuit.getGraph();
  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    auto component = graph[vertex].component;
    if (!component)
      continue;

    if (component->typeName() == SubcircuitComponent::Type) {
      appendSubcircuitInstance(component, context, wireMap, activeSubcircuits);
      continue;
    }

    appendClonedComponent(component, context, wireMap);
  }
}

[[nodiscard]] WirePtrMap seedRootWireMap(const Circuit& circuit)
{
  WirePtrMap wireMap;
  for (const auto& [component, _vertex] : circuit.getComponentToVertex()) {
    if (!component)
      continue;

    auto seedBus = [&wireMap](const Bus& bus) {
      for (const auto& wire : bus) {
        if (!wire || wireMap.contains(wire.get()))
          continue;

        // Root source wires intentionally become runtime wires so UI/test handles
        // continue to drive and observe them. Any source-side driver authorization
        // would reject the newly cloned runtime component, so reset it at this
        // source-to-runtime ownership boundary.
        wire->clearAuthorizedComponent();
        wireMap.emplace(wire.get(), wire);
      }
    };

    for (const auto& bus : component->inputBuses())
      seedBus(bus);
    for (const auto& bus : component->outputBuses())
      seedBus(bus);
  }
  return wireMap;
}

}  // namespace

namespace silicon::elaboration {

CircuitElaborator::CircuitElaborator(const ComponentRegistry& registry)
  : registry(registry)
{
}

std::shared_ptr<Circuit> CircuitElaborator::elaborate(const Circuit& sourceCircuit) const
{
  ElaborationContext context{.registry = registry, .components = {}};
  auto                     rootWireMap = seedRootWireMap(sourceCircuit);
  std::vector<std::string> activeSubcircuits;
  appendCircuit(sourceCircuit, context, std::move(rootWireMap), activeSubcircuits);

  auto runtimeCircuit = std::make_shared<Circuit>(context.components, false);
  runtimeCircuit->setName(sourceCircuit.getName());
  runtimeCircuit->setDescription(sourceCircuit.getDescription());

  return runtimeCircuit;
}

}  // namespace silicon::elaboration
