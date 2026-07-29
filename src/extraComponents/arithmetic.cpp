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

#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace SILICON::extra {
using namespace SILICON::core;

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

  State      carry            = State::LOW;
  const auto sumWidth         = outputBusSize(Outputs::Sum);
  const int  propagationDelay = getPropertyValue<int>("delay").value_or(0);

  for (unsigned short bit = 0; bit < sumWidth; ++bit) {
    const State a = inputState(Inputs::A, bit);
    const State b = inputState(Inputs::B, bit);

    sim.updateWire(outputWire(Outputs::Sum, bit), a ^ b ^ carry, propagationDelay,
                   weak_from_this());
    carry = (a && b) || (carry && (a ^ b));
  }

  sim.updateWire(outputWire(Outputs::Cout), carry, propagationDelay, weak_from_this());
}

}  // namespace SILICON::extra
