/*
 Copyright (c) 2025. Giulio Cocconi

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

#pragma once

#include <array>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include <utils/ranges_wrapper.hpp>

#include <core/wire.hpp>

template <typename T>
concept HasType = requires {
  { T::Type } -> std::convertible_to<std::string_view>;
};

using PropertyValue       = std::variant<int, bool, std::string>;
using PropertyMap         = std::unordered_map<std::string, PropertyValue>;
using PropertyCallback    = std::function<PropertyValue(const PropertyValue&)>;
using PropertyCallbackMap = std::unordered_map<std::string, PropertyCallback>;

class Component : public std::enable_shared_from_this<Component> {
protected:
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;

  action_ptr act;

  PropertyMap         properties;
  PropertyCallbackMap propertyCallbacks;

  void defineProperty(std::string key, int defaultValue,
                      PropertyCallback callback = nullptr)
  {
    properties[std::move(key)] = defaultValue;
    if (callback)
      propertyCallbacks[key] = std::move(callback);
  }

  void defineProperty(std::string key, bool defaultValue,
                      PropertyCallback callback = nullptr)
  {
    properties[std::move(key)] = defaultValue;
    if (callback)
      propertyCallbacks[std::move(key)] = std::move(callback);
  }

  void defineProperty(std::string key, std::string defaultValue,
                      PropertyCallback callback = nullptr)
  {
    properties[std::move(key)] = std::move(defaultValue);
    if (callback)
      propertyCallbacks[std::move(key)] = std::move(callback);
  }

  void defineProperty(std::string key, const char* defaultValue,
                      PropertyCallback callback = nullptr)
  {
    properties[std::move(key)] = std::string(defaultValue);
    if (callback)
      propertyCallbacks[std::move(key)] = std::move(callback);
  }

public:
  Component() = default;
  Component(std::vector<Bus> inputs, std::vector<Bus> outputs);
  void toggleAction(const Bus& bus, bool add) const;
  void replaceBus(std::vector<Bus>& busCollection, unsigned int index, Bus newBus,
                  bool isInput);

  void setProperty(const std::string& key, const PropertyValue& value);
  std::optional<PropertyValue> getProperty(const std::string& key) const;
  const PropertyMap&           getProperties() const { return properties; }

  void setPropertyCallback(const std::string& key, PropertyCallback callback);

  void setAction(const action& a);

  void setInput(const unsigned int index, const Bus& bus);
  void setInputs(std::vector<Bus>& newInputs);

  void setOutput(unsigned int index, const Bus& bus);
  void setOutputs(std::vector<Bus>& newOutputs);

  bool isConnectedTo(const Bus& b);

  void clearWires();

  std::vector<Bus> getInputs() const { return inputs; }
  std::vector<Bus> getOutputs() const { return outputs; }

  virtual std::string_view typeName() const = 0;

  virtual ~Component();
};
