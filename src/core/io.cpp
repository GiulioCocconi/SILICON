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
#include <core/wireUtils.hpp>
#include <utils/num_formatting.hpp>

namespace SILICON::core {

namespace {

  constexpr std::string_view                PortOrientationProperty = "portOrientation";
  constexpr std::array<std::string_view, 4> PortOrientations = {"UP", "DOWN", "LEFT",
                                                                "RIGHT"};
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
    return std::clamp(size, 1,
                      static_cast<int>(std::numeric_limits<unsigned short>::max()));
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
  defineProperty("size", 1, [this](const PropertyValue& value) {
    return setSize(std::get<int>(value));
  });
  defineProperty("value", BusValue{State::LOW}, [this](const PropertyValue& value) {
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
  const int normalizedSize = std::clamp(
      newSize, 1, static_cast<int>(std::numeric_limits<unsigned short>::max()));
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

void ConstantComponent::serializeYosys(
    SILICON::yosys::SerializationContext& context) const
{
  if (outputs.empty())
    throw std::runtime_error("Cannot export a malformed constant component");

  const auto value = getPropertyValue<BusValue>("value").value_or(
      BusValue(outputs[0].size(), State::UNKNOWN));
  SILICON::yosys::Json bits = SILICON::yosys::Json::array();
  for (const State state : value) {
    switch (state) {
      case State::LOW: bits.push_back("0"); break;
      case State::HIGH: bits.push_back("1"); break;
      case State::UNKNOWN: bits.push_back("x"); break;
      case State::ERROR:
        throw std::runtime_error("Cannot export an ERROR constant to Yosys");
    }
  }
  SILICON::yosys::detail::emitUnary(context, "constant", "$pos", std::move(bits),
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
  defineProperty("startValue", BusValue{State::LOW}, [](const PropertyValue& value) {
    return normalizedEditableValue(std::get<BusValue>(value), 1, true);
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
  defineProperty("startValue", BusValue(outputs[0].size(), State::LOW),
                 [this](const PropertyValue& value) {
                   const auto width = outputs.empty() ? 1 : outputs[0].size();
                   return normalizedEditableValue(std::get<BusValue>(value), width, true);
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
  if (const auto startValue = getPropertyValue<BusValue>("startValue"))
    setProperty("startValue", *startValue);
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
