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
#include <core/simulator.hpp>
#include <stdexcept>

Gate::Gate()
{
  defineProperty("delay", 5);
  setPropertyCallback("delay", [](const PropertyValue& value) {
    if (std::get<int>(value) >= 0)
      return value;

    throw std::invalid_argument("Delays must be non-negative!");
  });
}

Gate::Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output) : Gate()
{
  if (inputs.empty())
    throw std::invalid_argument("Gate requires at least one input");

  for (const auto& input : inputs)
    this->inputs.push_back({input});
  this->outputs = {{output}};
}

AndGate::AndGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, std::move(output))
{
  if (inputs.size() < 2)
    throw std::invalid_argument("AndGate requires at least 2 inputs");
}

void AndGate::simulate(Simulator& sim)
{
  State s = State::HIGH;
  for (const auto& input : this->inputs)
    s = s && Wire::safeGetCurrentState(input[0]);

  sim.updateWire(this->outputs[0][0], s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}

OrGate::OrGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, output)
{
  if (inputs.size() < 2)
    throw std::invalid_argument("OrGate requires at least 2 inputs");
}

void OrGate::simulate(Simulator& sim)
{
  State s = State::LOW;
  for (const auto& input : this->inputs)
    s = s || Wire::safeGetCurrentState(input[0]);

  sim.updateWire(this->outputs[0][0], s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}

NotGate::NotGate(Wire_ptr input, Wire_ptr output) : Gate({input}, output)
{
  setProperty("delay", 2);
}

void NotGate::simulate(Simulator& sim)
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

void NandGate::simulate(Simulator& sim)
{
  State s = State::HIGH;
  for (const auto& input : this->inputs)
    s = s && Wire::safeGetCurrentState(input[0]);

  sim.updateWire(this->outputs[0][0], !s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}

NorGate::NorGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output)
  : Gate(inputs, output)
{
  if (inputs.size() < 2)
    throw std::invalid_argument("NorGate requires at least 2 inputs");
  setProperty("delay", 6);
}

void NorGate::simulate(Simulator& sim)
{
  State s = State::LOW;
  for (const auto& input : this->inputs)
    s = s || Wire::safeGetCurrentState(input[0]);

  sim.updateWire(this->outputs[0][0], !s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}

XorGate::XorGate(const std::array<Wire_ptr, 2>& inputs, Wire_ptr output)
  : Gate({inputs[0], inputs[1]}, output)
{
  setProperty("delay", 7);
}

void XorGate::simulate(Simulator& sim)
{
  State s = Wire::safeGetCurrentState(this->inputs[0][0])
            ^ Wire::safeGetCurrentState(this->inputs[1][0]);
  sim.updateWire(this->outputs[0][0], s, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}
