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
#include <extraComponents/utils.hpp>

using namespace SILICON::core;
using namespace SILICON::extra;
using namespace SILICON::simulation;
using namespace SILICON::waveform;

TEST(UtilsTest, WireMergerCase)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);

  auto bus = Bus(2);

  auto      wm   = std::make_shared<WireMerger>(std::vector<Bus>{{a}, {b}}, bus);
  auto      circ = std::make_shared<Circuit>(Component_set{wm});
  Simulator sim(circ);
  sim.run(20);

  sim.setBus(Bus{a}, valueFor(Bus{a}, 1));
  sim.run(20);

  EXPECT_EQ(bus.getCurrentValue(), valueFor(bus, 3));
}

TEST(UtilsTest, WireSplitterCase)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();

  auto bus = Bus(2);

  auto      ws   = std::make_shared<WireSplitter>(bus, std::vector<Bus>{{a}, {b}});
  auto      circ = std::make_shared<Circuit>(Component_set{ws});
  Simulator sim(circ);

  sim.setBus(bus, valueFor(bus, 2));  // 0b10 sets 'b' to HIGH and 'a' to LOW
  sim.run(20);

  EXPECT_EQ(a->getCurrentState(), State::LOW);
  EXPECT_EQ(b->getCurrentState(), State::HIGH);
}
