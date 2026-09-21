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

#include <core/wireUtils.hpp>
#include <core/simulator.hpp>

#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace SILICON::extra {
using namespace SILICON::core;

Extender::Extender()
{
  defineProperty("inSize", 4);
  defineProperty("outSize", 8);
  defineStringListProperty("mode", std::string(UnsignedMode),
                           {std::string(UnsignedMode), std::string(SignedMode)});

  setPropertyCallback("inSize", [this](const PropertyValue& value) {
    const int width = std::get<int>(requireValidSize("Extender inSize", value));
    if (!inputs.empty())
      setInputSize(width);
    return value;
  });
  setPropertyCallback("outSize", [this](const PropertyValue& value) {
    const int width = std::get<int>(requireValidSize("Extender outSize", value));
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
  value = wireUtils::normalizeBusValue(value, outputs[0].size(), extension);

  sim.updateBus(outputs[0], value, 0, weak_from_this());
}
Complementer::Complementer()
{
  defineProperty("delay", 0);
  defineProperty("size", 4);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    return requireNonNegative("Complementer delay", value);
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(requireValidSize("Complementer size", value));

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
    return requireNonNegative("HalfAdder delay", value);
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
    return requireNonNegative("FullAdder delay", value);
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
    return requireNonNegative("AdderNBits delay", value);
  });
  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(requireValidSize("AdderNBits size", value));

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

  auto result = inputs[busIndex(Inputs::A)].getCurrentValue()
                + inputs[busIndex(Inputs::B)].getCurrentValue();

  const State carry = result[sumWidth];
  result.resize(sumWidth);

  sim.updateBus(outputs[busIndex(Outputs::Sum)], result, propagationDelay,
                weak_from_this());

  sim.updateWire(outputWire(Outputs::Cout), carry, propagationDelay, weak_from_this());
}

Comparator::Comparator()
{
  defineProperty("delay", 0, [this](const PropertyValue& value) {
    const auto validated   = requireNonNegative("Comparator delay", value);
    this->propagationDelay = static_cast<uint64_t>(std::get<int>(value));
    return validated;
  });

  defineProperty("size", 4, [this](const PropertyValue& value) {
    const int size = std::get<int>(requireValidSize("Comparator size", value));

    if (!inputs.empty() && !outputs.empty())
      setSize(size);

    return value;
  });

  defineStringListProperty("mode", "==", {"==", "<", "<=", ">", ">="});
  defineProperty("signed", false);
}

Comparator::Comparator(std::array<Bus, 2> inputBuses, Wire_ptr output) : Comparator()
{
  if (inputBuses[0].size() == 0 || inputBuses[0].size() != inputBuses[1].size())
    throw std::invalid_argument(
        "Comparator: input buses must have the same non-zero width");

  this->inputs  = {std::move(inputBuses[0]), std::move(inputBuses[1])};
  this->outputs = {{std::move(output)}};

  const int size = static_cast<int>(inputs[0].size());
  setProperty("size", size);
}

void Comparator::simulate(SILICON::simulation::Simulator& sim)
{
  const BusValue a = inputs[0].getCurrentValue();
  const BusValue b = inputs[1].getCurrentValue();

  const auto setOutput = [this, &sim](const State s) {
    sim.updateWire(outputs[0][0], s, propagationDelay, weak_from_this());
  };

  const auto isError = [](const BusValue& value) {
    return std::ranges::any_of(value,
                               [](const State state) { return state == State::ERROR; });
  };

  if (isError(a) || isError(b)) {
    setOutput(State::ERROR);
    return;
  }

  const auto isSigned = getPropertyValue<bool>("signed").value_or(false);
  const auto compareResult =
      compare(a, b, static_cast<Signedness>(isSigned));

  if (compareResult == std::partial_ordering::unordered) {
    setOutput(State::UNKNOWN);
    return;
  }

  const auto checkForOrdering = [compareResult](std::string_view mode) {
    const auto modeWantEquivalent = {"==", "<=", ">="};
    const auto modeWantLess       = {"<", "<="};
    const auto modeWantGreat      = {">", ">="};

    // clang-format off
    if ((std::ranges::contains(modeWantEquivalent, mode) && compareResult == std::partial_ordering::equivalent) ||
      (std::ranges::contains(modeWantLess, mode) && compareResult == std::partial_ordering::less) ||
      (std::ranges::contains(modeWantGreat, mode) && compareResult == std::partial_ordering::greater))
      return State::HIGH;

    return State::LOW;
  };
  // clang-format on

  const auto mode = getPropertyValue<std::string>("mode").value_or("==");
  setOutput(checkForOrdering(mode));
}

int Comparator::setSize(const int width)
{
  if (width < 1)
    return getPropertyValue<int>("size").value_or(4);

  auto newInputs = inputs;
  if (inputs.size() < 2)
    newInputs.resize(2);

  newInputs[0].setSize(width);
  newInputs[1].setSize(width);

  this->inputs = newInputs;

  return width;
}

}  // namespace SILICON::extra
