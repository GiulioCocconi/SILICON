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

#include "tests.hpp"
#include <core/circuit.hpp>
#include <core/simulator.hpp>
#include <extraComponents/arithmetic.hpp>
#include <vector>

TEST(ArithmeticTest, HalfAdderCase)
{
  auto a    = std::make_shared<Wire>(State::LOW);
  auto b    = std::make_shared<Wire>(State::LOW);
  auto sum  = std::make_shared<Wire>();
  auto cout = std::make_shared<Wire>();

  auto      ha   = std::make_shared<HalfAdder>(std::array<Wire_ptr, 2>{a, b}, sum, cout);
  auto      circ = std::make_shared<Circuit>(Component_set{ha});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(sum->getCurrentState(), State::LOW);
  EXPECT_EQ(cout->getCurrentState(), State::LOW);

  sim.setBus(Bus{b}, 1);
  sim.run(20);
  EXPECT_EQ(sum->getCurrentState(), State::HIGH);
  EXPECT_EQ(cout->getCurrentState(), State::LOW);

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(sum->getCurrentState(), State::LOW);
  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
}

TEST(ArithmeticTest, AdderNBitsFromComponents)
{
  auto a   = Bus(4);
  auto b   = Bus(4);
  auto sum = Bus(4);

  auto partialCarryWires = Bus(5);

  Component_set comps;

  Wire_ptr cout = partialCarryWires[4];

  for (int i = 0; i < 5; i++) {
    partialCarryWires[i]->forceSetCurrentState(State::LOW);
  }

  a.forceSetCurrentValue(0);
  b.forceSetCurrentValue(0);
  sum.forceSetCurrentValue(0);

  for (int i = 0; i < 4; i++) {
    auto fa = std::make_shared<FullAdder>(std::array<Wire_ptr, 2>{a[i], b[i]},
                                          partialCarryWires[i], sum[i],
                                          partialCarryWires[i + 1]);
    comps.insert(fa);
  }

  auto      circ = std::make_shared<Circuit>(comps);
  Simulator sim(circ);

  sim.setBus(a, 0);
  sim.setBus(b, 0);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0);

  sim.setBus(a, 0b1100);
  sim.setBus(b, 0b0011);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0b1111);

  sim.setBus(b, 0b1100);
  sim.setBus(a, 0b0011);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0b1111);

  sim.setBus(a, 0b1111);
  sim.setBus(b, 0b0001);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
  EXPECT_EQ(sum.getCurrentValue(), 0);
}

TEST(ArithmeticTest, AdderNBitsAtomic)
{
  auto a    = Bus(4);
  auto b    = Bus(4);
  auto sum  = Bus(4);
  auto cout = std::make_shared<Wire>();

  auto      adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{a, b}, sum, cout);
  auto      circ  = std::make_shared<Circuit>(Component_set{adder});
  Simulator sim(circ);

  sim.setBus(a, 0);
  sim.setBus(b, 0);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0);

  sim.setBus(a, 0b1100);
  sim.setBus(b, 0b0011);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0b1111);

  sim.setBus(b, 0b1100);
  sim.setBus(a, 0b0011);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), 0b1111);

  sim.setBus(a, 0b1111);
  sim.setBus(b, 0b0001);
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
  EXPECT_EQ(sum.getCurrentValue(), 0);
}
