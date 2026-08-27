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
#include <stdexcept>
#include <utility>
#include <vector>

using namespace SILICON::core;
using namespace SILICON::extra;
using namespace SILICON::simulation;
using namespace SILICON::waveform;

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

  sim.setBus(Bus{b}, valueFor(Bus{b}, 1));
  sim.run(20);
  EXPECT_EQ(sum->getCurrentState(), State::HIGH);
  EXPECT_EQ(cout->getCurrentState(), State::LOW);

  sim.setBus(Bus{a}, valueFor(Bus{a}, 1));
  sim.run(20);
  EXPECT_EQ(sum->getCurrentState(), State::LOW);
  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
}

TEST(ArithmeticTest, ExtenderSupportsUnsignedSignedAndNarrowingModes)
{
  const auto evaluate = [](const unsigned short inSize, const unsigned short outSize,
                           const std::string& mode, const unsigned int value) {
    auto      input    = Bus(inSize);
    auto      output   = Bus(outSize);
    auto      extender = std::make_shared<Extender>(input, output, mode);
    auto      circuit  = std::make_shared<Circuit>(Component_set{extender});
    Simulator simulator(circuit);
    simulator.setBus(input, valueFor(input, value));
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("Extender simulation did not complete");
    return output.getCurrentValue();
  };

  EXPECT_EQ(evaluate(3, 5, std::string(Extender::UnsignedMode), 6), valueFor(5, 6));
  EXPECT_EQ(evaluate(3, 5, std::string(Extender::SignedMode), 6), valueFor(5, 30));
  EXPECT_EQ(evaluate(5, 3, std::string(Extender::SignedMode), 29), valueFor(3, 5));
}

TEST(ArithmeticTest, ExtenderPropertiesValidateAndReshapeBuses)
{
  auto extender = std::make_shared<Extender>();
  EXPECT_EQ(extender->getPropertyValue<int>("inSize"), 4);
  EXPECT_EQ(extender->getPropertyValue<int>("outSize"), 8);
  EXPECT_EQ(extender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UnsignedMode));
  EXPECT_TRUE(extender->getInputs().empty());
  EXPECT_TRUE(extender->getOutputs().empty());
  EXPECT_THROW(extender->setProperty("inSize", 0), std::invalid_argument);
  EXPECT_THROW(extender->setProperty("outSize", -1), std::invalid_argument);
  EXPECT_THROW(extender->setProperty("mode", std::string("invalid")),
               std::invalid_argument);
  EXPECT_THROW((void)Extender(Bus(), Bus(4)), std::invalid_argument);

  extender =
      std::make_shared<Extender>(Bus(3), Bus(5), std::string(Extender::SignedMode));
  EXPECT_EQ(extender->getPropertyValue<int>("inSize"), 3);
  EXPECT_EQ(extender->getPropertyValue<int>("outSize"), 5);
  EXPECT_EQ(extender->getPropertyValue<std::string>("mode"),
            std::string(Extender::SignedMode));

  extender->setProperty("inSize", 4);
  extender->setProperty("outSize", 7);
  EXPECT_EQ(extender->getInputs()[0].size(), 4);
  EXPECT_EQ(extender->getOutputs()[0].size(), 7);
}

TEST(ArithmeticTest, ComplementerComputesFixedWidthTwosComplement)
{
  auto      input        = Bus(4);
  auto      output       = Bus(4);
  auto      complementer = std::make_shared<Complementer>(input, output);
  auto      circuit      = std::make_shared<Circuit>(Component_set{complementer});
  Simulator simulator(circuit);

  for (const auto [value, expected] : std::vector<std::pair<unsigned int, unsigned int>>{
           {0, 0}, {1, 15}, {3, 13}, {8, 8}, {15, 1}}) {
    simulator.setBus(input, valueFor(input, value));
    ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
    EXPECT_EQ(output.getCurrentValue(), valueFor(output, expected));
  }
}

TEST(ArithmeticTest, ComplementerValidatesAndReshapesItsWidth)
{
  auto complementer = std::make_shared<Complementer>();
  EXPECT_EQ(complementer->getPropertyValue<int>("size"), 4);
  EXPECT_TRUE(complementer->getInputs().empty());
  EXPECT_TRUE(complementer->getOutputs().empty());
  EXPECT_THROW(complementer->setProperty("size", 0), std::invalid_argument);
  EXPECT_THROW(complementer->setProperty("size", -1), std::invalid_argument);

  EXPECT_THROW((void)Complementer(Bus(), Bus()), std::invalid_argument);
  EXPECT_THROW((void)Complementer(Bus(3), Bus(4)), std::invalid_argument);

  complementer = std::make_shared<Complementer>(Bus(5), Bus(5));
  EXPECT_EQ(complementer->getPropertyValue<int>("size"), 5);
  complementer->setProperty("size", 8);
  EXPECT_EQ(complementer->getInputs()[0].size(), 8);
  EXPECT_EQ(complementer->getOutputs()[0].size(), 8);
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

  a.forceSetCurrentValue(valueFor(a, 0));
  b.forceSetCurrentValue(valueFor(b, 0));
  sum.forceSetCurrentValue(valueFor(sum, 0));

  for (int i = 0; i < 4; i++) {
    auto fa = std::make_shared<FullAdder>(std::array<Wire_ptr, 2>{a[i], b[i]},
                                          partialCarryWires[i], sum[i],
                                          partialCarryWires[i + 1]);
    comps.insert(fa);
  }

  auto      circ = std::make_shared<Circuit>(comps);
  Simulator sim(circ);

  sim.setBus(a, valueFor(a, 0));
  sim.setBus(b, valueFor(b, 0));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0));

  sim.setBus(a, valueFor(a, 0b1100));
  sim.setBus(b, valueFor(b, 0b0011));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0b1111));

  sim.setBus(b, valueFor(b, 0b1100));
  sim.setBus(a, valueFor(a, 0b0011));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0b1111));

  sim.setBus(a, valueFor(a, 0b1111));
  sim.setBus(b, valueFor(b, 0b0001));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0));
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

  sim.setBus(a, valueFor(a, 0));
  sim.setBus(b, valueFor(b, 0));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0));

  sim.setBus(a, valueFor(a, 0b1100));
  sim.setBus(b, valueFor(b, 0b0011));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0b1111));

  sim.setBus(b, valueFor(b, 0b1100));
  sim.setBus(a, valueFor(a, 0b0011));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::LOW);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0b1111));

  sim.setBus(a, valueFor(a, 0b1111));
  sim.setBus(b, valueFor(b, 0b0001));
  sim.run(100);

  EXPECT_EQ(cout->getCurrentState(), State::HIGH);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0));
}

TEST(ArithmeticTest, DelayedAdderReschedulesUnchangedOutputBits)
{
  auto a    = Bus(4);
  auto b    = Bus(4);
  auto sum  = Bus(4);
  auto cout = std::make_shared<Wire>();

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{a, b}, sum, cout);
  adder->setProperty("delay", 5);

  auto      circ = std::make_shared<Circuit>(Component_set{adder});
  Simulator sim(circ);

  const std::vector<Sample> inputSnapshots{
      {0, {busValueFromBits("0000"), busValueFromBits("0000")}},
      {2, {busValueFromBits("0100"), busValueFromBits("0101")}},
  };
  const std::vector<Simulator::WaveformInputDriver> inputDrivers{
      {a, {}},
      {b, {}},
  };

  EXPECT_EQ(sim.simulateWaveform(6, inputSnapshots, inputDrivers),
            Simulator::RunResult::Completed);
  for (const auto& wire : sum)
    EXPECT_EQ(wire->getCurrentState(), State::UNKNOWN);

  EXPECT_EQ(sim.run(1), Simulator::RunResult::Completed);
  EXPECT_EQ(sum.getCurrentValue(), valueFor(sum, 0b1001));
}

TEST(ArithmeticTest, AdderNBitsSizePropertyDefaultsAndValidation)
{
  auto adder = std::make_shared<AdderNBits>();

  EXPECT_EQ(adder->getPropertyValue<int>("size"), 4);
  EXPECT_TRUE(adder->getInputs().empty());
  EXPECT_TRUE(adder->getOutputs().empty());

  EXPECT_THROW(adder->setProperty("size", 0), std::invalid_argument);
  EXPECT_THROW(adder->setProperty("size", -1), std::invalid_argument);
}

TEST(ArithmeticTest, AdderNBitsSizePropertyReshapesIO)
{
  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4),
                                            std::make_shared<Wire>());

  ASSERT_EQ(adder->getInputs().size(), 2);
  ASSERT_EQ(adder->getOutputs().size(), 2);
  EXPECT_EQ(adder->getPropertyValue<int>("size"), 4);

  adder->setProperty("size", 8);

  EXPECT_EQ(adder->getPropertyValue<int>("size"), 8);
  EXPECT_EQ(adder->getInputs()[0].size(), 8);
  EXPECT_EQ(adder->getInputs()[1].size(), 8);
  EXPECT_EQ(adder->getOutputs()[0].size(), 8);
  EXPECT_EQ(adder->getOutputs()[1].size(), 1);
}
