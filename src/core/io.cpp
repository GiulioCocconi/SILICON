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
#include <limits>
#include <stdexcept>
#include <utility>

#include <core/serialization/yosys_helpers.hpp>
#include <core/simulator.hpp>
#include <utils/num_formatting.hpp>

namespace SILICON::core {

namespace {

  constexpr std::string_view                PortOrientationProperty = "portOrientation";
  constexpr std::array<std::string_view, 4> PortOrientations = {"UP", "DOWN", "LEFT",
                                                                "RIGHT"};
  constexpr int MaxEditableBus = std::numeric_limits<unsigned int>::digits - 1;

  [[nodiscard]] StringPropertyOptions orientationOptions()
  {
    StringPropertyOptions options;
    options.reserve(PortOrientations.size());
    for (const auto orientation : PortOrientations)
      options.emplace_back(orientation);
    return options;
  }

  [[nodiscard]] int normalizedBusSize(const int size)
  {
    return std::clamp(size, 1, MaxEditableBus);
  }

}  // namespace

ConstantComponent::ConstantComponent()
{
  defineStringListProperty("value", "0", {"0", "1", "x"});
}

ConstantComponent::ConstantComponent(Wire_ptr output, std::string value)
  : ConstantComponent()
{
  outputs = {{std::move(output)}};
  setProperty("value", std::move(value));
}

void ConstantComponent::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputs.empty() || outputs[0].size() != 1)
    return;

  const auto value = getPropertyValue<std::string>("value").value_or("x");
  const auto state = value == "0"   ? State::LOW
                     : value == "1" ? State::HIGH
                                    : State::UNKNOWN;
  sim.updateWire(outputs[0][0], state, 0, weak_from_this());
}

void ConstantComponent::serializeYosys(
    SILICON::yosys::SerializationContext& context) const
{
  if (outputs.empty() || outputs[0].size() != 1)
    throw std::runtime_error("Cannot export a malformed constant component");

  const auto value = getPropertyValue<std::string>("value").value_or("x");
  SILICON::yosys::detail::emitUnary(context, "constant", "$pos",
                                    SILICON::yosys::Json::array({value}),
                                    context.bits(outputs[0]));
}

BoundaryIoComponent::BoundaryIoComponent(std::vector<Bus> inputs,
                                         std::vector<Bus> outputs, std::string name)
  : Component(std::move(inputs), std::move(outputs))
{
  defineProperty("name", std::move(name));
  defineStringListProperty(std::string(PortOrientationProperty), "DOWN",
                           orientationOptions());
}

void BoundaryIoComponent::serializeYosys(
    SILICON::yosys::SerializationContext& context) const
{
  const auto role = metadata().portRole;
  if (role == PortRole::Input) {
    context.addPort(getPropertyValue<std::string>("name").value_or("input"), "input",
                    outputBuses().at(0));
  } else if (role == PortRole::Output) {
    context.addPort(getPropertyValue<std::string>("name").value_or("output"), "output",
                    inputBuses().at(0));
  } else {
    throw std::runtime_error("Cannot export boundary I/O without a port role");
  }
}

DummyInputComponent::DummyInputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({}, {std::move(bus)}, std::move(name))
{
  defineProperty("startValue", 0, [](const PropertyValue& value) {
    return std::clamp(std::get<int>(value), 0, 1);
  });
}

DummyInputComponent::DummyInputComponent() : DummyInputComponent(Bus{}, "in")
{
  outputs.clear();
}

DummyBusInputComponent::DummyBusInputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({}, {std::move(bus)}, std::move(name))
{
  defineProperty(
      "size", static_cast<int>(outputs[0].size()),
      [this](const PropertyValue& value) { return setSize(std::get<int>(value)); });
  defineProperty("startValue", 0, [this](const PropertyValue& value) {
    const auto width    = outputs.empty() ? 1 : outputs[0].size();
    const int  maxValue = static_cast<int>(SILICON::core::maxValueForBusWidth(width));
    return std::clamp(std::get<int>(value), 0, maxValue);
  });
}

DummyBusInputComponent::DummyBusInputComponent() : DummyBusInputComponent(Bus{}, "bus_in")
{
  outputs.clear();
}

int DummyBusInputComponent::setSize(const int newSize)
{
  const int size = normalizedBusSize(newSize);
  Bus       bus  = outputs.empty() ? Bus(size) : outputs[0];
  bus.setSize(static_cast<unsigned short>(size));
  setOutput(0, bus);
  return size;
}

DummyOutputComponent::DummyOutputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({std::move(bus)}, {}, std::move(name))
{
}

DummyOutputComponent::DummyOutputComponent() : DummyOutputComponent(Bus{}, "out")
{
  inputs.clear();
}

DummyBusOutputComponent::DummyBusOutputComponent(Bus bus, std::string name)
  : BoundaryIoComponent({std::move(bus)}, {}, std::move(name))
{
  defineProperty(
      "size", static_cast<int>(inputs[0].size()),
      [this](const PropertyValue& value) { return setSize(std::get<int>(value)); });
}

DummyBusOutputComponent::DummyBusOutputComponent()
  : DummyBusOutputComponent(Bus{}, "bus_out")
{
  inputs.clear();
}

int DummyBusOutputComponent::setSize(const int newSize)
{
  const int size = normalizedBusSize(newSize);
  Bus       bus  = inputs.empty() ? Bus(size) : inputs[0];
  bus.setSize(static_cast<unsigned short>(size));
  setInput(0, bus);
  return size;
}

}  // namespace SILICON::core
