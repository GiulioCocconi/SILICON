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
#include <limits>
#include <map>
#include <ranges>
#include <stdexcept>

#include <utils/ranges_wrapper.hpp>

namespace SILICON::core {

const PropertyDefinition ZeroDelayProperty =
    propertyDefinition("delay", 0, PropagationDelayDescription, {.minimum = 0});

PropertyType propertyType(const PropertyValue& value)
{
  return std::visit(
      [](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, int>)
          return PropertyType::Integer;
        else if constexpr (std::is_same_v<T, bool>)
          return PropertyType::Boolean;
        else if constexpr (std::is_same_v<T, std::string>)
          return PropertyType::String;
        else {
          static_assert(std::is_same_v<T, BusValue>);
          return PropertyType::BusValue;
        }
      },
      value);
}

bool PropertyConstraints::contains(int value) const
{
  return (!minimum || value >= *minimum) && (!maximum || value <= *maximum);
}

PropertyValue PropertyDefinition::normalize(const PropertyValue& value) const
{
  if (constraints.minimum && constraints.maximum
      && *constraints.minimum > *constraints.maximum)
    throw std::logic_error("Invalid declared bounds for property '" + key + "'");
  if (propertyType(value) != type)
    throw std::invalid_argument("Type mismatch for property '" + key + "'");
  if (!acceptedValues.empty() && !std::ranges::contains(acceptedValues, value))
    throw std::invalid_argument("Invalid value for property '" + key + "'");
  if (const auto* number = std::get_if<int>(&value);
      number && !constraints.contains(*number)) {
    if (constraints.normalization == PropertyNormalization::Clamp)
      return std::clamp(*number,
                        constraints.minimum.value_or(std::numeric_limits<int>::min()),
                        constraints.maximum.value_or(std::numeric_limits<int>::max()));
    throw std::invalid_argument("Value outside declared bounds for property '" + key
                                + "'");
  }
  return value;
}

PropertyDefinition propertyDefinition(std::string key, PropertyValue defaultValue,
                                      std::string         description,
                                      PropertyConstraints constraints)
{
  return {.key          = key,
          .label        = std::move(key),
          .description  = std::move(description),
          .type         = propertyType(defaultValue),
          .defaultValue = std::move(defaultValue),
          .constraints  = std::move(constraints)};
}

PropertyDefinition stringPropertyDefinition(std::string key, std::string defaultValue,
                                            std::vector<std::string> acceptedValues,
                                            std::string              description)
{
  if (acceptedValues.empty() || !std::ranges::contains(acceptedValues, defaultValue))
    throw std::invalid_argument("String-list declaration requires an accepted default");
  return {.key            = key,
          .label          = std::move(key),
          .description    = std::move(description),
          .type           = PropertyType::String,
          .defaultValue   = std::move(defaultValue),
          .acceptedValues = acceptedValues | std::views::as_rvalue
                            | std::ranges::to<std::vector<PropertyValue>>()};
}

std::vector<PropertyDefinition> Component::declaredProperties() const
{
  return propertyDeclarations
         | std::views::transform(
             [](const auto* definition) -> const auto& { return *definition; })
         | std::ranges::to<std::vector>();
}

const PropertyDefinition& Component::propertyDeclaration(std::string_view key) const
{
  const auto it = std::ranges::find(propertyDeclarations, key, &PropertyDefinition::key);
  if (it == propertyDeclarations.end())
    throw std::invalid_argument("Unknown property declaration: " + std::string(key));
  return **it;
}

std::vector<PropertyDefinition> Component::propertyDefinitions() const
{
  return declaredProperties();
}

void Component::defineProperty(const PropertyDefinition&    definition,
                               PropertyCallback             callback,
                               std::optional<PropertyValue> initialValue)
{
  if (definition.access != PropertyAccess::Derived && !definition.defaultValue)
    throw std::invalid_argument("Stored property requires a default: " + definition.key);
  const auto it =
      std::ranges::find(propertyDeclarations, definition.key, &PropertyDefinition::key);
  if (it == propertyDeclarations.end())
    propertyDeclarations.push_back(&definition);
  else
    *it = &definition;
  if (definition.access == PropertyAccess::Derived)
    return;
  auto initial =
      definition.normalize(initialValue ? *initialValue : *definition.defaultValue);
  if (callback) {
    propertyCallbacks.insert_or_assign(definition.key, callback);
    initial = callback(initial);
  }
  properties.insert_or_assign(definition.key, definition.normalize(initial));
}

std::vector<PortDescription> Component::portDescriptions() const
{
  std::vector<PortDescription> result;
  for (const auto direction : {PortDirection::Input, PortDirection::Output}) {
    const auto& buses = direction == PortDirection::Input ? inputs : outputs;
    for (auto [i, bus] : buses | SILICON::views::enumerate)
      result.push_back(
          {std::format("{}{}", direction == PortDirection::Input ? "i" : "o", i),
           direction, static_cast<unsigned int>(i), static_cast<unsigned int>(bus.size()),
           direction == PortDirection::Input ? "Input signal." : "Output signal."});
  }
  return result;
}

ComponentDescription Component::describe() const
{
  auto ports = portDescriptions();
  // Attached buses are authoritative, including constructor-inferred widths.
  for (auto& port : ports) {
    const auto& buses = port.direction == PortDirection::Input ? inputs : outputs;
    if (port.index < buses.size())
      port.width = buses[port.index].size();
  }
  auto definitions = propertyDefinitions();
  auto values      = properties;
  for (const auto& definition : definitions)
    if (definition.access == PropertyAccess::Derived)
      if (const auto value = getProperty(definition.key))
        values.insert_or_assign(definition.key, *value);
  return {.metadata   = metadata(),
          .ports      = std::move(ports),
          .properties = std::move(definitions),
          .values     = std::move(values)};
}

void Component::setProperties(const PropertyMap& values)
{
  // Dimensions and other scalar properties settle before bus-value normalization.
  const std::map ordered(values.begin(), values.end());
  for (const bool busValue : {false, true})
    for (const auto& [key, value] : ordered)
      if (std::holds_alternative<BusValue>(value) == busValue)
        setProperty(key, value);
}

Component::Component(std::vector<Bus> inputs, std::vector<Bus> outputs)
  : inputs(std::move(inputs)), outputs(std::move(outputs))
{
}

ComponentMetadata Component::metadata() const
{
  const std::string type = std::string(typeName());
  return {.displayName = type, .description = type, .category = ComponentCategory::Utils};
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
                                         PropertyCallback      callback,
                                         std::string           description)
{
  auto definition = std::make_unique<PropertyDefinition>(
      stringPropertyDefinition(std::move(key), std::move(defaultValue),
                               std::move(allowedValues), std::move(description)));
  auto* declaration = definition.get();
  ownedPropertyDeclarations.push_back(std::move(definition));
  defineProperty(*declaration, std::move(callback));
  declaration->defaultValue = properties.at(declaration->key);
}

PropertyValue Component::validatedProperty(std::string_view     key,
                                           const PropertyValue& value) const
{
  const auto& definition = propertyDeclaration(key);
  if (definition.access != PropertyAccess::Editable)
    throw std::invalid_argument("Property is read-only: " + std::string(key));
  return definition.normalize(value);
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
                           Bus newBus)
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

  const auto& definition = propertyDeclaration(key);
  if (definition.access != PropertyAccess::Editable)
    throw std::invalid_argument("Property is read-only: " + std::string(key));
  auto normalized = definition.normalize(value);

  // Call callback if it's registered. Update the value accordingly
  auto cbIt = propertyCallbacks.find(key);

  if (cbIt != propertyCallbacks.end())
    normalized = definition.normalize(cbIt->second(normalized));

  it->second = std::move(normalized);
}

std::optional<PropertyValue> Component::getProperty(std::string_view key) const
{
  if (const auto it = properties.find(key); it != properties.end()) {
    return it->second;
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

  const auto [callbackIt, inserted] =
      propertyCallbacks.insert_or_assign(std::string(key), std::move(callback));
  PropertyValue finalValue = callbackIt->second(property->second);
  property->second         = validatedProperty(key, finalValue);
}

void Component::setInput(const unsigned int index, const Bus& bus, const bool checkSize)
{
  if (this->inputs.size() > index && this->inputs[index] == bus)
    return;

  if (checkSize)
    handleInputSizeChange(index, bus.size());
  replaceBus(this->inputs, index, bus);
  notifyIOListeners();
}

void Component::setOutput(const unsigned int index, const Bus& bus)
{
  if (this->outputs.size() > index && this->outputs[index] == bus)
    return;
  replaceBus(this->outputs, index, bus);
  notifyIOListeners();
}

void Component::setInputs(const std::vector<Bus>& newInputs)
{
  if (this->inputs == newInputs)
    return;
  this->inputs.resize(newInputs.size());
  for (auto [index, bus] : newInputs | SILICON::views::enumerate) {
    replaceBus(this->inputs, index, bus);
  }
  notifyIOListeners();
}

void Component::setOutputs(const std::vector<Bus>& newOutputs)
{
  if (this->outputs == newOutputs)
    return;
  this->outputs.resize(newOutputs.size());
  for (auto [index, bus] : newOutputs | SILICON::views::enumerate) {
    replaceBus(this->outputs, index, bus);
  }
  notifyIOListeners();
}

bool Component::isConnectedTo(const Bus& b) const
{
  return std::ranges::contains(allBuses(), b);
}

void Component::clearWires()
{
  for (auto& bus :
       std::array{std::span<Bus>(inputs), std::span<Bus>(outputs)} | std::views::join)
    bus = Bus(std::vector<Wire_ptr>(bus.size()));
  notifyIOListeners();
}

}  // namespace SILICON::core
