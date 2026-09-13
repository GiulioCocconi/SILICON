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

#include "circuitDocument.hpp"
#include <stdexcept>
#include <vector>

#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::core {

namespace {

  [[nodiscard]] std::vector<Bus>
  makeExternalBuses(const std::vector<CircuitPort>& interfacePorts)
  {
    std::vector<Bus> buses;
    buses.reserve(interfacePorts.size());
    for (const auto& port : interfacePorts)
      buses.emplace_back(static_cast<unsigned short>(port.bus.size()));
    return buses;
  }

}  // namespace

SubcircuitComponent::SubcircuitComponent(const CircuitResolver* resolver)
  : resolver(resolver)
{
  defineProperty(std::string("slug"), std::string(), [this](const PropertyValue& value) {
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

  auto definition      = resolver->resolve(slug);
  auto externalInputs  = makeExternalBuses(definition.inputs);
  auto externalOutputs = makeExternalBuses(definition.outputs);
  setInputs(externalInputs);
  setOutputs(externalOutputs);
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

}  // namespace SILICON::core
