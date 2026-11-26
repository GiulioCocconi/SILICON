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

#include "gates.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

#include <core/simulator.hpp>

namespace SILICON::core {

namespace {
  uint64_t gateDelay(const Gate& gate)
  {
    return gate.getPropertyValue<int>("delay").value_or(0);
  }

  bool gateBitwiseEnabled(const Gate& gate)
  {
    return gate.getPropertyValue<bool>("bitwise").value_or(false);
  }

  int gateWidth(const Gate& gate)
  {
    return gate.getPropertyValue<int>("size").value_or(1);
  }

  State busTruthValue(const Bus& bus)
  {
    State result = State::LOW;
    for (const auto& wire : bus)
      result = result || Wire::safeGetCurrentState(wire);
    return result;
  }

  template <typename Reducer, typename Finalizer = std::identity>
  void simulateBitwiseGate(Gate& gate, SILICON::simulation::Simulator& sim,
                           State initialState, Reducer&& reducer,
                           Finalizer&& finalizer = {})
  {
    const auto delay   = gateDelay(gate);
    const auto inputs  = gate.getInputs();
    const auto outputs = gate.getOutputs();

    if (!gateBitwiseEnabled(gate)) {
      State result = initialState;
      for (const auto& input : inputs) {
        if (gateWidth(gate) > 1) {
          for (const auto& wire : input)
            result = reducer(result, Wire::safeGetCurrentState(wire));
        } else {
          result = reducer(result, busTruthValue(input));
        }
      }

      sim.updateWire(outputs[0][0], finalizer(result), delay, gate.weak_from_this());
      return;
    }

    const int width = gateWidth(gate);
    for (int bit = 0; bit < width; ++bit) {
      State result = initialState;
      for (const auto& input : inputs)
        result = reducer(result, Wire::safeGetCurrentState(input[bit]));

      sim.updateWire(outputs[0][bit], finalizer(result), delay, gate.weak_from_this());
    }
  }
}  // namespace

Gate::Gate() : Gate(true) {}

Gate::Gate(const bool enableBitwiseProperties)
{
  initializeProperties(enableBitwiseProperties);
}

Gate::Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, std::move(output), true)
{
}

Gate::Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output,
           const bool enableBitwiseProperties)
  : Gate(enableBitwiseProperties)
{
  if (inputs.empty())
    throw std::invalid_argument("Gate requires at least one input");

  scalarInputCount = inputs.size();
  for (const auto& input : inputs)
    this->inputs.push_back({input});
  this->outputs = {{output}};
}

void Gate::initializeProperties(const bool enableBitwiseProperties)
{
  static const auto delayDefinition =
      propertyDefinition("delay", 5, PropagationDelayDescription, {.minimum = 0});
  defineProperty(delayDefinition);

  if (!enableBitwiseProperties)
    return;

  static const auto bitwiseDefinition = propertyDefinition(
      "bitwise", false,
      "When true, combine corresponding bits across inputs. When false and size is "
      "greater than one, reduce a single input bus to one output bit.");
  defineProperty(bitwiseDefinition);
  static const auto sizeDefinition =
      propertyDefinition("size", 1,
                         "Input word width; bitwise mode produces a result of the same "
                         "width, while reduction mode produces one bit.",
                         {.minimum = 1, .maximum = MaxBusWidth});
  defineProperty(sizeDefinition);

  setPropertyCallback("bitwise", [this](const PropertyValue& value) {
    const bool bitwise = std::get<bool>(value);
    if (!inputs.empty() && !outputs.empty()) {
      const int size = getPropertyValue<int>("size").value_or(1);
      configureIO(size, bitwise);
    }
    return value;
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);

    if (!inputs.empty() && !outputs.empty())
      configureIO(size, getPropertyValue<bool>("bitwise").value_or(false));

    return value;
  });
}

std::size_t Gate::configuredInputCount(int width, bool bitwise) const
{
  return !getPropertyValue<bool>("bitwise") || (!bitwise && width > 1) ? 1
                                                                       : scalarInputCount;
}

unsigned int Gate::configuredOutputWidth(unsigned int width, bool bitwise)
{
  return bitwise ? width : 1;
}

void Gate::configureIO(const int size, const bool bitwise)
{
  auto newInputs = getInputs();
  if (newInputs.size() > 1)
    scalarInputCount = newInputs.size();

  const auto inputCount = configuredInputCount(size, bitwise);
  newInputs.resize(std::min(newInputs.size(), inputCount));
  while (newInputs.size() < inputCount)
    newInputs.emplace_back(1);
  for (auto& input : newInputs)
    input.setSize(static_cast<unsigned short>(size));

  auto newOutputs = getOutputs();
  for (auto& output : newOutputs)
    output.setSize(static_cast<unsigned short>(configuredOutputWidth(size, bitwise)));

  setInputs(newInputs);
  setOutputs(newOutputs);
}

int Gate::setSize(const int width)
{
  if (width < *propertyDeclaration("size").constraints.minimum)
    return static_cast<int>(outputs.empty() ? 0 : outputs[0].size());

  auto newInputs = getInputs();
  for (auto& input : newInputs)
    input.setSize(static_cast<unsigned short>(width));
  setInputs(newInputs);

  auto newOutputs = getOutputs();
  for (auto& output : newOutputs)
    output.setSize(static_cast<unsigned short>(width));
  setOutputs(newOutputs);

  return width;
}

AndGate::AndGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, std::move(output))
{
  if (inputs.size() < 2)
    throw std::invalid_argument("AndGate requires at least 2 inputs");
}

void AndGate::simulate(SILICON::simulation::Simulator& sim)
{
  simulateBitwiseGate(*this, sim, State::HIGH,
                      [](const State lhs, const State rhs) { return lhs && rhs; });
}

OrGate::OrGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, output)
{
  if (inputs.size() < 2)
    throw std::invalid_argument("OrGate requires at least 2 inputs");
}

void OrGate::simulate(SILICON::simulation::Simulator& sim)
{
  simulateBitwiseGate(*this, sim, State::LOW,
                      [](const State lhs, const State rhs) { return lhs || rhs; });
}

NotGate::NotGate() : Gate(false)
{
  initializeNotProperties();
}

NotGate::NotGate(Wire_ptr input, Wire_ptr output) : Gate({input}, output, false)
{
  initializeNotProperties();
}

void NotGate::initializeNotProperties()
{
  static const auto delayDefinition =
      propertyDefinition("delay", 2, PropagationDelayDescription, {.minimum = 0});
  defineProperty(delayDefinition);
  static const auto sizeDefinition = propertyDefinition(
      "size", 1,
      "Number of input bits inverted independently into an equally wide output.",
      {.minimum = 1, .maximum = MaxBusWidth});
  defineProperty(sizeDefinition);
  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
    if (!inputs.empty() && !outputs.empty())
      setSize(size);
    return value;
  });
}

void NotGate::simulate(SILICON::simulation::Simulator& sim)
{
  const auto delay = getPropertyValue<int>("delay").value_or(0);
  if (outputs[0].size() == 1) {
    sim.updateWire(outputs[0][0], !busTruthValue(inputs[0]), delay, weak_from_this());
    return;
  }

  if (inputs[0].size() != outputs[0].size())
    throw std::logic_error("NOT gate input and output widths must match");

  for (std::size_t bit = 0; bit < outputs[0].size(); ++bit) {
    sim.updateWire(
        outputs[0][static_cast<unsigned short>(bit)],
        !Wire::safeGetCurrentState(inputs[0][static_cast<unsigned short>(bit)]), delay,
        weak_from_this());
  }
}

NandGate::NandGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, output)
{
  if (inputs.size() < 2)
    throw std::invalid_argument("NandGate requires at least 2 inputs");
  setProperty("delay", 6);
}

void NandGate::simulate(SILICON::simulation::Simulator& sim)
{
  simulateBitwiseGate(
      *this, sim, State::HIGH,
      [](const State lhs, const State rhs) { return lhs && rhs; },
      [](const State state) { return !state; });
}

NorGate::NorGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, output)
{
  if (inputs.size() < 2)
    throw std::invalid_argument("NorGate requires at least 2 inputs");
  setProperty("delay", 6);
}

void NorGate::simulate(SILICON::simulation::Simulator& sim)
{
  simulateBitwiseGate(
      *this, sim, State::LOW, [](const State lhs, const State rhs) { return lhs || rhs; },
      [](const State state) { return !state; });
}

XorGate::XorGate(const std::array<Wire_ptr, 2>& inputs, Wire_ptr output)
  : Gate({inputs[0], inputs[1]}, output)
{
  setProperty("delay", 7);
}

void XorGate::simulate(SILICON::simulation::Simulator& sim)
{
  simulateBitwiseGate(*this, sim, State::LOW,
                      [](const State lhs, const State rhs) { return lhs ^ rhs; });
}

std::vector<PortDescription> Gate::portDescriptions() const
{
  const auto width = static_cast<unsigned int>(getPropertyValue<int>("size").value_or(1));
  const auto bitwise = getPropertyValue<bool>("bitwise");
  const auto count = inputs.empty() ? configuredInputCount(width, bitwise.value_or(true))
                                    : inputs.size();
  std::vector<PortDescription> result;
  for (unsigned int i = 0; i < count; ++i)
    result.push_back({count == 1   ? "i"
                      : count == 2 ? (i == 0 ? "a" : "b")
                                   : std::format("i{}", i),
                      PortDirection::Input, i, width,
                      "Operand bits; bitwise mode combines corresponding bit positions, "
                      "while reduction mode folds all bits into one result."});
  result.push_back({"o", PortDirection::Output, 0,
                    configuredOutputWidth(width, bitwise.value_or(true)),
                    "Result of the gate operation."});
  return result;
}

}  // namespace SILICON::core
