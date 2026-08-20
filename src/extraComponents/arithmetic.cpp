/*
  Copyright (C) 2026 Giulio Cocconi

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

#include "arithmetic.hpp"

#include <core/simulator.hpp>

#include <format>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace SILICON::extra {
using namespace SILICON::core;

namespace {

[[nodiscard]] int validateExtenderWidth(const PropertyValue&   value,
                                        const std::string_view property)
{
  const int width = std::get<int>(value);
  if (width < 1)
    throw std::invalid_argument(std::format("Extender {} must be at least 1", property));
  if (width > std::numeric_limits<unsigned short>::max())
    throw std::invalid_argument(std::format("Extender {} is too large", property));
  return width;
}

}  // namespace

Extender::Extender()
{
  defineProperty("inSize", 4);
  defineProperty("outSize", 8);
  defineStringListProperty("mode", std::string(UnsignedMode),
                           {std::string(UnsignedMode), std::string(SignedMode)});

  setPropertyCallback("inSize", [this](const PropertyValue& value) {
    const int width = validateExtenderWidth(value, "inSize");
    if (!inputs.empty())
      setInputSize(width);
    return value;
  });
  setPropertyCallback("outSize", [this](const PropertyValue& value) {
    const int width = validateExtenderWidth(value, "outSize");
    if (!outputs.empty())
      setOutputSize(width);
    return value;
  });
}

Extender::Extender(Bus in, Bus out, std::string mode) : Extender()
{
  if (in.size() == 0 || out.size() == 0)
    throw std::invalid_argument("Extender buses must not be empty");

  setProperty("inSize", static_cast<int>(in.size()));
  setProperty("outSize", static_cast<int>(out.size()));
  setProperty("mode", mode);
  inputs  = {std::move(in)};
  outputs = {std::move(out)};
}

int Extender::setInputSize(const int width)
{
  if (width < 1 || width > std::numeric_limits<unsigned short>::max())
    return getPropertyValue<int>("inSize").value_or(4);

  auto newInputs = getInputs();
  if (newInputs.empty())
    newInputs.resize(1);
  newInputs[0].setSize(static_cast<unsigned short>(width));
  setInputs(newInputs);
  return width;
}

int Extender::setOutputSize(const int width)
{
  if (width < 1 || width > std::numeric_limits<unsigned short>::max())
    return getPropertyValue<int>("outSize").value_or(8);

  auto newOutputs = getOutputs();
  if (newOutputs.empty())
    newOutputs.resize(1);
  newOutputs[0].setSize(static_cast<unsigned short>(width));
  setOutputs(newOutputs);
  return width;
}

void Extender::simulate(SILICON::simulation::Simulator& sim)
{
  if (inputs.size() != 1 || outputs.size() != 1 || inputs[0].size() == 0)
    return;

  auto value = inputs[0].getCurrentValue();

  const bool signedMode =
      getPropertyValue<std::string>("mode").value_or(std::string(UnsignedMode))
      == SignedMode;

  const State extension = signedMode ? value.back() : State::LOW;
  value.resize(outputs[0].size(), extension);
  sim.updateBus(outputs[0], value, 0, weak_from_this());
}
Complementer::Complementer()
{
  defineProperty("delay", 0);
  defineProperty("size", 4);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) < 0)
      throw std::invalid_argument("Complementer delay must be non-negative");

    return value;
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
    if (size < 1)
      throw std::invalid_argument("Complementer size must be at least 1");
    if (size > std::numeric_limits<unsigned short>::max())
      throw std::invalid_argument("Complementer size is too large");

    if (!inputs.empty() && !outputs.empty())
      setSize(size);

    return value;
  });
}

Complementer::Complementer(Bus in, Bus out) : Complementer()
{
  if (in.size() == 0 || in.size() != out.size())
    throw std::invalid_argument(
        "Complementer: input bus width must match non-empty output bus width");

  setProperty("size", static_cast<int>(in.size()));
  inputs  = {std::move(in)};
  outputs = {std::move(out)};
}

int Complementer::setSize(const int width)
{
  if (width < 1 || width > std::numeric_limits<unsigned short>::max())
    return getPropertyValue<int>("size").value_or(4);

  auto newInputs = getInputs();
  if (newInputs.empty())
    newInputs.resize(1);
  newInputs[0].setSize(static_cast<unsigned short>(width));
  setInputs(newInputs);

  auto newOutputs = getOutputs();
  if (newOutputs.empty())
    newOutputs.resize(1);
  newOutputs[0].setSize(static_cast<unsigned short>(width));
  setOutputs(newOutputs);

  return width;
}

void Complementer::simulate(SILICON::simulation::Simulator& sim)
{
  if (inputs.size() != 1 || outputs.size() != 1 || inputs[0].size() != outputs[0].size())
    return;

  const int  propagationDelay = getPropertyValue<int>("delay").value_or(0);
  const auto complement       = twosComplement(inputs[0].getCurrentValue());
  sim.updateBus(outputs[0], complement, propagationDelay, weak_from_this());
}

HalfAdder::HalfAdder()
{
  defineProperty("delay", 2);
  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) < 0)
      throw std::invalid_argument("HalfAdder delay must be non-negative");

    return value;
  });
}

HalfAdder::HalfAdder(std::array<Wire_ptr, 2> inputWires, Wire_ptr sum, Wire_ptr cout)
  : HalfAdder()
{
  inputs  = {{std::move(inputWires[busIndex(Inputs::A)])},
             {std::move(inputWires[busIndex(Inputs::B)])}};
  outputs = {{std::move(sum)}, {std::move(cout)}};
}

void HalfAdder::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBusSize(Outputs::Sum) == 0 || outputBusSize(Outputs::Cout) == 0)
    return;

  const State a                = inputState(Inputs::A);
  const State b                = inputState(Inputs::B);
  const int   propagationDelay = getPropertyValue<int>("delay").value_or(0);

  sim.updateWire(outputWire(Outputs::Sum), a ^ b, propagationDelay, weak_from_this());
  sim.updateWire(outputWire(Outputs::Cout), a && b, propagationDelay, weak_from_this());
}

FullAdder::FullAdder()
{
  defineProperty("delay", 3);
  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) < 0)
      throw std::invalid_argument("FullAdder delay must be non-negative");

    return value;
  });
}

FullAdder::FullAdder(std::array<Wire_ptr, 2> inputWires, Wire_ptr cin, Wire_ptr sum,
                     Wire_ptr cout)
  : FullAdder()
{
  inputs  = {{std::move(inputWires[busIndex(Inputs::A)])},
             {std::move(inputWires[busIndex(Inputs::B)])},
             {std::move(cin)}};
  outputs = {{std::move(sum)}, {std::move(cout)}};
}

void FullAdder::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBusSize(Outputs::Sum) == 0 || outputBusSize(Outputs::Cout) == 0)
    return;

  const State a                = inputState(Inputs::A);
  const State b                = inputState(Inputs::B);
  const State cin              = inputState(Inputs::Cin);
  const State sum              = a ^ b ^ cin;
  const State cout             = (a && b) || (cin && (a ^ b));
  const int   propagationDelay = getPropertyValue<int>("delay").value_or(0);

  sim.updateWire(outputWire(Outputs::Sum), sum, propagationDelay, weak_from_this());
  sim.updateWire(outputWire(Outputs::Cout), cout, propagationDelay, weak_from_this());
}

AdderNBits::AdderNBits()
{
  defineProperty("delay", 5);
  defineProperty("size", 4);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) < 0)
      throw std::invalid_argument("AdderNBits delay must be non-negative");

    return value;
  });
  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
    if (size < 1)
      throw std::invalid_argument("AdderNBits size must be at least 1");
    if (size > std::numeric_limits<unsigned short>::max())
      throw std::invalid_argument("AdderNBits size is too large");

    if (!inputs.empty() && !outputs.empty())
      setSize(size);

    return value;
  });
}

AdderNBits::AdderNBits(std::array<Bus, 2> inputBuses, Bus sum, Wire_ptr cout)
  : AdderNBits()
{
  if (inputBuses[busIndex(Inputs::A)].size() != sum.size()
      || inputBuses[busIndex(Inputs::B)].size() != sum.size()) {
    throw std::invalid_argument("AdderNBits: input bus width must match sum bus width");
  }

  inputs  = {inputBuses[busIndex(Inputs::A)], inputBuses[busIndex(Inputs::B)]};
  outputs = {sum, {std::move(cout)}};
  setProperty("size", static_cast<int>(sum.size()));
}

int AdderNBits::setSize(const int width)
{
  if (width < 1)
    return getPropertyValue<int>("size").value_or(4);

  auto newInputs = getInputs();
  if (newInputs.size() < 2)
    newInputs.resize(2);
  newInputs[busIndex(Inputs::A)].setSize(static_cast<unsigned short>(width));
  newInputs[busIndex(Inputs::B)].setSize(static_cast<unsigned short>(width));
  setInputs(newInputs);

  auto newOutputs = getOutputs();
  if (newOutputs.size() < 2)
    newOutputs.resize(2);
  newOutputs[busIndex(Outputs::Sum)].setSize(static_cast<unsigned short>(width));
  if (newOutputs[busIndex(Outputs::Cout)].size() != 1)
    newOutputs[busIndex(Outputs::Cout)].setSize(1);
  setOutputs(newOutputs);

  return width;
}

void AdderNBits::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBusSize(Outputs::Sum) == 0 || outputBusSize(Outputs::Cout) == 0)
    return;

  const auto propagationDelay = getPropertyValue<int>("delay").value_or(0);
  const auto sumWidth         = outputBusSize(Outputs::Sum);

  auto result =
      inputs[busIndex(Inputs::A)].getCurrentValue()
      + inputs[busIndex(Inputs::B)].getCurrentValue();

  const State carry = result[sumWidth];
  result.resize(sumWidth);

  sim.updateBus(outputs[busIndex(Outputs::Sum)], result, propagationDelay,
                weak_from_this());

  sim.updateWire(outputWire(Outputs::Cout), carry, propagationDelay,
                 weak_from_this());
}
}  // namespace SILICON::extra
