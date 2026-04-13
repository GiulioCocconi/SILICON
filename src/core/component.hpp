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

#include <core/wire.hpp>

class Simulator;

template <typename T>
concept HasType = requires {
  { T::Type } -> std::convertible_to<std::string_view>;
};

using PropertyValue       = std::variant<int, bool, std::string>;
using PropertyMap         = std::unordered_map<std::string, PropertyValue>;
using PropertyCallback    = std::function<PropertyValue(const PropertyValue&)>;
using PropertyCallbackMap = std::unordered_map<std::string, PropertyCallback>;

class Component : public std::enable_shared_from_this<Component> {
public:
  using IOObserver = std::function<void(Component*)>;

protected:
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;

  PropertyMap         properties;
  PropertyCallbackMap propertyCallbacks;

  uint64_t                                 nextIoListenerId = 0;
  std::unordered_map<uint64_t, IOObserver> ioListeners;

  template <typename T>
  void defineProperty(std::string key, T&& defaultValue, PropertyCallback callback = nullptr) {
    if (callback) {
      propertyCallbacks[key] = std::move(callback);
    }
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_constructible_v<std::string, T> && !std::is_same_v<Decayed, bool>) {
      properties[key] = std::string(std::forward<T>(defaultValue));
    } else {
      properties[key] = PropertyValue(std::forward<T>(defaultValue));
    }
  }

  void notifyIOListeners();

public:
  Component() = default;
  Component(std::vector<Bus> inputs, std::vector<Bus> outputs);
  virtual ~Component() = default;

  virtual void simulate(Simulator& sim) = 0;

  void replaceBus(std::vector<Bus>& busCollection, unsigned int index, Bus newBus, bool isInput);

  void setProperty(const std::string& key, const PropertyValue& value);
  [[nodiscard]] std::optional<PropertyValue> getProperty(const std::string& key) const;
  [[nodiscard]] const PropertyMap& getProperties() const { return properties; }

  template <typename T> void setPropertyValue(const std::string& key, T&& value) {
    setProperty(key, PropertyValue(std::forward<T>(value)));
  }

  template <typename T> std::optional<T> getPropertyValue(const std::string& key) const {
    auto it = properties.find(key);
    if (it != properties.end()) {
      if (const T* val = std::get_if<T>(&it->second)) {
        return *val;
      }
    }
    return std::nullopt;
  }

  void setPropertyCallback(const std::string& key, PropertyCallback callback);

  void setInput(unsigned int index, const Bus& bus);
  void setInputs(std::vector<Bus>& newInputs);

  void setOutput(unsigned int index, const Bus& bus);
  void setOutputs(std::vector<Bus>& newOutputs);

  bool isConnectedTo(const Bus& b) const;
  void clearWires();

  [[nodiscard]] std::vector<Bus> getInputs() const { return inputs; }
  [[nodiscard]] std::vector<Bus> getOutputs() const { return outputs; }

  virtual std::string_view typeName() const = 0;

  // --- IO Observer Pattern ---
  uint64_t addIOListener(IOObserver cb);
  void     removeIOListener(uint64_t id);
};
