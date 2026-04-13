/*
  Copyright (C) 2025 Giulio Cocconi

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
#include <stdexcept>

HalfAdder::HalfAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr sum, Wire_ptr cout)
  : Component({{inputs[0]}, {inputs[1]}}, {{sum}, {cout}})
{

 /* PIN MAP:
     a    = inputs [0][0];
     b    = inputs [1][0];
     sum  = outputs[0][0];
     cout = outputs[1][0];
  */

  defineProperty("delay", 2);
}

void HalfAdder::simulate(Simulator& sim)
{
  State a = Wire::safeGetCurrentState(this->inputs[0][0]);
  State b = Wire::safeGetCurrentState(this->inputs[1][0]);

  sim.updateWire(this->outputs[0][0], a ^ b, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
  sim.updateWire(this->outputs[1][0], a && b, getPropertyValue<int>("delay").value_or(0),
                 weak_from_this());
}

FullAdder::FullAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr cin, Wire_ptr sum,
                     Wire_ptr cout)
  : Component({{inputs[0]}, {inputs[1]}, {cin}}, {{sum}, {cout}})
{

  /* PIN MAP:
     a    = inputs [0][0];
     b    = inputs [1][0];
     cin  = inputs [2][0];
     sum  = outputs[0][0];
     cout = outputs[1][0]; */

  defineProperty("delay", 3);
}

void FullAdder::simulate(Simulator& sim)
{
  State a   = Wire::safeGetCurrentState(this->inputs[0][0]);
  State b   = Wire::safeGetCurrentState(this->inputs[1][0]);
  State cin = Wire::safeGetCurrentState(this->inputs[2][0]);

  State sum  = a ^ b ^ cin;
  State cout = (a && b) || (cin && (a ^ b));

  int delay = getPropertyValue<int>("delay").value_or(0);
  sim.updateWire(this->outputs[0][0], sum, delay, weak_from_this());
  sim.updateWire(this->outputs[1][0], cout, delay, weak_from_this());
}

AdderNBits::AdderNBits(std::array<Bus, 2> inputs, Bus sum, Wire_ptr cout)
  : Component({inputs[0], inputs[1]}, {sum, {cout}})
{

  /* PIN MAP:
     a    = inputs [0][0:N];
     b    = inputs [1][0:N];
     sum  = outputs[0][0:N];
     cout = outputs[1][ 0 ]; */

  static_assert(inputs.size() == 2);
  if (inputs[0].size() != sum.size())
    throw std::invalid_argument("AdderNBits: input bus width must match sum bus width");

  defineProperty("delay", 5);
}

void AdderNBits::simulate(Simulator& sim)
{
  int delay = getPropertyValue<int>("delay").value_or(0);

  try {
    if (this->inputs[0].isInErrorState() || this->inputs[1].isInErrorState()) {
      throw std::logic_error("Input bus in error state");
    }

    unsigned int a   = this->inputs[0].getCurrentValue();
    unsigned int b   = this->inputs[1].getCurrentValue();
    unsigned int sum = a + b;

    for (unsigned short i = 0; i < this->outputs[0].size(); i++) {
      if (this->outputs[0][i]) {
        State s = (sum >> i) & 1 ? State::HIGH : State::LOW;
        sim.updateWire(this->outputs[0][i], s, delay, weak_from_this());
      }
    }

    State overflow = (sum >= (1u << this->outputs[0].size())) ? State::HIGH : State::LOW;
    sim.updateWire(this->outputs[1][0], overflow, delay, weak_from_this());

  } catch (const std::logic_error&) {
    for (auto& w : this->outputs[0]) {
      if (w)
        sim.updateWire(w, State::ERROR, delay, weak_from_this());
    }
    if (this->outputs[1][0]) {
      sim.updateWire(this->outputs[1][0], State::ERROR, delay, weak_from_this());
    }
  }
}
