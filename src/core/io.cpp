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

#include "io.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

#include <core/simulator.hpp>
#include <core/wireUtils.hpp>
#include <utils/num_formatting.hpp>

namespace SILICON::core {

namespace {

  constexpr char BoundaryNameDescription[] =
      "Signal name exposed by the reusable circuit interface and exported HDL boundary.";

  const auto sizeDefinition = propertyDefinition(
      "size", 1, "Configured data width in bits.",
      {.minimum       = 1,
       .maximum       = MaxBusWidth,
       .description   = "Out-of-range sizes are clamped to the supported bus width.",
       .normalization = PropertyNormalization::Clamp});
  const auto startValueDefinition = propertyDefinition(
      "startValue", BusValue{State::LOW},
      "Initial input value, normalized to the configured bus width.",
      {.description =
           "Nonempty LOW/HIGH/UNKNOWN bits; normalized to the port width. A scalar "
           "UNKNOWN "
           "fills the bus with UNKNOWN; other values are zero-extended or truncated."});

  std::atomic<uint64_t> nextPortIdentity{1};

  constexpr std::string_view                PORT_ORIENTATION_PROPERTY = "portOrientation";
  constexpr std::array<std::string_view, 4> PORT_ORIENTATIONS = {"UP", "DOWN", "LEFT",
                                                                "RIGHT"};
  [[nodiscard]] StringPropertyOptions orientationOptions()
  {
    return PORT_ORIENTATIONS | std::ranges::to<StringPropertyOptions>();
  }

  [[nodiscard]] int normalizedBusSize(const Component& component, const int size)
  {
    return std::get<int>(component.propertyDeclaration("size").normalize(size));
  }

  [[nodiscard]] BusValue normalizedEditableValue(const BusValue&   value,
                                                 const std::size_t width,
                                                 const bool        fillScalarUnknown)
  {
    if (value.empty())
      throw std::invalid_argument("Bus value must not be empty");
    if (std::ranges::contains(value, State::ERROR))
      throw std::invalid_argument("State::ERROR cannot be used as an editable value");

    const State extension =
        fillScalarUnknown && value.size() == 1 && value.front() == State::UNKNOWN
            ? State::UNKNOWN
            : State::LOW;
    return SILICON::wireUtils::normalizeBusValue(value, width, extension);
  }

}  // namespace

ConstantComponent::ConstantComponent()
{
  defineProperty(sizeDefinition, [this](const PropertyValue& value) {
    return setSize(std::get<int>(value));
  });
  static const auto valueDefinition = propertyDefinition(
      "value", BusValue{State::LOW},
      "Logic value driven by the constant, least-significant bit first.",
      {.description =
           "Nonempty LOW/HIGH/UNKNOWN bits; truncated or zero-extended to the configured "
           "width. A scalar UNKNOWN fills all bits. ERROR is reserved for simulation "
           "failures."});
  defineProperty(valueDefinition, [this](const PropertyValue& value) {
    return setValue(std::get<BusValue>(value));
  });
}

ConstantComponent::ConstantComponent(Wire_ptr output, BusValue value)
  : ConstantComponent(Bus{std::move(output)}, std::move(value))
{
}

ConstantComponent::ConstantComponent(Bus output, BusValue value) : ConstantComponent()
{
  if (output.size() == 0)
    throw std::invalid_argument("Constant output bus must not be empty");
  outputs = {std::move(output)};
  setProperty("size", static_cast<int>(outputs[0].size()));
  setProperty("value", std::move(value));
}

int ConstantComponent::setSize(const int newSize)
{
  const int normalizedSize =
      std::get<int>(propertyDeclaration("size").normalize(newSize));
  if (!outputs.empty()) {
    outputs[0].setSize(static_cast<unsigned short>(normalizedSize));
    notifyIOListeners();
  }
  if (const auto value = getPropertyValue<BusValue>("value"))
    setProperty("value", *value);
  return normalizedSize;
}

BusValue ConstantComponent::setValue(const BusValue& value) const
{
  const auto width =
      outputs.empty()
          ? static_cast<std::size_t>(getPropertyValue<int>("size").value_or(1))
          : outputs[0].size();
  return normalizedEditableValue(value, width, true);
}

void ConstantComponent::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputs.empty())
    return;

  sim.updateBus(outputs[0],
                getPropertyValue<BusValue>("value").value_or(
                    BusValue(outputs[0].size(), State::UNKNOWN)),
                0, weak_from_this());
}

BoundaryIoComponent::BoundaryIoComponent(std::vector<Bus> inputs,
                                         std::vector<Bus> outputs)
  : Component(std::move(inputs), std::move(outputs))
{
  resetPortId();
  static const auto left = stringPropertyDefinition(
      std::string(PORT_ORIENTATION_PROPERTY), "LEFT", orientationOptions(),
      "Side on which the external circuit boundary connection is placed.");
  static const auto right = stringPropertyDefinition(
      std::string(PORT_ORIENTATION_PROPERTY), "RIGHT", orientationOptions(),
      "Side on which the external circuit boundary connection is placed.");
  defineProperty(this->outputs.empty() ? left : right);
}

void BoundaryIoComponent::setPortId(uint64_t id)
{
  if (id == 0 || id == std::numeric_limits<uint64_t>::max())
    throw std::invalid_argument("Boundary port identity must be a nonzero integer");
  // Identities only need to be unique within one circuit. Reserve loaded numeric
  // IDs so newly created or pasted boundary ports cannot reuse them.
  auto next = nextPortIdentity.load(std::memory_order_relaxed);
  while (next <= id
         && !nextPortIdentity.compare_exchange_weak(next, id + 1,
                                                    std::memory_order_relaxed)) {}
  portIdentity = id;
}

void BoundaryIoComponent::resetPortId()
{
  portIdentity = nextPortIdentity.fetch_add(1, std::memory_order_relaxed);
}

DummyInputComponent::DummyInputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({}, {std::move(bus)})
{
  static const auto nameDefinition =
      propertyDefinition("name", std::string("in"), BoundaryNameDescription);
  defineProperty(nameDefinition, nullptr, PropertyValue{std::move(name)});

  defineProperty(startValueDefinition, [](const PropertyValue& value) {
    return normalizedEditableValue(std::get<BusValue>(value), 1, true);
  });
}

DummyInputComponent::DummyInputComponent() : DummyInputComponent(Bus{}, "in")
{
  outputs.clear();
}

DummyBusInputComponent::DummyBusInputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({}, {std::move(bus)})
{
  static const auto nameDefinition =
      propertyDefinition("name", std::string("bus_in"), BoundaryNameDescription);
  defineProperty(nameDefinition, nullptr, PropertyValue{std::move(name)});

  defineProperty(
      sizeDefinition,
      [this](const PropertyValue& value) { return setSize(std::get<int>(value)); },
      PropertyValue{static_cast<int>(outputs[0].size())});
  defineProperty(
      startValueDefinition,
      [this](const PropertyValue& value) {
        const auto width = outputs.empty() ? 1 : outputs[0].size();
        return normalizedEditableValue(std::get<BusValue>(value), width, true);
      },
      PropertyValue{BusValue(outputs[0].size(), State::LOW)});
}

DummyBusInputComponent::DummyBusInputComponent() : DummyBusInputComponent(Bus{}, "bus_in")
{
  outputs.clear();
}

int DummyBusInputComponent::setSize(const int newSize)
{
  const int size = normalizedBusSize(*this, newSize);
  Bus       bus  = outputs.empty() ? Bus(size) : outputs[0];
  bus.setSize(static_cast<unsigned short>(size));
  setOutput(0, bus);
  if (const auto startValue = getPropertyValue<BusValue>("startValue"))
    setProperty("startValue", *startValue);
  return size;
}

DummyOutputComponent::DummyOutputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({std::move(bus)}, {})
{
  static const auto nameDefinition =
      propertyDefinition("name", std::string("out"), BoundaryNameDescription);
  defineProperty(nameDefinition, nullptr, PropertyValue{std::move(name)});
}

DummyOutputComponent::DummyOutputComponent() : DummyOutputComponent(Bus{}, "out")
{
  inputs.clear();
}

DummyBusOutputComponent::DummyBusOutputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({std::move(bus)}, {})
{
  static const auto nameDefinition =
      propertyDefinition("name", std::string("bus_out"), BoundaryNameDescription);
  defineProperty(nameDefinition, nullptr, PropertyValue{std::move(name)});

  defineProperty(
      sizeDefinition,
      [this](const PropertyValue& value) { return setSize(std::get<int>(value)); },
      PropertyValue{static_cast<int>(inputs[0].size())});
}

DummyBusOutputComponent::DummyBusOutputComponent()
  : DummyBusOutputComponent(Bus{}, "bus_out")
{
  inputs.clear();
}

int DummyBusOutputComponent::setSize(const int newSize)
{
  const int size = normalizedBusSize(*this, newSize);
  Bus       bus  = inputs.empty() ? Bus(size) : inputs[0];
  bus.setSize(static_cast<unsigned short>(size));
  setInput(0, bus);
  return size;
}

std::vector<PortDescription> ConstantComponent::portDescriptions() const
{
  return {{"o", PortDirection::Output, 0,
           static_cast<unsigned int>(getPropertyValue<int>("size").value()),
           "Configured constant logic value."}};
}

std::vector<PortDescription> BoundaryIoComponent::portDescriptions() const
{
  const bool input = metadata().portRole == PortRole::Input;
  const auto width = static_cast<unsigned int>(getPropertyValue<int>("size").value_or(1));
  return {{getPropertyValue<int>("size") ? "bus"
           : input                       ? "o"
                                         : "in",
           input ? PortDirection::Output : PortDirection::Input, 0, width,
           input ? "Signal supplied to the circuit boundary."
                 : "Signal observed at the circuit boundary."}};
}

}  // namespace SILICON::core
