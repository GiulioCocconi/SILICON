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

TEST(UtilsTest, WireMergerEditorInputsAcceptSingleBitConnections)
{
  auto merger = std::make_shared<WireMerger>(std::vector<Bus>{}, Bus());

  for (const int size : {2, 4}) {
    merger->setProperty("size", size);
    merger->clearWires();
    ASSERT_EQ(merger->inputBuses().size(), size);
    for (int bit = 0; bit < size; ++bit) {
      EXPECT_EQ(merger->inputBuses()[bit].size(), 1);
      EXPECT_NO_THROW(merger->setInput(bit, Bus(1), true));
      EXPECT_THROW(merger->setInput(bit, Bus(2), true), std::runtime_error);
    }
    EXPECT_EQ(merger->outputBuses()[0].size(), size);
  }
}

TEST(UtilsTest, WireMergerInitializesEmptyInputWidths)
{
  WireMerger merger(std::vector<Bus>(2), Bus(2));

  for (unsigned int bit = 0; bit < 2; ++bit) {
    EXPECT_EQ(merger.inputBuses()[bit].size(), 1);
    EXPECT_NO_THROW(merger.setInput(bit, Bus(1), true));
  }
}

TEST(UtilsTest, WireMergerDisconnectedInputsRemainUnknown)
{
  for (const bool clearWires : {false, true}) {
    auto merger = std::make_shared<WireMerger>(std::vector<Bus>{}, Bus());
    if (clearWires)
      merger->clearWires();

    const Bus input{std::make_shared<Wire>(State::ERROR)};
    merger->setInput(1, input, true);
    const Bus output(2);
    merger->setOutput(0, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{merger});
    Simulator sim(circuit);
    sim.run(20);

    EXPECT_EQ(output[0]->getCurrentState(), State::UNKNOWN);
    EXPECT_EQ(output[1]->getCurrentState(), State::ERROR);
  }
}

TEST(UtilsTest, WireSplitterEditorOutputsCarryIndependentBits)
{
  for (const int size : {2, 4}) {
    auto splitter = std::make_shared<WireSplitter>(Bus(), std::vector<Bus>{});
    splitter->setProperty("size", size);
    const Bus input(size);
    splitter->setInput(0, input, true);
    EXPECT_THROW(splitter->setInput(0, Bus(1), true), std::runtime_error);
    ASSERT_EQ(splitter->outputBuses().size(), size);

    auto      circuit = std::make_shared<Circuit>(Component_set{splitter});
    Simulator sim(circuit);
    sim.setBus(input, valueFor(input, 2));
    sim.run(20);

    for (int bit = 0; bit < size; ++bit) {
      ASSERT_EQ(splitter->outputBuses()[bit].size(), 1);
      EXPECT_EQ(splitter->outputBuses()[bit][0]->getCurrentState(),
                bit == 1 ? State::HIGH : State::LOW);
    }
  }
}
