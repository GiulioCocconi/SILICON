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

#include <limits>
#include <string>
#include <utility>

namespace {
void expectBusStates(const Bus& bus, std::initializer_list<State> expected)
{
  ASSERT_EQ(bus.size(), expected.size());

  size_t index = 0;
  for (const State state : expected) {
    EXPECT_EQ(bus[index]->getCurrentState(), state) << "Bit " << index;
    ++index;
  }
}

class StringListPropertyTestComponent : public Component {
public:
  explicit StringListPropertyTestComponent(std::string defaultValue = "DOWN")
    : Component({}, {})
  {
    defineStringListProperty("orientation", std::move(defaultValue),
                             {"UP", "DOWN", "LEFT", "RIGHT"});
  }

  std::string_view typeName() const override { return "StringListPropertyTest"; }
  void             simulate(Simulator& sim) override {}
};

class CountingNotGate : public NotGate {
public:
  using NotGate::NotGate;

  int simulateCount = 0;

  void simulate(Simulator& sim) override
  {
    ++simulateCount;
    NotGate::simulate(sim);
  }
};

class ContextRecordingComponent : public Component {
public:
  struct Record {
    bool                              initialEvaluation = false;
    std::vector<Bus>                  changedBuses;
    std::vector<std::optional<State>> previousInputStates;
    std::vector<bool>                 changedInputs;
    std::vector<Simulator::EdgeType>  inputEdges;
  };

  explicit ContextRecordingComponent(Bus input) : Component({std::move(input)}, {}) {}

  std::string_view typeName() const override { return "ContextRecording"; }

  void simulate(Simulator&) override { ++legacyCallCount; }

  void simulate(Simulator&, const SimulationContext& context) override
  {
    Record record;
    record.initialEvaluation = context.initialEvaluation;
    record.changedBuses = {context.changedBuses.begin(), context.changedBuses.end()};
    for (const auto& wire : inputs[0]) {
      record.previousInputStates.push_back(context.previousState(wire));
      record.changedInputs.push_back(context.changed(wire));
      record.inputEdges.push_back(Simulator::edgeType(context, wire));
    }
    records.push_back(std::move(record));
  }

  int                 legacyCallCount = 0;
  std::vector<Record> records;
};
}  // namespace

TEST(LogicTest, And)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto      g    = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);  // Process initial zero-delay and gate evaluations

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, ERROR) = " << to_str(o->getCurrentState());

  // Using simulateBus to evaluate the upstream cone for non-standard states
  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, LOW) = " << to_str(o->getCurrentState());

  // Using setBus to reactively propagate inputs through the forward subgraph
  sim.setBus(Bus{a}, 0);  // LOW
  sim.setBus(Bus{b}, 0);  // LOW
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "AND(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);  // HIGH
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "AND(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);  // HIGH
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "AND(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(SimulatorTest, CancelledRunCanResumePendingEvents)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>();

  auto       gate    = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto       circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator  simulator(circuit);
  const auto stateBeforeCancellation = o->getCurrentState();

  EXPECT_EQ(simulator.run(20, []() { return true; }), Simulator::RunResult::Cancelled);
  EXPECT_EQ(o->getCurrentState(), stateBeforeCancellation);

  EXPECT_EQ(simulator.run(20), Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);
}

TEST(SimulatorTest, CancellationStopsBusPropagation)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>();

  auto      gate    = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{a}, 1, []() { return true; }),
            Simulator::RunResult::Cancelled);
  EXPECT_EQ(a->getCurrentState(), State::LOW);
}

TEST(SimulatorTest, PassesSimulationContextToComponents)
{
  auto lowBit  = std::make_shared<Wire>(State::LOW);
  auto highBit = std::make_shared<Wire>(State::LOW);
  Bus  input{lowBit, highBit};

  auto      recorder = std::make_shared<ContextRecordingComponent>(input);
  auto      circuit  = std::make_shared<Circuit>(Component_set{recorder});
  Simulator simulator(circuit);

  ASSERT_EQ(recorder->records.size(), 1);
  EXPECT_TRUE(recorder->records[0].initialEvaluation);
  EXPECT_TRUE(recorder->records[0].changedBuses.empty());
  EXPECT_EQ(recorder->legacyCallCount, 0);

  EXPECT_EQ(simulator.setBus(input, 1), Simulator::RunResult::Completed);

  ASSERT_EQ(recorder->records.size(), 2);
  EXPECT_FALSE(recorder->records[1].initialEvaluation);
  ASSERT_EQ(recorder->records[1].changedBuses.size(), 1);
  ASSERT_EQ(recorder->records[1].changedBuses[0].size(), 2);
  EXPECT_EQ(recorder->records[1].changedBuses[0][0], lowBit);
  EXPECT_EQ(recorder->records[1].changedBuses[0][1], highBit);
  ASSERT_EQ(recorder->records[1].previousInputStates.size(), 2);
  ASSERT_TRUE(recorder->records[1].previousInputStates[0].has_value());
  ASSERT_TRUE(recorder->records[1].previousInputStates[1].has_value());
  EXPECT_EQ(*recorder->records[1].previousInputStates[0], State::LOW);
  EXPECT_EQ(*recorder->records[1].previousInputStates[1], State::LOW);
  EXPECT_EQ(recorder->records[1].changedInputs, (std::vector<bool>{true, false}));
  EXPECT_EQ(recorder->records[1].inputEdges,
            (std::vector<Simulator::EdgeType>{Simulator::EdgeType::RISE,
                                              Simulator::EdgeType::NO_CHANGE}));

  EXPECT_EQ(simulator.setBus(input, 0), Simulator::RunResult::Completed);

  ASSERT_EQ(recorder->records.size(), 3);
  EXPECT_EQ(recorder->records[2].inputEdges,
            (std::vector<Simulator::EdgeType>{Simulator::EdgeType::FALL,
                                              Simulator::EdgeType::NO_CHANGE}));
}

TEST(SimulatorTest, DelayedEventBatchCapturesPreviousStateBeforeApplyingEvents)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto x = std::make_shared<Wire>(State::UNKNOWN);
  auto y = std::make_shared<Wire>(State::UNKNOWN);

  auto xDriver = std::make_shared<NotGate>(a, x);
  auto yDriver = std::make_shared<NotGate>(b, y);
  xDriver->setProperty("delay", 5);
  yDriver->setProperty("delay", 5);

  auto recorder = std::make_shared<ContextRecordingComponent>(Bus{x, y});
  auto circuit  = std::make_shared<Circuit>(Component_set{xDriver, yDriver, recorder});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);

  ASSERT_GE(recorder->records.size(), 2);
  const auto& delayedRecord = recorder->records.back();
  EXPECT_FALSE(delayedRecord.initialEvaluation);
  ASSERT_EQ(delayedRecord.previousInputStates.size(), 2);
  ASSERT_TRUE(delayedRecord.previousInputStates[0].has_value());
  ASSERT_TRUE(delayedRecord.previousInputStates[1].has_value());
  EXPECT_EQ(*delayedRecord.previousInputStates[0], State::UNKNOWN);
  EXPECT_EQ(*delayedRecord.previousInputStates[1], State::UNKNOWN);
  EXPECT_EQ(delayedRecord.changedInputs, (std::vector<bool>{true, true}));
  EXPECT_EQ(delayedRecord.inputEdges,
            (std::vector<Simulator::EdgeType>{Simulator::EdgeType::UNKNOWN,
                                              Simulator::EdgeType::UNKNOWN}));
  EXPECT_EQ(x->getCurrentState(), State::HIGH);
  EXPECT_EQ(y->getCurrentState(), State::LOW);
}

TEST(SimulatorTest, SimulatesInternalInputWaveform)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>();

  auto      gate    = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  const std::vector<SiliconWaveformSample> inputSnapshots{
      {0, {"0", "0"}},
      {3, {"1", "1"}},
  };
  const std::vector<Simulator::WaveformInputDriver> inputDrivers{
      {Bus{a}, {}},
      {Bus{b}, {}},
  };

  EXPECT_EQ(simulator.simulateWaveform(10, inputSnapshots, inputDrivers),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.getCurrentTime(), 10);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);
}

TEST(SimulatorTest, WaveformInputSampleCapturesPreviousWireStates)
{
  auto lowBit  = std::make_shared<Wire>(State::LOW);
  auto highBit = std::make_shared<Wire>(State::LOW);
  Bus  input{lowBit, highBit};

  auto      recorder = std::make_shared<ContextRecordingComponent>(input);
  auto      circuit  = std::make_shared<Circuit>(Component_set{recorder});
  Simulator simulator(circuit);

  const std::vector<SiliconWaveformSample> inputSnapshots{{0, {"01"}}};
  const std::vector<Simulator::WaveformInputDriver> inputDrivers{{input, {}}};

  EXPECT_EQ(simulator.simulateWaveform(1, inputSnapshots, inputDrivers),
            Simulator::RunResult::Completed);

  ASSERT_EQ(recorder->records.size(), 2);
  const auto& sampleRecord = recorder->records[1];
  EXPECT_FALSE(sampleRecord.initialEvaluation);
  ASSERT_EQ(sampleRecord.previousInputStates.size(), 2);
  ASSERT_TRUE(sampleRecord.previousInputStates[0].has_value());
  ASSERT_TRUE(sampleRecord.previousInputStates[1].has_value());
  EXPECT_EQ(*sampleRecord.previousInputStates[0], State::LOW);
  EXPECT_EQ(*sampleRecord.previousInputStates[1], State::LOW);
  EXPECT_EQ(sampleRecord.changedInputs, (std::vector<bool>{true, false}));
  EXPECT_EQ(sampleRecord.inputEdges,
            (std::vector<Simulator::EdgeType>{Simulator::EdgeType::RISE,
                                              Simulator::EdgeType::NO_CHANGE}));
}

TEST(SimulatorTest, DelayedGateRejectsShortInputPulse)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::LOW);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  gate->setProperty("delay", 10);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{a}, 1), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{a}, 0), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.run(20), Simulator::RunResult::Completed);

  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(SimulatorTest, DelayedGateReplacesPendingTransition)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  gate->setProperty("delay", 10);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{a}, 1), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::UNKNOWN);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);
}

TEST(SimulatorTest, DelayedGateReevaluationDoesNotPostponePendingTransition)
{
  auto input    = std::make_shared<Wire>(State::LOW);
  auto inverted = std::make_shared<Wire>(State::UNKNOWN);
  auto output   = std::make_shared<Wire>(State::UNKNOWN);

  auto notGate = std::make_shared<CountingNotGate>(input, inverted);
  notGate->setProperty("delay", 7);

  auto andGate =
      std::make_shared<AndGate>(std::vector<Wire_ptr>{input, inverted}, output);
  andGate->setProperty("delay", 5);

  auto      circuit = std::make_shared<Circuit>(Component_set{notGate, andGate});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::LOW);
  EXPECT_EQ(inverted->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(notGate->simulateCount, 1);

  EXPECT_EQ(simulator.run(2), Simulator::RunResult::Completed);
  EXPECT_EQ(inverted->getCurrentState(), State::HIGH);
  EXPECT_EQ(notGate->simulateCount, 1);
}

TEST(SimulatorTest, ForwardPropagationDoesNotRescheduleIndependentDelayedBranch)
{
  auto a1 = std::make_shared<Wire>(State::HIGH);
  auto b1 = std::make_shared<Wire>(State::HIGH);
  auto o1 = std::make_shared<Wire>(State::UNKNOWN);

  auto a2 = std::make_shared<Wire>(State::HIGH);
  auto b2 = std::make_shared<Wire>(State::HIGH);
  auto o2 = std::make_shared<Wire>(State::UNKNOWN);

  auto first = std::make_shared<AndGate>(std::vector<Wire_ptr>{a1, b1}, o1);
  first->setProperty("delay", 10);

  auto second = std::make_shared<AndGate>(std::vector<Wire_ptr>{a2, b2}, o2);
  second->setProperty("delay", 10);

  auto      circuit = std::make_shared<Circuit>(Component_set{first, second});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{a1}, 0), Simulator::RunResult::Completed);

  EXPECT_EQ(simulator.run(5), Simulator::RunResult::Completed);
  EXPECT_EQ(o1->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(o2->getCurrentState(), State::HIGH);
}

TEST(SimulatorTest, RunUntilIdlePropagatesThroughLongDelayChain)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto c = std::make_shared<Wire>(State::HIGH);
  auto w = std::make_shared<Wire>(State::UNKNOWN);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto first = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, w);
  first->setProperty("delay", 50);

  auto second = std::make_shared<AndGate>(std::vector<Wire_ptr>{w, c}, o);
  second->setProperty("delay", 50);

  auto      circuit = std::make_shared<Circuit>(Component_set{first, second});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.getCurrentTime(), 100);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);
}

TEST(SimulatorTest, RunUntilIdleStopsAtStepLimitForDelayedFeedback)
{
  const auto previousMaxSimulationSteps = Simulator::getMaxSimulationSteps();
  Simulator::setMaxSimulationSteps(3);

  auto wire = std::make_shared<Wire>(State::LOW);
  auto gate = std::make_shared<NotGate>(wire, wire);
  gate->setProperty("delay", 1);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  EXPECT_EQ(simulator.runUntilIdle(), Simulator::RunResult::StepLimitReached);
  EXPECT_EQ(simulator.getCurrentTime(), 3);

  Simulator::setMaxSimulationSteps(previousMaxSimulationSteps);
}


















TEST(SimulatorTest, InteractiveSettlingKeepsWaveformChangesAtDistinctTimes)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  gate->setProperty("delay", 0);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  std::vector<SiliconWaveformSample> samples;
  auto appendSnapshot = [&samples](uint64_t time, std::vector<std::string> values) {
    if (!samples.empty() && samples.back().time == time) {
      samples.back().values = std::move(values);
      return;
    }
    samples.push_back({time, std::move(values)});
  };

  simulator.setTraceBuses({{"input", Bus{a}}, {"output", Bus{o}}});
  simulator.setTraceSink([&](uint64_t time, const std::vector<std::string>& values) {
    appendSnapshot(time, values);
  });

  auto settleInteractive = [&simulator]() {
    const auto result = simulator.runUntilIdle();
    if (result != Simulator::RunResult::Completed)
      return result;
    return simulator.run(1);
  };

  EXPECT_EQ(settleInteractive(), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{a}, 1), Simulator::RunResult::Completed);
  EXPECT_EQ(settleInteractive(), Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{a}, 0), Simulator::RunResult::Completed);
  EXPECT_EQ(settleInteractive(), Simulator::RunResult::Completed);

  ASSERT_GE(samples.size(), 4U);
  EXPECT_EQ(samples[0].time, 0);
  EXPECT_EQ(samples[1].time, 1);
  EXPECT_EQ(samples[2].time, 2);
  EXPECT_EQ(samples[3].time, 3);
  EXPECT_EQ(samples[1].values, (std::vector<std::string>{"1", "1"}));
  EXPECT_EQ(samples[2].values, (std::vector<std::string>{"0", "0"}));
}

TEST(SimulatorTest, WaveformAppliesSameTimestampInputsTogether)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>(State::LOW);

  auto gate = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{a, b}, o);
  gate->setProperty("delay", 0);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit);

  std::vector<std::string> tracedValues;
  simulator.setTraceBuses({{"xor", Bus{o}}});
  simulator.setTraceSink([&](uint64_t, const std::vector<std::string>& values) {
    if (!values.empty())
      tracedValues.push_back(values[0]);
  });
  tracedValues.clear();

  const std::vector<SiliconWaveformSample> inputSnapshots{
      {0, {"0", "0"}},
      {3, {"1", "1"}},
  };
  const std::vector<Simulator::WaveformInputDriver> inputDrivers{
      {Bus{a}, {}},
      {Bus{b}, {}},
  };

  EXPECT_EQ(simulator.simulateWaveform(5, inputSnapshots, inputDrivers),
            Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
  EXPECT_TRUE(std::ranges::none_of(
      tracedValues, [](const std::string& value) { return value == "1"; }));
}

TEST(LogicTest, Nand)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>();

  auto      g    = std::make_shared<NandGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::LOW);

  sim.setBus(Bus{b}, 0);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);
}

TEST(LogicTest, Or)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto      g    = std::make_shared<OrGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, ERROR) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 0);
  sim.setBus(Bus{b}, 0);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "OR(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "OR(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "OR(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(LogicTest, Nor)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>();

  auto      g    = std::make_shared<NorGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::HIGH);

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(LogicTest, Xor)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto      g    = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, ERROR) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 0);
  sim.setBus(Bus{b}, 0);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "XOR(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "XOR(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "XOR(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(LogicTest, CircuitEditing1)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>();

  {
    auto      g = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::HIGH);
  }

  {
    auto      g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::LOW);
  }

  {
    b->forceSetCurrentState(State::HIGH);
    auto      g = std::make_shared<OrGate>(std::vector<Wire_ptr>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::HIGH);
  }
}

TEST(LogicTest, CircuitEditing2)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto c = std::make_shared<Wire>(State::HIGH);

  auto o = std::make_shared<Wire>();

  auto      ag   = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{ag});
  Simulator sim(circ);
  sim.run(20);

  std::vector<Bus> newInputs = {{c}, {b}};
  ag->setInputs(newInputs);

  // Dynamic topology alters require instantiating a fresh graph configuration
  auto      circ2 = std::make_shared<Circuit>(Component_set{ag});
  Simulator sim2(circ2);
  sim2.run(20);

  a->forceSetCurrentState(State::ERROR);  // Should not influence the gate anymore

  EXPECT_EQ(o->getCurrentState(), State::HIGH);

  sim2.setBus(Bus{c}, 0);
  sim2.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(LogicTest, InteractiveTopologyEditRecompilesSimulatorPlan)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto c = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>(State::UNKNOWN);

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  gate->setProperty("delay", 0);

  auto      circuit = std::make_shared<Circuit>(Component_set{gate});
  Simulator simulator(circuit, 0, true);

  EXPECT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);

  std::vector<Bus> newInputs = {{c}, {b}};
  gate->setInputs(newInputs);

  a->forceSetCurrentState(State::LOW);
  EXPECT_EQ(simulator.setBus(Bus{c}, 0), Simulator::RunResult::Completed);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(LogicTest, BusSettingReading)
{
  auto a = Bus(4);
  for (int i = 0; i <= 0b1111; i++) {
    a.forceSetCurrentValue(i);
    EXPECT_EQ(a.getCurrentValue(), i);
  }
}

TEST(LogicTest, BusSettingReadingAtUnsignedIntWidth)
{
  Bus bus(std::numeric_limits<unsigned int>::digits);

  EXPECT_EQ(bus.forceSetCurrentValue(std::numeric_limits<unsigned int>::max()), 0);
  EXPECT_EQ(bus.getCurrentValue(), std::numeric_limits<unsigned int>::max());

  EXPECT_EQ(bus.setCurrentValue(0, {}), 0);
  EXPECT_EQ(bus.getCurrentValue(), 0U);
}

TEST(LogicTest, GateBitwiseDefaultsAndValidation)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  EXPECT_EQ(gate->getPropertyValue<bool>("bitwise"), false);
  EXPECT_EQ(gate->getPropertyValue<int>("size"), 1);
  EXPECT_EQ(gate->getInputs()[0].size(), 1);
  EXPECT_EQ(gate->getInputs()[1].size(), 1);
  EXPECT_EQ(gate->getOutputs()[0].size(), 1);

  EXPECT_THROW(gate->setProperty("size", 0), std::invalid_argument);
  EXPECT_THROW(gate->setProperty("size", -2), std::invalid_argument);

  auto notGate = std::make_shared<NotGate>(a, o);
  EXPECT_FALSE(notGate->getProperty("bitwise").has_value());
  EXPECT_FALSE(notGate->getProperty("size").has_value());
}

TEST(LogicTest, GateBitwiseReshapesIO)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto gate = std::make_shared<OrGate>(std::vector<Wire_ptr>{a, b}, o);

  gate->setProperty("size", 4);
  EXPECT_EQ(gate->getPropertyValue<int>("size"), 4);
  EXPECT_EQ(gate->getInputs()[0].size(), 1);
  EXPECT_EQ(gate->getInputs()[1].size(), 1);
  EXPECT_EQ(gate->getOutputs()[0].size(), 1);

  gate->setProperty("bitwise", true);
  EXPECT_EQ(gate->getInputs()[0].size(), 4);
  EXPECT_EQ(gate->getInputs()[1].size(), 4);
  EXPECT_EQ(gate->getOutputs()[0].size(), 4);

  gate->setProperty("size", 2);
  EXPECT_EQ(gate->getInputs()[0].size(), 2);
  EXPECT_EQ(gate->getInputs()[1].size(), 2);
  EXPECT_EQ(gate->getOutputs()[0].size(), 2);

  gate->setProperty("bitwise", false);
  EXPECT_EQ(gate->getPropertyValue<int>("size"), 2);
  EXPECT_EQ(gate->getInputs()[0].size(), 1);
  EXPECT_EQ(gate->getInputs()[1].size(), 1);
  EXPECT_EQ(gate->getOutputs()[0].size(), 1);
}

TEST(LogicTest, AndBitwiseSimulation)
{
  auto gate = std::make_shared<AndGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  auto      circ = std::make_shared<Circuit>(Component_set{gate});
  Simulator sim(circ);
  sim.setBus(gate->getInputs()[0], 0b1101);
  sim.setBus(gate->getInputs()[1], 0b1011);
  sim.run(20);

  expectBusStates(gate->getOutputs()[0],
                  {State::HIGH, State::LOW, State::LOW, State::HIGH});
}

TEST(LogicTest, OrBitwiseSimulation)
{
  auto gate = std::make_shared<OrGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  auto      circ = std::make_shared<Circuit>(Component_set{gate});
  Simulator sim(circ);
  sim.setBus(gate->getInputs()[0], 0b0101);
  sim.setBus(gate->getInputs()[1], 0b1010);
  sim.run(20);

  expectBusStates(gate->getOutputs()[0],
                  {State::HIGH, State::HIGH, State::HIGH, State::HIGH});
}

TEST(LogicTest, NandBitwiseSimulation)
{
  auto gate = std::make_shared<NandGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  auto      circ = std::make_shared<Circuit>(Component_set{gate});
  Simulator sim(circ);
  sim.setBus(gate->getInputs()[0], 0b1101);
  sim.setBus(gate->getInputs()[1], 0b1011);
  sim.run(20);

  expectBusStates(gate->getOutputs()[0],
                  {State::LOW, State::HIGH, State::HIGH, State::LOW});
}

TEST(LogicTest, NorBitwiseSimulation)
{
  auto gate = std::make_shared<NorGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  auto      circ = std::make_shared<Circuit>(Component_set{gate});
  Simulator sim(circ);
  sim.setBus(gate->getInputs()[0], 0b0101);
  sim.setBus(gate->getInputs()[1], 0b1000);
  sim.run(20);

  expectBusStates(gate->getOutputs()[0],
                  {State::LOW, State::HIGH, State::LOW, State::LOW});
}

TEST(LogicTest, XorBitwiseSimulationPreservesUnknownAndErrorPerBit)
{
  auto gate = std::make_shared<XorGate>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  auto inputs = gate->getInputs();
  inputs[0][0]->forceSetCurrentState(State::LOW);
  inputs[1][0]->forceSetCurrentState(State::HIGH);
  inputs[0][1]->forceSetCurrentState(State::HIGH);
  inputs[1][1]->forceSetCurrentState(State::HIGH);
  inputs[0][2]->forceSetCurrentState(State::UNKNOWN);
  inputs[1][2]->forceSetCurrentState(State::LOW);
  inputs[0][3]->forceSetCurrentState(State::ERROR);
  inputs[1][3]->forceSetCurrentState(State::HIGH);

  auto      circ = std::make_shared<Circuit>(Component_set{gate});
  Simulator sim(circ);
  sim.run(20);

  expectBusStates(gate->getOutputs()[0],
                  {State::HIGH, State::LOW, State::UNKNOWN, State::ERROR});
}

// --- Component Property Tests --------------------------------------------------------

TEST(ComponentTest, SetAndGetIntProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("value");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<int>(*prop), 10);

  c->setProperty("value", 20);
  prop = c->getProperty("value");
  EXPECT_EQ(std::get<int>(*prop), 20);
}

TEST(ComponentTest, SetAndGetIntPropertyTemplated)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getPropertyValue<int>("value");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(*prop, 10);

  c->setPropertyValue<int>("value", 20);
  prop = c->getPropertyValue<int>("value");
  EXPECT_EQ(*prop, 20);
}

TEST(ComponentTest, SetAndGetBoolProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("enabled", false); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("enabled");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<bool>(*prop), false);

  c->setProperty("enabled", true);
  prop = c->getProperty("enabled");
  EXPECT_EQ(std::get<bool>(*prop), true);
}

TEST(ComponentTest, SetAndGetStringProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {})
    {
      defineProperty("name", std::string("default"));
    }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("name");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<std::string>(*prop), "default");

  c->setProperty("name", std::string("custom"));
  prop = c->getProperty("name");
  EXPECT_EQ(std::get<std::string>(*prop), "custom");
}

TEST(ComponentTest, SetAndGetStringListProperty)
{
  auto c = std::make_shared<StringListPropertyTestComponent>();

  auto prop = c->getPropertyValue<std::string>("orientation");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(*prop, "DOWN");

  const auto options = c->getStringPropertyOptions("orientation");
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->get().size(), 4);

  c->setProperty("orientation", std::string("LEFT"));
  EXPECT_EQ(c->getPropertyValue<std::string>("orientation").value(), "LEFT");
}

TEST(ComponentTest, StringListPropertyRejectsInvalidValues)
{
  auto c = std::make_shared<StringListPropertyTestComponent>();

  EXPECT_THROW(c->setProperty("orientation", std::string("DIAGONAL")),
               std::invalid_argument);
  EXPECT_EQ(c->getPropertyValue<std::string>("orientation").value(), "DOWN");
}

TEST(ComponentTest, StringListPropertyRejectsInvalidDefault)
{
  EXPECT_THROW((void)std::make_shared<StringListPropertyTestComponent>("DIAGONAL"),
               std::invalid_argument);
}

TEST(ComponentTest, StringListPropertyRejectsInvalidCallbackResult)
{
  auto c = std::make_shared<StringListPropertyTestComponent>();

  EXPECT_THROW(
      c->setPropertyCallback(
          "orientation", [](const PropertyValue&) { return std::string("DIAGONAL"); }),
      std::invalid_argument);
  EXPECT_EQ(c->getPropertyValue<std::string>("orientation").value(), "DOWN");
}

TEST(ComponentTest, GetPropertyNotFound)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("nonexistent");
  EXPECT_FALSE(prop.has_value());
}

TEST(ComponentTest, SetPropertyTypeMismatch)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  EXPECT_THROW(c->setProperty("value", std::string("wrong type")), std::invalid_argument);
}

TEST(ComponentTest, SetPropertyUndefined)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) {}
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  EXPECT_THROW(c->setProperty("undefined", 10), std::invalid_argument);
}

TEST(ComponentTest, GetProperties)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {})
    {
      defineProperty("intVal", 5);
      defineProperty("boolVal", true);
      defineProperty("strVal", std::string("hello"));
    }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c     = std::make_shared<TestComponent>();
  auto props = c->getProperties();

  EXPECT_EQ(props.size(), 3);
  EXPECT_EQ(std::get<int>(props.at("intVal")), 5);
  EXPECT_EQ(std::get<bool>(props.at("boolVal")), true);
  EXPECT_EQ(std::get<std::string>(props.at("strVal")), "hello");
}
