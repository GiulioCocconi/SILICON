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

#include <stdexcept>

HalfAdder::HalfAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr sum, Wire_ptr cout)
  : Component({{inputs[0]}, {inputs[1]}}, {{sum}, {cout}}, "HalfAdder")
{
  /* PIN MAP:
     a    = inputs [0][0];
     b    = inputs [1][0];
     sum  = outputs[0][0];
     cout = outputs[1][0];
  */

  this->setAction([this] {
    State cout =
        this->inputs[0][0]->getCurrentState() && this->inputs[1][0]->getCurrentState();

    State sum =
        this->inputs[0][0]->getCurrentState() ^ this->inputs[1][0]->getCurrentState();

    this->outputs[0][0]->setCurrentState(sum, weak_from_this());
    this->outputs[1][0]->setCurrentState(cout, weak_from_this());
  });
}

FullAdder::FullAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr cin, Wire_ptr sum,
                     Wire_ptr cout)
  : Component({{inputs[0]}, {inputs[1]}, {cin}}, {{sum}, {cout}}, "FullAdder")
{
  /* PIN MAP:
     a    = inputs [0][0];
     b    = inputs [1][0];
     cin  = inputs [2][0];
     sum  = outputs[0][0];
     cout = outputs[1][0]; */

  this->setAction([this] {
    auto partialSum1   = std::make_shared<Wire>();
    auto partialCarry1 = std::make_shared<Wire>();
    auto partialCarry2 = std::make_shared<Wire>();

    auto h1 = std::make_shared<HalfAdder>(
        std::array<Wire_ptr, 2>{this->inputs[0][0], this->inputs[1][0]}, partialSum1,
        partialCarry1);

    auto h2 = std::make_shared<HalfAdder>(
        std::array<Wire_ptr, 2>{partialSum1, this->inputs[2][0]}, this->outputs[0][0],
        partialCarry2);

    auto og = std::make_shared<OrGate>(
        std::vector<Wire_ptr>{partialCarry1, partialCarry2}, this->outputs[1][0]);
  });
}

AdderNBits::AdderNBits(std::array<Bus, 2> inputs, Bus sum, Wire_ptr cout)
  : Component({inputs[0], inputs[1]}, {sum, {cout}}, "AdderNBits")
{
  /* PIN MAP:
     a    = inputs [0][0:N];
     b    = inputs [1][0:N];
     sum  = outputs[0][0:N];
     cout = outputs[1][ 0 ]; */

  static_assert(inputs.size() == 2);
  if (inputs[0].size() != sum.size())
    throw std::invalid_argument("AdderNBits: input bus width must match sum bus width");

  this->setAction([this] {
    int a = this->inputs[0].getCurrentValue();
    int b = this->inputs[1].getCurrentValue();

    int overflow = this->outputs[0].setCurrentValue(a + b, weak_from_this());

    this->outputs[1].setCurrentValue(overflow, weak_from_this());
  });
}
