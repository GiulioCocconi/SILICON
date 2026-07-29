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

#include "gates.hpp"

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

template <typename Reducer, typename Finalizer = std::identity>
void simulateBitwiseGate(Gate& gate, SILICON::simulation::Simulator& sim, State initialState,
                         Reducer&& reducer, Finalizer&& finalizer = {})
{
  const auto delay   = gateDelay(gate);
  const auto inputs  = gate.getInputs();
  const auto outputs = gate.getOutputs();

  if (!gateBitwiseEnabled(gate)) {
    State result = initialState;
    for (const auto& input : inputs)
      result = reducer(result, Wire::safeGetCurrentState(input[0]));

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

  for (const auto& input : inputs)
    this->inputs.push_back({input});
  this->outputs = {{output}};
}

void Gate::initializeProperties(const bool enableBitwiseProperties)
{
  defineProperty("delay", 5);
  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) >= 0)
      return value;

    throw std::invalid_argument("Delays must be non-negative!");
  });

  if (!enableBitwiseProperties)
    return;

  defineProperty("bitwise", false);
  defineProperty("size", 1);

  setPropertyCallback("bitwise", [this](const PropertyValue& value) {
    const bool bitwise = std::get<bool>(value);
    if (!inputs.empty() && !outputs.empty()) {
      const int size = getPropertyValue<int>("size").value_or(1);
      setSize(bitwise ? size : 1);
    }
    return value;
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
    if (size < 1)
      throw std::invalid_argument("Gate size must be at least 1");

    if (getPropertyValue<bool>("bitwise").value_or(false) && !inputs.empty()
        && !outputs.empty()) {
      setSize(size);
    }

    return value;
  });
}

int Gate::setSize(const int width)
{
  if (width < 1)
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

NotGate::NotGate(Wire_ptr input, Wire_ptr output) : Gate({input}, output, false)
{
  setProperty("delay", 2);
}

void NotGate::simulate(SILICON::simulation::Simulator& sim)
{
  State s = !Wire::safeGetCurrentState(inputs[0][0]);
  sim.updateWire(this->outputs[0][0], s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
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
  const auto delay = gateDelay(*this);
  if (!gateBitwiseEnabled(*this)) {
    State s = Wire::safeGetCurrentState(this->inputs[0][0])
              ^ Wire::safeGetCurrentState(this->inputs[1][0]);
    sim.updateWire(this->outputs[0][0], s, delay, weak_from_this());
    return;
  }

  for (int bit = 0; bit < gateWidth(*this); ++bit) {
    State s = Wire::safeGetCurrentState(this->inputs[0][bit])
              ^ Wire::safeGetCurrentState(this->inputs[1][bit]);
    sim.updateWire(this->outputs[0][bit], s, delay, weak_from_this());
  }
}

}  // namespace SILICON::core
