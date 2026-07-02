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

#include "tests.hpp"

#include <core/circuit.hpp>
#include <core/register.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/simulator.hpp>

#include <nlohmann/json.hpp>

namespace {

void expectBusStates(const Bus& bus, std::initializer_list<State> expected)
{
  ASSERT_EQ(bus.size(), expected.size());

  unsigned short bit = 0;
  for (const State state : expected) {
    EXPECT_EQ(bus[bit]->getCurrentState(), state) << "Bit " << bit;
    ++bit;
  }
}

void clockCycle(Simulator& simulator, const Wire_ptr& clock)
{
  ASSERT_EQ(simulator.setBus(Bus{clock}, 1), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{clock}, 0), Simulator::RunResult::Completed);
}

void resetRegister(Simulator& simulator, const Wire_ptr& clear)
{
  ASSERT_EQ(simulator.setBus(Bus{clear}, 1), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{clear}, 0), Simulator::RunResult::Completed);
}

std::vector<Component_ptr> componentsIn(const Circuit& circuit)
{
  std::vector<Component_ptr> components;
  const auto&                graph = circuit.getGraph();
  components.reserve(boost::num_vertices(graph));

  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    if (auto component = graph[vertex].component)
      components.push_back(component);
  }

  return components;
}

}  // namespace

TEST(RegisterTest, ValidatesPropertiesAndShapesBuses)
{
  Register reg;

  EXPECT_THROW(reg.setProperty("delay", -1), std::invalid_argument);
  EXPECT_THROW(reg.setProperty("size", 0), std::invalid_argument);
  EXPECT_THROW(reg.setProperty("size", 1), std::invalid_argument);
  EXPECT_THROW(reg.setProperty("inputType", std::string("Invalid")),
               std::invalid_argument);
  EXPECT_THROW(reg.setProperty("outputType", std::string("Invalid")),
               std::invalid_argument);

  std::vector<Bus> inputs{Bus(1), Bus(1), Bus(1), Bus(1)};
  std::vector<Bus> outputs{Bus(1)};
  reg.setInputs(inputs);
  reg.setOutputs(outputs);

  reg.setProperty("size", 4);
  reg.setProperty("inputType", std::string("Parallel"));
  reg.setProperty("outputType", std::string("Serial"));

  ASSERT_EQ(reg.getInputs().size(), 5);
  ASSERT_EQ(reg.getOutputs().size(), 1);
  EXPECT_EQ(reg.getInputs()[0].size(), 4);
  EXPECT_EQ(reg.getInputs()[1].size(), 1);
  EXPECT_EQ(reg.getInputs()[2].size(), 1);
  EXPECT_EQ(reg.getInputs()[3].size(), 1);
  EXPECT_EQ(reg.getInputs()[4].size(), 1);
  EXPECT_EQ(reg.getOutputs()[0].size(), 1);
}

TEST(RegisterTest, MetadataUsesRegisterCategory)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  const auto metadata = registry.metadata(Register::Type);
  EXPECT_EQ(metadata.displayName, "Register");
  EXPECT_EQ(metadata.category, ComponentCategory::Register);
  EXPECT_EQ(componentCategoryName(metadata.category), "Register");
}

TEST(RegisterTest, ParallelInParallelOutCapturesOnRisingEdge)
{
  Bus data(4);
  Bus out(4);
  auto clock = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, out);
  reg->setProperty("size", 4);
  reg->setProperty("inputType", std::string("Parallel"));
  reg->setProperty("outputType", std::string("Parallel"));

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);

  ASSERT_EQ(simulator.setBus(data, 0b1010), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);

  expectBusStates(out, {State::LOW, State::HIGH, State::LOW, State::HIGH});
}

TEST(RegisterTest, ClearIsActiveHighAndAsynchronous)
{
  Bus data(3);
  Bus out(3);
  auto clock = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, out);
  reg->setProperty("size", 3);

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);

  ASSERT_EQ(simulator.setBus(data, 0b111), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  expectBusStates(out, {State::HIGH, State::HIGH, State::HIGH});

  ASSERT_EQ(simulator.setBus(Bus{clear}, 1), Simulator::RunResult::Completed);
  expectBusStates(out, {State::LOW, State::LOW, State::LOW});
}

TEST(RegisterTest, EnableLowHoldsStateButClearStillOverrides)
{
  Bus data(2);
  Bus out(2);
  auto clock  = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear  = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, out);
  reg->setProperty("size", 2);

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);

  ASSERT_EQ(simulator.setBus(data, 0b11), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  expectBusStates(out, {State::HIGH, State::HIGH});

  ASSERT_EQ(simulator.setBus(Bus{enable}, 0), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(data, 0b00), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  expectBusStates(out, {State::HIGH, State::HIGH});

  ASSERT_EQ(simulator.setBus(Bus{clear}, 1), Simulator::RunResult::Completed);
  expectBusStates(out, {State::LOW, State::LOW});
}

TEST(RegisterTest, SerialInSerialOutShiftsLsbFirst)
{
  Bus serialIn(1);
  Bus serialOut(1);
  auto clock = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(serialIn, clock, enable, clear, serialOut);
  reg->setProperty("size", 3);
  reg->setProperty("inputType", std::string("Serial"));
  reg->setProperty("outputType", std::string("Serial"));

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);
  resetRegister(simulator, clear);

  ASSERT_EQ(simulator.setBus(serialIn, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(serialIn, 0), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(serialIn, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::HIGH);
}

TEST(RegisterTest, SerialInParallelOutAccumulatesBits)
{
  Bus serialIn(1);
  Bus out(3);
  auto clock = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(serialIn, clock, enable, clear, out);
  reg->setProperty("size", 3);
  reg->setProperty("inputType", std::string("Serial"));
  reg->setProperty("outputType", std::string("Parallel"));

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);
  resetRegister(simulator, clear);

  ASSERT_EQ(simulator.setBus(serialIn, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  ASSERT_EQ(simulator.setBus(serialIn, 0), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  ASSERT_EQ(simulator.setBus(serialIn, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);

  expectBusStates(out, {State::HIGH, State::LOW, State::HIGH});
}

TEST(RegisterTest, ParallelInSerialOutLoadsThenShiftsLsbFirst)
{
  Bus data(3);
  Bus serialOut(1);
  auto clock  = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear  = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, serialOut);
  reg->setProperty("size", 3);
  reg->setProperty("inputType", std::string("Parallel"));
  reg->setProperty("outputType", std::string("Serial"));
  ASSERT_EQ(reg->getInputs().size(), 5);
  auto load =
      reg->getInputs()[static_cast<unsigned int>(Register::Inputs::Load)][0];

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);
  resetRegister(simulator, clear);

  ASSERT_EQ(simulator.setBus(data, 0b101), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{load}, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::HIGH);

  ASSERT_EQ(simulator.setBus(Bus{load}, 0), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(data, 0b010), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::HIGH);
}

TEST(RegisterTest, ParallelInSerialOutHonorsExplicitLoadControlPriority)
{
  Bus data(3);
  Bus serialOut(1);
  auto clock  = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear  = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, serialOut);
  reg->setProperty("size", 3);
  reg->setProperty("inputType", std::string("Parallel"));
  reg->setProperty("outputType", std::string("Serial"));
  auto load =
      reg->getInputs()[static_cast<unsigned int>(Register::Inputs::Load)][0];

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);
  resetRegister(simulator, clear);

  ASSERT_EQ(simulator.setBus(data, 0b010), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{load}, 1), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(Bus{enable}, 0), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(data, 0b111), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(Bus{clear}, 1), Simulator::RunResult::Completed);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(Bus{clear}, 0), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{enable}, 1), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{load}, 1), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(data, 0b111), Simulator::RunResult::Completed);
  clockCycle(simulator, clock);
  EXPECT_EQ(serialOut[0]->getCurrentState(), State::HIGH);
}

TEST(RegisterTest, DelayAppliesToOutputUpdates)
{
  Bus data(1);
  Bus out(1);
  auto clock = std::make_shared<Wire>(State::LOW);
  auto enable = std::make_shared<Wire>(State::HIGH);
  auto clear = std::make_shared<Wire>(State::LOW);

  auto reg = std::make_shared<Register>(data, clock, enable, clear, out);
  reg->setProperty("delay", 5);

  auto      circuit = std::make_shared<Circuit>(Component_set{reg});
  Simulator simulator(circuit);

  ASSERT_EQ(simulator.setBus(data, 1), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(Bus{clock}, 1), Simulator::RunResult::Completed);
  EXPECT_NE(out[0]->getCurrentState(), State::HIGH);

  ASSERT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(out[0]->getCurrentState(), State::HIGH);
}

TEST(RegisterTest, SerializationPreservesPropertiesAndBusWidths)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  Bus data(4);
  Bus out(1);
  auto reg = std::make_shared<Register>(data, std::make_shared<Wire>(State::LOW),
                                        std::make_shared<Wire>(State::HIGH),
                                        std::make_shared<Wire>(State::LOW), out);
  reg->setProperty("size", 4);
  reg->setProperty("delay", 3);
  reg->setProperty("inputType", std::string("Parallel"));
  reg->setProperty("outputType", std::string("Serial"));

  Circuit original(reg, false);
  auto    restored = Circuit::deserialize(original.serialize(), registry);

  const auto components = componentsIn(restored);
  ASSERT_EQ(components.size(), 1);

  const auto component = components.front();
  ASSERT_TRUE(component);
  EXPECT_EQ(component->typeName(), Register::Type);
  EXPECT_EQ(component->getPropertyValue<int>("size"), 4);
  EXPECT_EQ(component->getPropertyValue<int>("delay"), 3);
  EXPECT_EQ(component->getPropertyValue<std::string>("inputType"), "Parallel");
  EXPECT_EQ(component->getPropertyValue<std::string>("outputType"), "Serial");
  EXPECT_EQ(component->getInputs()[0].size(), 4);
  EXPECT_EQ(component->getInputs()[2].size(), 1);
  EXPECT_EQ(component->getInputs()[4].size(), 1);
  EXPECT_EQ(component->getOutputs()[0].size(), 1);
}
