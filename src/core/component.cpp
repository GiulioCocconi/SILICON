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

#include "component.hpp"
#include <ranges>
#include <stdexcept>

Component::Component(std::vector<Bus> inputs, std::vector<Bus> outputs)
  : inputs(std::move(inputs)), outputs(std::move(outputs))
{
}

// --- Helpers ---------------------------------------------------------------------------

void Component::toggleAction(const Bus& bus, bool add) const
{
  if (!this->act)
    return;

  for (const auto& w : bus) {
    if (w) {
      if (add)
        w->addUpdateAction(this->act);
      else
        w->deleteUpdateAction(this->act);
    }
  }
}

void Component::replaceBus(std::vector<Bus>& busCollection, const unsigned int index,
                           Bus newBus, bool isInput)
{
  if (busCollection[index] == newBus) {
    return;
  }

  // Keep a copy of the old bus before overwriting it
  Bus oldBus = busCollection[index];

  if (isInput)
    toggleAction(oldBus, false);

  busCollection[index] = newBus;

  if (isInput)
    toggleAction(newBus, true);
}

// --- Properties Management -------------------------------------------------------------

void Component::setProperty(const std::string& key, const PropertyValue& value)
{
  auto it = properties.find(key);

  // 1. Enforce static set of properties
  if (it == properties.end()) {
    throw std::invalid_argument(
        std::format("Property '{}' does not exist on component '{}'", key, typeName()));
  }

  // 2. Enforce static types (the variant index must match the one set during
  // defineProperty)
  if (it->second.index() != value.index()) {
    throw std::invalid_argument(std::format(
        "Type mismatch when setting property '{}' on component '{}'", key, typeName()));
  }

  // 3. Call the callback if registered, use returned value
  PropertyValue finalValue = value;
  if (auto cbIt = propertyCallbacks.find(key); cbIt != propertyCallbacks.end()) {
    finalValue = cbIt->second(value);
  }

  it->second = finalValue;
}

std::optional<PropertyValue> Component::getProperty(const std::string& key) const
{
  if (const auto it = properties.find(key); it != properties.end()) {
    return it->second;
  }
  return std::nullopt;
}

void Component::setPropertyCallback(const std::string& key, PropertyCallback callback)
{
  if (properties.find(key) == properties.end()) {
    throw std::invalid_argument(
        std::format("Property '{}' does not exist on component '{}'", key, typeName()));
  }
  propertyCallbacks[key] = std::move(callback);
}

// --- Other public Methods --------------------------------------------------------------

void Component::setInput(const unsigned int index, const Bus& bus)
{
  replaceBus(this->inputs, index, bus, true);
}

void Component::setOutput(const unsigned int index, const Bus& bus)
{
  replaceBus(this->outputs, index, bus, false);
}

void Component::setInputs(std::vector<Bus>& newInputs)
{
  if (this->inputs == newInputs)
    return;

  // Resize instead of copy to avoid double-attaching logic in setInput
  if (this->inputs.size() != newInputs.size()) {
    this->inputs.resize(newInputs.size());
  }

  for (auto [index, bus] : newInputs | silicon::views::enumerate) {
    setInput(index, bus);
  }
}

void Component::setOutputs(std::vector<Bus>& newOutputs)
{
  if (this->outputs == newOutputs)
    return;

  if (this->outputs.size() != newOutputs.size()) {
    this->outputs.resize(newOutputs.size());
  }

  for (auto [index, bus] : newOutputs | silicon::views::enumerate) {
    setOutput(index, bus);
  }
}

bool Component::isConnectedTo(const Bus& b)
{
  return (std::ranges::find(inputs, b) != inputs.end()
          || std::ranges::find(outputs, b) != outputs.end());
}

void Component::clearWires()
{
  auto clearBuses = [](auto& busCollection, auto setterFunc) {
    for (const auto [index, bus] : busCollection | silicon::views::enumerate) {
      setterFunc(index, Bus(std::vector<Wire_ptr>(bus.size())));
    }
  };

  clearBuses(this->outputs, [this](auto i, const auto& b) { setOutput(i, b); });
  clearBuses(this->inputs, [this](auto i, const auto& b) { setInput(i, b); });
}

void Component::setAction(const action& a)
{
  this->act = std::make_shared<action>(a);
  if (!this->act)
    throw std::logic_error("Failed to create action");

  for (const auto& bus : this->inputs) {
    toggleAction(bus, true);
  }
}

Component::~Component()
{
  for (const auto& bus : this->inputs) {
    toggleAction(bus, false);
  }
}