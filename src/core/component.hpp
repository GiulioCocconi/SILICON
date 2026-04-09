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

using PropertyValue = std::variant<int, bool, std::string>;
using PropertyMap   = std::unordered_map<std::string, PropertyValue>;

class Component : public std::enable_shared_from_this<Component> {
protected:
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;

  std::string name;

  action_ptr act;

  PropertyMap properties;

  void defineProperty(std::string key, int defaultValue)
  { properties[std::move(key)] = defaultValue; }

  void defineProperty(std::string key, bool defaultValue)
  { properties[std::move(key)] = defaultValue; }

  void defineProperty(std::string key, std::string defaultValue)
  { properties[std::move(key)] = std::move(defaultValue); }

  void defineProperty(std::string key, const char* defaultValue)
  { properties[std::move(key)] = std::string(defaultValue); }

public:
  Component() = default;
  Component(std::vector<Bus> inputs, std::vector<Bus> outputs, std::string name);
  void toggleAction(const Bus& bus, bool add) const;
  void replaceBus(std::vector<Bus>& busCollection, unsigned int index, Bus newBus,
                  bool isInput);

  void setProperty(const std::string& key, const PropertyValue& value);
  std::optional<PropertyValue> getProperty(const std::string& key) const;
  const PropertyMap& getProperties() const { return properties; }


  void setAction(const action& a);

  void setInput(const unsigned int index, const Bus& bus);
  void setInputs(std::vector<Bus>& newInputs);

  void setOutput(unsigned int index, const Bus& bus);
  void setOutputs(std::vector<Bus>& newOutputs);

  bool isConnectedTo(const Bus& b);

  void setName(const std::string_view& newName) { this->name = newName; }

  void clearWires();

  std::vector<Bus> getInputs() const { return inputs; }
  std::vector<Bus> getOutputs() const { return outputs; }
  std::string      getName() const { return name; }

  virtual std::string_view typeName() const = 0;

  virtual ~Component();
};
