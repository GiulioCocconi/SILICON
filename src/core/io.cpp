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

  [[nodiscard]] std::string normalizedConstantValue(std::string value,
                                                    const std::size_t width)
  {
    if (value.starts_with("0b") || value.starts_with("0B"))
      value.erase(0, 2);
    if (value.empty())
      throw std::invalid_argument("Constant value must not be empty");

    std::ranges::transform(value, value.begin(), [](const char digit) {
      return digit == 'X' ? 'x' : digit;
    });
    if (!std::ranges::all_of(value, [](const char digit) {
          return digit == '0' || digit == '1' || digit == 'x';
        })) {
      throw std::invalid_argument(
          "Constant value must contain only binary or unknown digits");
    }

    if (value.size() > width)
      value.erase(0, value.size() - width);
    if (value.size() < width) {
      const char extension = value == "x" ? 'x' : '0';
      value.insert(0, width - value.size(), extension);
    }
    return value;
  }

}  // namespace

ConstantComponent::ConstantComponent()
{
  defineProperty("size", 1, [this](const PropertyValue& value) {
    return setSize(std::get<int>(value));
  });
  defineProperty("value", "0", [this](const PropertyValue& value) {
    return setValue(std::get<std::string>(value));
  });
}

ConstantComponent::ConstantComponent(Wire_ptr output, std::string value)
  : ConstantComponent(Bus{std::move(output)}, std::move(value))
{
}

ConstantComponent::ConstantComponent(Bus output, std::string value)
  : ConstantComponent()
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
  if (const auto value = getPropertyValue<std::string>("value"))
    setProperty("value", *value);
  return normalizedSize;
}

std::string ConstantComponent::setValue(std::string value)
{
  const std::size_t width = outputs.empty()
                                ? static_cast<std::size_t>(
                                      getPropertyValue<int>("size").value_or(1))
                                : outputs[0].size();
  return normalizedConstantValue(std::move(value), width);
}

void ConstantComponent::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputs.empty())
    return;

  const auto value = normalizedConstantValue(
      getPropertyValue<std::string>("value").value_or("x"), outputs[0].size());
  for (std::size_t bit = 0; bit < outputs[0].size(); ++bit) {
    const char digit = value[value.size() - bit - 1];
    const auto state = digit == '0'   ? State::LOW
                       : digit == '1' ? State::HIGH
                                      : State::UNKNOWN;
    sim.updateWire(outputs[0][static_cast<unsigned short>(bit)], state, 0,
                   weak_from_this());
  }
}

void ConstantComponent::serializeYosys(
    SILICON::yosys::SerializationContext& context) const
{
  if (outputs.empty())
    throw std::runtime_error("Cannot export a malformed constant component");

  const auto value = normalizedConstantValue(
      getPropertyValue<std::string>("value").value_or("x"), outputs[0].size());
  SILICON::yosys::Json bits = SILICON::yosys::Json::array();
  for (auto digit = value.rbegin(); digit != value.rend(); ++digit)
    bits.push_back(std::string(1, *digit));
  SILICON::yosys::detail::emitUnary(context, "constant", "$pos",
                                    std::move(bits), context.bits(outputs[0]));
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
