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

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

#include <utils/ranges_wrapper.hpp>

Component::Component(std::vector<Bus> inputs, std::vector<Bus> outputs)
  : inputs(std::move(inputs)), outputs(std::move(outputs))
{
}

ComponentMetadata Component::metadata() const
{
  const std::string type = std::string(typeName());
  return {type, type, ComponentCategory::Utils};
}

void Component::serializeYosys(silicon::yosys::SerializationContext&) const
{
  // Export is opt-in: a component must override this method with an exact lowering.
  // Failing here prevents an unsupported component from silently disappearing from
  // the generated netlist and changing the circuit's behavior.
  throw std::runtime_error(std::format(
      "Component type '{}' does not support Yosys serialization", typeName()));
}

uint64_t Component::addIOListener(IOObserver cb)
{
  uint64_t id     = ++nextIoListenerId;
  ioListeners[id] = std::move(cb);
  return id;
}

void Component::removeIOListener(uint64_t id)
{
  ioListeners.erase(id);
}

void Component::notifyIOListeners()
{
  for (auto& [id, cb] : ioListeners) {
    cb(this);
  }
}

void Component::defineStringListProperty(std::string key, std::string defaultValue,
                                         StringPropertyOptions allowedValues,
                                         PropertyCallback      callback)
{
  if (allowedValues.empty()) {
    throw std::invalid_argument(std::format(
        "String-list property '{}' on component '{}' must define allowed values", key,
        typeName()));
  }

  const bool defaultAllowed = std::ranges::contains(allowedValues, defaultValue);
  if (!defaultAllowed) {
    throw std::invalid_argument(std::format(
        "Default value '{}' is not allowed for property '{}' on component '{}'",
        defaultValue, key, typeName()));
  }

  const std::string propertyKey = key;
  stringPropertyOptions[key]    = std::move(allowedValues);
  defineProperty(std::move(key), std::move(defaultValue), std::move(callback));
  const auto& propertyValue = properties.at(propertyKey);
  validatePropertyValue(propertyKey, propertyValue, propertyValue);
}

void Component::validatePropertyValue(std::string_view     key,
                                      const PropertyValue& currentValue,
                                      const PropertyValue& newValue) const
{
  if (currentValue.index() != newValue.index()) {
    throw std::invalid_argument(std::format(
        "Type mismatch when setting property '{}' on component '{}'", key, typeName()));
  }

  const auto options = stringPropertyOptions.find(key);
  if (options == stringPropertyOptions.end())
    return;

  const auto* stringValue = std::get_if<std::string>(&newValue);
  if (!stringValue || !std::ranges::contains(options->second, *stringValue)) {
    throw std::invalid_argument(std::format(
        "Invalid value for property '{}' on component '{}'", key, typeName()));
  }
}
void Component::handleInputSizeChange(const unsigned int index,
                                      const unsigned int newSize)
{
  const auto currentSize = inputs[index].size();
  if (currentSize != newSize)
    throw std::runtime_error(std::format(
        "Input size mismatch, currently is {} but wants to be {}", currentSize, newSize));
}

void Component::replaceBus(std::vector<Bus>& busCollection, const unsigned int index,
                           Bus newBus, bool isInput)
{
  if (busCollection.size() > index && busCollection[index] == newBus)
    return;
  if (busCollection.size() <= index)
    busCollection.resize(index + 1);
  busCollection[index] = std::move(newBus);
}

// --- Properties Management -------------------------------------------------------------

void Component::setProperty(std::string_view key, const PropertyValue& value)
{
  auto it = properties.find(key);

  // Enforce static set of properties
  if (it == properties.end()) {
    throw std::invalid_argument(
        std::format("Property '{}' does not exist on component '{}'", key, typeName()));
  }

  validatePropertyValue(key, it->second, value);

  // Call callback if it's registered. Update the value accordingly
  auto cbIt = propertyCallbacks.find(key);

  const PropertyValue finalValue =
      (cbIt == propertyCallbacks.end()) ? value : cbIt->second(value);

  validatePropertyValue(key, it->second, finalValue);
  it->second = finalValue;
}

std::optional<PropertyValue> Component::getProperty(std::string_view key) const
{
  if (const auto it = properties.find(key); it != properties.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const StringPropertyOptions>>
Component::getStringPropertyOptions(std::string_view key) const
{
  if (const auto it = stringPropertyOptions.find(key);
      it != stringPropertyOptions.end()) {
    return std::cref(it->second);
  }
  return std::nullopt;
}

void Component::setPropertyCallback(std::string_view key, PropertyCallback callback)
{
  const auto property = properties.find(key);
  if (property == properties.end()) {
    throw std::invalid_argument(
        std::format("Property '{}' does not exist on component '{}'", key, typeName()));
  }

  propertyCallbacks.insert_or_assign(std::string(key), std::move(callback));
  const PropertyValue finalValue = propertyCallbacks.find(key)->second(property->second);
  validatePropertyValue(key, property->second, finalValue);
  property->second = finalValue;
}

void Component::setInput(const unsigned int index, const Bus& bus, const bool checkSize)
{
  if (this->inputs.size() > index && this->inputs[index] == bus)
    return;

  if (checkSize)
    handleInputSizeChange(index, bus.size());
  replaceBus(this->inputs, index, bus, true);
  notifyIOListeners();
}

void Component::setOutput(const unsigned int index, const Bus& bus)
{
  if (this->outputs.size() > index && this->outputs[index] == bus)
    return;
  replaceBus(this->outputs, index, bus, false);
  notifyIOListeners();
}

void Component::setInputs(std::vector<Bus>& newInputs)
{
  if (this->inputs == newInputs)
    return;
  this->inputs.resize(newInputs.size());
  for (auto [index, bus] : newInputs | silicon::views::enumerate) {
    replaceBus(this->inputs, index, bus, true);
  }
  notifyIOListeners();
}

void Component::setOutputs(std::vector<Bus>& newOutputs)
{
  if (this->outputs == newOutputs)
    return;
  this->outputs.resize(newOutputs.size());
  for (auto [index, bus] : newOutputs | silicon::views::enumerate) {
    replaceBus(this->outputs, index, bus, false);
  }
  notifyIOListeners();
}

bool Component::isConnectedTo(const Bus& b) const
{
  return (std::ranges::find(inputs, b) != inputs.end()
          || std::ranges::find(outputs, b) != outputs.end());
}

void Component::clearWires()
{
  for (auto [index, bus] : this->outputs | silicon::views::enumerate) {
    replaceBus(this->outputs, index, Bus(std::vector<Wire_ptr>(bus.size())), false);
  }
  for (auto [index, bus] : this->inputs | silicon::views::enumerate) {
    replaceBus(this->inputs, index, Bus(std::vector<Wire_ptr>(bus.size())), true);
  }
  notifyIOListeners();
}
