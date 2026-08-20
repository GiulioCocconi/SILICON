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
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <core/wire.hpp>
#include <utils/transparent_string.hpp>

namespace SILICON::yosys {
class SerializationContext;
}

namespace SILICON::simulation {
class Simulator;
}

namespace SILICON::core {

enum class ComponentCategory {
  Gates,
  Multiplexers,
  Arithmetic,
  FlipFlops,
  Register,
  Subcircuits,
  Inputs,
  Outputs,
  Utils,
};

/**
 * @brief Declares whether a component represents a public circuit boundary port.
 *
 * This is deliberately separate from @ref ComponentCategory: catalog categories may
 * contain components that are not module ports (for example, constants in Inputs).
 */
enum class PortRole {
  None,
  Input,
  Output,
};

struct ComponentMetadata {
  std::string       displayName;
  std::string       description;
  ComponentCategory category;
  PortRole          portRole = PortRole::None;
};

}  // namespace SILICON::core

namespace SILICON::simulation {

/**
 * @brief Context describing why a component is being evaluated.
 *
 * Components that do not need scheduling context can keep implementing the legacy
 * simulate(Simulator&) overload. Timing-aware components can override the overload
 * that receives this structure.
 */
struct Context {
  /** @brief True for initial or otherwise full-plan evaluation. */
  bool initialEvaluation = false;

  /** @brief Buses that caused a reactive forward-cone evaluation. */
  std::span<const core::Bus> changedBuses{};

  /** @brief Wire states captured before the reactive mutation that caused this pass. */
  std::unordered_map<uint64_t, core::State> previousWireStates{};

  /** @brief Returns true when a wire has a captured previous state different from now. */
  [[nodiscard]] bool changed(const core::Wire_ptr& wire) const
  {
    const auto previous = previousState(wire);
    return previous.has_value() && wire && *previous != wire->getCurrentState();
  }

  /** @brief Returns the state a wire had before this reactive evaluation, if captured. */
  [[nodiscard]] std::optional<core::State> previousState(const core::Wire_ptr& wire) const
  {
    if (!wire)
      return std::nullopt;

    const auto it = previousWireStates.find(wire->getId());
    if (it == previousWireStates.end())
      return std::nullopt;

    return it->second;
  }
};

}  // namespace SILICON::simulation

namespace SILICON::core {

/**
 * @brief Concept to check if a type has a Type static member.
 *
 * Used to identify component types during serialization and factory creation.
 *
 * @tparam T The type to check
 */
template <typename T>
concept HasType = requires {
  { T::Type } -> std::convertible_to<std::string_view>;
};

/**
 * @brief Variant type for component properties.
 *
 * Supports configuration scalars, strings, and native logic-bus values.
 */
using PropertyValue = std::variant<int, bool, std::string, BusValue>;

/** @brief Map from property names to their values */
using PropertyMap = std::unordered_map<std::string, PropertyValue, TransparentStringHash,
                                       TransparentStringEqual>;

/** @brief Callback function for property validation/transformation */
using PropertyCallback = std::function<PropertyValue(const PropertyValue&)>;

/** @brief Map from property names to their callback functions */
using PropertyCallbackMap =
    std::unordered_map<std::string, PropertyCallback, TransparentStringHash,
                       TransparentStringEqual>;

/** @brief Allowed values for a constrained string property */
using StringPropertyOptions = std::vector<std::string>;

/** @brief Map from property names to constrained string values */
using StringPropertyOptionsMap =
    std::unordered_map<std::string, StringPropertyOptions, TransparentStringHash,
                       TransparentStringEqual>;

/**
 * @class Component
 * @brief Base class for all digital circuit components.
 *
 * The Component class represents a fundamental building block in a digital
 * circuit. It manages input and output buses for data connectivity, maintains
 * configurable properties, and implements an observer pattern for I/O changes.
 *
 * Components operate in a reactive simulation environment where their
 * simulate() method is called to process inputs and produce outputs.
 *
 * @see Circuit
 * @see Simulator
 */
class Component : public std::enable_shared_from_this<Component> {
public:
  /**
   * @brief Callback type for I/O change notifications.
   *
   * IOObservers are invoked when the component's input or output buses change,
   * allowing dependent components or circuits to update accordingly.
   *
   * @param component Pointer to the component whose I/O changed
   */
  using IOObserver = std::function<void(Component*)>;

protected:
  /** @brief Input buses connected to this component */
  std::vector<Bus> inputs;

  /** @brief Output buses produced by this component */
  std::vector<Bus> outputs;

  /** @brief Per-input state used when a bus bit has no connected wire. */
  std::unordered_map<unsigned int, State> unconnectedInputDefaults;

  /** @brief Map of configurable properties for this component */
  PropertyMap properties;

  /** @brief Map of callbacks for property validation/transformation */
  PropertyCallbackMap propertyCallbacks;

  /** @brief Allowed values for constrained string properties */
  StringPropertyOptionsMap stringPropertyOptions;

  /** @brief Counter for generating unique I/O listener IDs */
  uint64_t nextIoListenerId = 0;

  /** @brief Map of I/O change listeners indexed by ID */
  std::unordered_map<uint64_t, IOObserver> ioListeners;

  /**
   * @brief Defines a property with a default value and optional callback.
   *
   * Used by derived components to declare their configurable properties
   * during construction.
   *
   * @tparam T The property value type
   * @param key The property name
   * @param defaultValue The default property value
   * @param callback Optional callback for value validation/transformation
   */
  template <typename T>
  void defineProperty(std::string key, T&& defaultValue,
                      PropertyCallback callback = nullptr)
  {
    PropertyCallback cb;
    if (callback) {
      cb                     = std::move(callback);
      propertyCallbacks[key] = cb;
    }
    using Decayed = std::decay_t<T>;
    PropertyValue defaultVal;
    if constexpr (std::is_constructible_v<std::string, T>
                  && !std::is_same_v<Decayed, bool>) {
      defaultVal = std::string(std::forward<T>(defaultValue));
    } else {
      defaultVal = PropertyValue(std::forward<T>(defaultValue));
    }
    const PropertyValue finalValue = cb ? cb(defaultVal) : defaultVal;
    properties[key]                = finalValue;
  }

  /**
   * @brief Defines a string property constrained to a fixed set of values.
   *
   * @param key The property name
   * @param defaultValue The default string value
   * @param allowedValues Non-empty list of accepted values
   * @param callback Optional callback for validation/transformation
   */
  void defineStringListProperty(std::string key, std::string defaultValue,
                                StringPropertyOptions allowedValues,
                                PropertyCallback      callback = nullptr);

  /**
   * @brief Notifies all registered I/O listeners of a change.
   *
   * Called when the component's input or output configuration changes,
   * typically after setInput/setOutput is called.
   */
  void notifyIOListeners();

  /** @brief Validates a property value against the declared type and metadata. */
  void validatePropertyValue(std::string_view key, const PropertyValue& currentValue,
                             const PropertyValue& newValue) const;

  /**
   * @brief Handles input size changes. By default input size shouldn't change
   * automatically.
   * @param index The index of the input being changed
   * @param newSize The new size of the input
   */
  void handleInputSizeChange(const unsigned int index, const unsigned int newSize);

  /**
   * @brief Converts a component-local bus enum to its storage index.
   * @tparam Enum Component input/output enum type
   * @param value Enum value to convert
   * @return Numeric bus index
   */
  template <typename Enum>
    requires std::is_enum_v<Enum>
  static constexpr unsigned int busIndex(Enum value)
  {
    return std::to_underlying(value);
  }

  /**
   * @brief Defines the state read from an input when its wire is unconnected.
   * @param input Input bus index
   * @param value State used for every unconnected bit on that input bus
   */
  void defineUnconnectedInputDefault(unsigned int input, State value)
  {
    unconnectedInputDefaults.insert_or_assign(input, value);
  }

  [[nodiscard]] State inputState(unsigned int input, unsigned short bit = 0) const
  {
    if (input < inputs.size() && bit < inputs[input].size() && inputs[input][bit])
      return Wire::safeGetCurrentState(inputs[input][bit]);

    return unconnectedInputDefault(input).value_or(State::ERROR);
  }

  [[nodiscard]] Wire_ptr inputWire(unsigned int input, unsigned short bit = 0) const
  {
    if (input >= inputs.size() || bit >= inputs[input].size())
      return {};

    return inputs[input][bit];
  }

  template <typename Enum>
    requires std::is_enum_v<Enum>
  [[nodiscard]] Wire_ptr inputWire(Enum input, unsigned short bit = 0) const
  {
    return inputWire(busIndex(input), bit);
  }

  /**
   * @brief Reads an input bit, returning ERROR when the bus or bit is missing.
   * @tparam Enum Component input enum type
   * @param input Input bus enum value
   * @param bit Bit index inside the bus
   * @return Current wire state, or ERROR for missing wiring
   */
  template <typename Enum>
    requires std::is_enum_v<Enum>
  [[nodiscard]] State inputState(Enum input, unsigned short bit = 0) const
  {
    return inputState(busIndex(input), bit);
  }

  [[nodiscard]] Wire_ptr outputWire(unsigned int output, unsigned short bit = 0) const
  {
    if (output >= outputs.size() || bit >= outputs[output].size())
      return {};

    return outputs[output][bit];
  }

  /**
   * @brief Returns an output wire, or null when the bus or bit is missing.
   * @tparam Enum Component output enum type
   * @param output Output bus enum value
   * @param bit Bit index inside the bus
   * @return Output wire pointer, or null
   */
  template <typename Enum>
    requires std::is_enum_v<Enum>
  [[nodiscard]] Wire_ptr outputWire(Enum output, unsigned short bit = 0) const
  {
    return outputWire(busIndex(output), bit);
  }

  [[nodiscard]] std::size_t outputBusSize(unsigned int output) const
  {
    return output < outputs.size() ? outputs[output].size() : 0;
  }

  /**
   * @brief Returns the size of an output bus, or zero when it is missing.
   * @tparam Enum Component output enum type
   * @param output Output bus enum value
   * @return Output bus width
   */
  template <typename Enum>
    requires std::is_enum_v<Enum>
  [[nodiscard]] std::size_t outputBusSize(Enum output) const
  {
    return outputBusSize(busIndex(output));
  }

public:
  Component() = default;

  /**
   * @brief Constructs a component with input and output buses.
   * @param inputs Vector of input buses
   * @param outputs Vector of output buses
   */
  Component(std::vector<Bus> inputs, std::vector<Bus> outputs);

  virtual ~Component() = default;

  /**
   * @brief Executes the component's logic.
   *
   * Called by the Simulator during circuit evaluation.
   * Each component implements this to read input bus states and produce
   * output bus states.
   *
   * @param sim The simulator executing this component
   */
  virtual void simulate(SILICON::simulation::Simulator& sim)
  {
    simulate(sim, SILICON::simulation::Context{true, {}});
  }

  /**
   * @brief Executes the component's logic with simulator scheduling context.
   *
   * The default implementation preserves backwards compatibility by delegating to
   * simulate(Simulator&). Components can override this overload only when they need
   * to inspect the evaluation context.
   *
   * @param sim The simulator executing this component
   * @param context Evaluation metadata for this simulation pass
   */
  virtual void simulate(SILICON::simulation::Simulator&     sim,
                        const SILICON::simulation::Context& context)
  {
    (void)context;
    simulate(sim);
  }

  /**
   * @brief Whether zero-delay output writes should be staged during reactive passes.
   *
   * Edge-triggered sequential components return true so all triggered outputs are
   * committed from the same pre-edge snapshot. Combinational components keep the
   * default immediate zero-delay behavior.
   */
  [[nodiscard]] virtual bool usesStagedSequentialOutputs() const { return false; }

  /**
   * @brief Replaces a bus in the input or output collection.
   *
   * @param busCollection The collection to modify (inputs or outputs)
   * @param index The index of the bus to replace
   * @param newBus The new bus to set
   * @param isInput True for input collection, false for output
   */
  void replaceBus(std::vector<Bus>& busCollection, unsigned int index, Bus newBus,
                  bool isInput);

  /**
   * @brief Sets a property value.
   * @param key The property name
   * @param value The property value
   */
  void setProperty(std::string_view key, const PropertyValue& value);

  /**
   * @brief Gets a property value.
   * @param key The property name
   * @return The property value if present
   */
  [[nodiscard]] std::optional<PropertyValue> getProperty(std::string_view key) const;

  /** @brief Gets all properties.
   * @return Reference to the property map
   */
  [[nodiscard]] const PropertyMap& getProperties() const { return properties; }

  /**
   * @brief Gets allowed values for a constrained string property.
   * @return The allowed values if the property is constrained
   */
  [[nodiscard]] std::optional<std::reference_wrapper<const StringPropertyOptions>>
  getStringPropertyOptions(std::string_view key) const;

  /**
   * @brief Sets a property value with type deduction.
   * @tparam T The property value type
   * @param key The property name
   * @param value The property value
   */
  template <typename T> void setPropertyValue(std::string_view key, T&& value)
  {
    setProperty(key, PropertyValue(std::forward<T>(value)));
  }

  /**
   * @brief Gets a property value with type deduction.
   * @tparam T The expected property type
   * @param key The property name
   * @return The property value if present and of correct type
   */
  template <typename T> std::optional<T> getPropertyValue(std::string_view key) const
  {
    auto it = properties.find(key);
    if (it != properties.end()) {
      if (const T* val = std::get_if<T>(&it->second)) {
        return *val;
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Sets a callback for property validation/transformation.
   * @param key The property name
   * @param callback The callback function
   */
  void setPropertyCallback(std::string_view key, PropertyCallback callback);

  /**
   * @brief Sets an input bus at a specific index.
   * @param index The input index
   * @param bus The bus to connect
   * @param checkSize Whether to call handleInputSizeChange
   */
  void setInput(unsigned int index, const Bus& bus, bool checkSize = false);

  /**
   * @brief Replaces all input buses.
   * @param newInputs The new input buses
   */
  void setInputs(const std::vector<Bus>& newInputs);

  /**
   * @brief Sets an output bus at a specific index.
   * @param index The output index
   * @param bus The bus to connect
   */
  void setOutput(unsigned int index, const Bus& bus);

  /**
   * @brief Replaces all output buses.
   * @param newOutputs The new output buses
   */
  void setOutputs(const std::vector<Bus>& newOutputs);

  /**
   * @brief Checks if this component is connected to a bus.
   * @param b The bus to check
   * @return True if connected
   */
  bool isConnectedTo(const Bus& b) const;

  /**
   * @brief Clears all wire connections.
   *
   * Removes references to all wires in input and output buses,
   * disconnecting this component from the circuit.
   *
   * Derived components may override this when some pins have component-specific
   * unconnected semantics.
   */
  virtual void clearWires();

  /**
   * @brief Gets all input buses.
   * @return Vector of input buses
   */
  [[nodiscard]] std::vector<Bus> getInputs() const { return inputs; }

  /**
   * @brief Gets all input buses without copying.
   * @return Const reference to the input bus vector
   */
  [[nodiscard]] const std::vector<Bus>& inputBuses() const { return inputs; }

  /**
   * @brief Gets the state used when an input has no connected wire.
   * @param input Input bus index
   * @return Declared default, or no value when the input is required
   */
  [[nodiscard]] std::optional<State>
  unconnectedInputDefault(unsigned int input) const
  {
    if (const auto it = unconnectedInputDefaults.find(input);
        it != unconnectedInputDefaults.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  /**
   * @brief Gets all output buses.
   * @return Vector of output buses
   */
  [[nodiscard]] std::vector<Bus> getOutputs() const { return outputs; }

  /**
   * @brief Gets all output buses without copying.
   * @return Const reference to the output bus vector
   */
  [[nodiscard]] const std::vector<Bus>& outputBuses() const { return outputs; }

  /**
   * @brief Gets the component type name.
   * @return String view of the type identifier
   */
  virtual std::string_view typeName() const = 0;

  /**
   * @brief Gets catalog metadata for this component type.
   * @return Display name, description, and category for UI catalogs.
   */
  [[nodiscard]] virtual ComponentMetadata metadata() const;

  /**
   * @brief Lower this component into native Yosys netlist cells.
   *
   * Third-party components that do not override this method fail export with a
   * descriptive error instead of silently producing an incomplete netlist.
   */
  virtual void serializeYosys(SILICON::yosys::SerializationContext& context) const;

  // --- IO Observer Pattern ---

  /**
   * @brief Adds a listener for I/O changes.
   * @param cb The callback function
   * @return Unique ID for later removing the listener
   */
  uint64_t addIOListener(IOObserver cb);

  /**
   * @brief Removes a previously added I/O listener.
   * @param id The ID returned by addIOListener
   */
  void removeIOListener(uint64_t id);
};

}  // namespace SILICON::core
