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

#include "subcircuit.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::core {

namespace {

  [[nodiscard]] std::vector<uint64_t> portIds(const std::vector<CircuitPort>& ports)
  {
    if (std::ranges::any_of(ports, [](const CircuitPort& port) { return port.id == 0; }))
      return {};  // Topology-only interfaces have no persistent identity.

    return ports | std::views::transform(&CircuitPort::id)
           | std::ranges::to<std::vector>();
  }

  void preserveConnection(Bus& target, const Bus& source)
  {
    auto       wires = static_cast<std::vector<Wire_ptr>>(target);
    const auto saved = static_cast<std::vector<Wire_ptr>>(source);
    for (auto [destination, original] : std::views::zip(wires, saved))
      destination = original;
    target = Bus(std::move(wires));
  }

  [[nodiscard]] std::vector<Bus>
  remapConnections(const std::vector<Bus>&      expected,
                   const std::vector<uint64_t>& expectedIds,
                   const std::vector<Bus>& saved, const std::vector<uint64_t>& savedIds)
  {
    if (!savedIds.empty()) {
      std::unordered_set<uint64_t> seen;
      if (savedIds.size() != saved.size())
        throw std::runtime_error(
            "Subcircuit port identity count does not match its buses");
      for (const auto& id : savedIds)
        if (id == 0 || !seen.insert(id).second)
          throw std::runtime_error(
              "Subcircuit port identities must be nonzero and unique");
    }

    // Start with the current interface widths and fresh wires. Reuse only the
    // overlapping bits from a matching saved port; any added bits stay fresh.
    auto result = expected;
    for (std::size_t portIndex = 0; portIndex < result.size(); ++portIndex) {
      auto savedPortIndex = portIndex;
      if (!savedIds.empty()) {
        if (portIndex >= expectedIds.size())
          continue;  // An implicit topology port cannot inherit a removed boundary ID.
        const auto match = std::ranges::find(savedIds, expectedIds.at(portIndex));
        if (match == savedIds.end())
          continue;
        savedPortIndex = static_cast<std::size_t>(match - savedIds.begin());
      }
      if (savedPortIndex >= saved.size())
        continue;
      preserveConnection(result[portIndex], saved[savedPortIndex]);
    }
    return result;
  }

  [[nodiscard]] std::vector<Bus> externalBuses(const std::vector<CircuitPort>& ports)
  {
    return ports | std::views::transform([](const CircuitPort& port) {
             return Bus(static_cast<unsigned short>(port.bus.size()));
           })
           | std::ranges::to<std::vector>();
  }

}  // namespace

SubcircuitComponent::SubcircuitComponent(const CircuitResolver* resolver)
  : resolver(resolver)
{
  static const auto slugDefinition = propertyDefinition(
      std::string("slug"), std::string(),
      "Circuit document to instantiate. Its resolved named boundary determines this "
      "component's ports; an empty identifier leaves the interface unbound.");
  defineProperty(slugDefinition, [this](const PropertyValue& value) {
    configureFromSlug(std::get<std::string>(value));
    return value;
  });
}

std::shared_ptr<SubcircuitComponent> SubcircuitComponent::imported(
    std::string slug, std::vector<std::string> inputNames, std::vector<Bus> inputs,
    std::vector<std::string> outputNames, std::vector<Bus> outputs)
{
  auto component                  = std::make_shared<SubcircuitComponent>();
  component->properties["slug"]   = std::move(slug);
  component->transientInputNames  = std::move(inputNames);
  component->transientOutputNames = std::move(outputNames);
  component->setInputs(inputs);
  component->setOutputs(outputs);
  return component;
}

void SubcircuitComponent::clearResolvedCircuit()
{
  transientInputNames.clear();
  transientOutputNames.clear();
  inputIds.clear();
  outputIds.clear();
  inputs.clear();
  outputs.clear();
  notifyIOListeners();
}

void SubcircuitComponent::configureFromSlug(std::string_view slug)
{
  if (slug.empty()) {
    clearResolvedCircuit();
    return;
  }

  if (!resolver) {
    clearResolvedCircuit();
    return;
  }

  auto       definition     = resolver->resolve(slug);
  const bool sameDefinition = getPropertyValue<std::string>("slug") == slug;
  auto       newInputs      = externalBuses(definition.inputs);
  auto       newOutputs     = externalBuses(definition.outputs);
  if (sameDefinition) {
    newInputs = remapConnections(newInputs, portIds(definition.inputs), inputs, inputIds);
    newOutputs =
        remapConnections(newOutputs, portIds(definition.outputs), outputs, outputIds);
  }
  transientInputNames.clear();
  transientOutputNames.clear();
  for (const auto& port : definition.inputs)
    transientInputNames.push_back(port.name);
  for (const auto& port : definition.outputs)
    transientOutputNames.push_back(port.name);
  inputIds  = portIds(definition.inputs);
  outputIds = portIds(definition.outputs);
  // Update both directions before notifying observers of the new interface.
  inputs  = std::move(newInputs);
  outputs = std::move(newOutputs);
  notifyIOListeners();
}

void SubcircuitComponent::restoreConnections(const std::vector<Bus>&      savedInputs,
                                             const std::vector<Bus>&      savedOutputs,
                                             const std::vector<uint64_t>& savedInputIds,
                                             const std::vector<uint64_t>& savedOutputIds)
{
  auto newInputs  = remapConnections(inputs, inputIds, savedInputs, savedInputIds);
  auto newOutputs = remapConnections(outputs, outputIds, savedOutputs, savedOutputIds);
  inputs          = std::move(newInputs);
  outputs         = std::move(newOutputs);
  notifyIOListeners();
}

std::vector<Bus>
SubcircuitComponent::inputsForPorts(const std::vector<CircuitPort>& ports) const
{
  return remapConnections(externalBuses(ports), portIds(ports), inputs, inputIds);
}

std::vector<Bus>
SubcircuitComponent::outputsForPorts(const std::vector<CircuitPort>& ports) const
{
  return remapConnections(externalBuses(ports), portIds(ports), outputs, outputIds);
}

void SubcircuitComponent::setCircuitResolver(const CircuitResolver* newResolver)
{
  if (resolver == newResolver)
    return;
  resolver = newResolver;
  reloadFromResolver();
}

const CircuitResolver* SubcircuitComponent::circuitResolver() const noexcept
{
  return resolver;
}

void SubcircuitComponent::reloadFromResolver()
{
  configureFromSlug(getPropertyValue<std::string>("slug").value_or(std::string()));
}

void SubcircuitComponent::simulate(SILICON::simulation::Simulator& sim)
{
  (void)sim;
  throw std::logic_error("Unprocessed SubcircuitComponent reached "
                         "SILICON::simulation::Simulator; preprocess the circuit "
                         "with CircuitElaborator or Session before simulation");
}

std::vector<PortDescription> SubcircuitComponent::portDescriptions() const
{
  auto result = Component::portDescriptions();
  for (auto& port : result) {
    const auto& names = port.direction == PortDirection::Input ? transientInputNames
                                                               : transientOutputNames;
    if (port.index < names.size())
      port.name = names[port.index];
    port.description = "Reusable circuit boundary signal: " + port.name + ".";
  }
  return result;
}

}  // namespace SILICON::core
