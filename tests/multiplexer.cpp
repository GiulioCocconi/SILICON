/*
 Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include "tests.hpp"

#include <core/circuit.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/simulator.hpp>
#include <extraComponents/multiplexer.hpp>
#include <stdexcept>
#include <vector>

namespace {
void expectBusStates(const Bus& bus, const std::vector<State>& expected)
{
  ASSERT_EQ(bus.size(), expected.size());
  for (unsigned short bit = 0; bit < bus.size(); ++bit)
    EXPECT_EQ(bus[bit]->getCurrentState(), expected[bit]) << "bit=" << bit;
}

std::vector<State> oneHotStates(const size_t width, const size_t selected,
                                const State selectedState = State::HIGH)
{
  std::vector<State> states(width, State::LOW);
  states[selected] = selectedState;
  return states;
}
}  // namespace

TEST(MultiplexerTest, DefaultsAndValidation)
{
  auto mux = std::make_shared<Multiplexer>();

  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("busSize"), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("delay"), 0);
  EXPECT_TRUE(mux->getInputs().empty());
  EXPECT_TRUE(mux->getOutputs().empty());

  EXPECT_THROW(mux->setProperty("selectionSize", 0), std::invalid_argument);
  EXPECT_THROW(mux->setProperty("selectionSize", -1), std::invalid_argument);
  EXPECT_THROW(mux->setProperty("selectionSize", 16), std::invalid_argument);
  EXPECT_THROW(mux->setProperty("busSize", 0), std::invalid_argument);
  EXPECT_THROW(mux->setProperty("busSize", -1), std::invalid_argument);
  EXPECT_THROW(mux->setProperty("delay", -1), std::invalid_argument);
}

TEST(MultiplexerTest, TwoToOneSelectsInputBit)
{
  auto data      = Bus(2);
  auto selection = Bus(1);
  auto output    = std::make_shared<Wire>();
  auto mux       = std::make_shared<Multiplexer>(data, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{mux});
  Simulator sim(circuit);

  sim.setBus(data, 0b10);
  sim.setBus(selection, 0);
  sim.run(10);
  EXPECT_EQ(output->getCurrentState(), State::LOW);

  sim.setBus(selection, 1);
  sim.run(10);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST(MultiplexerTest, LargerMuxSelectsMatchingDataBit)
{
  auto data      = Bus(8);
  auto selection = Bus(3);
  auto output    = std::make_shared<Wire>();
  auto mux       = std::make_shared<Multiplexer>(data, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{mux});
  Simulator sim(circuit);

  sim.setBus(data, 0b1010'0100);

  for (unsigned int selected = 0; selected < 8; ++selected) {
    sim.setBus(selection, selected);
    sim.run(10);

    const State expected = ((0b1010'0100u >> selected) & 1u) ? State::HIGH
                                                             : State::LOW;
    EXPECT_EQ(output->getCurrentState(), expected) << "selected=" << selected;
  }
}

TEST(MultiplexerTest, SelectionSizePropertyReshapesIO)
{
  auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());

  mux->setProperty("selectionSize", 4);

  ASSERT_EQ(mux->getInputs().size(), 2);
  ASSERT_EQ(mux->getOutputs().size(), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 4);
  EXPECT_EQ(mux->getInputs()[0].size(), 16);
  EXPECT_EQ(mux->getInputs()[1].size(), 4);
  EXPECT_EQ(mux->getOutputs()[0].size(), 1);
}

TEST(MultiplexerTest, UnknownAndErrorSelectionPropagate)
{
  {
    auto data      = Bus(2);
    auto selection = Bus(1);
    auto output    = std::make_shared<Wire>();
    auto mux       = std::make_shared<Multiplexer>(data, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{mux});
    Simulator sim(circuit);

    sim.setBus(data, 0b11);
    selection[0]->forceSetCurrentState(State::UNKNOWN);
    sim.run(10);
    EXPECT_EQ(output->getCurrentState(), State::UNKNOWN);
  }

  {
    auto data      = Bus(2);
    auto selection = Bus(1);
    auto output    = std::make_shared<Wire>();
    selection[0]->forceSetCurrentState(State::ERROR);
    auto mux = std::make_shared<Multiplexer>(data, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{mux});
    Simulator sim(circuit);

    sim.setBus(data, 0b11);
    sim.run(10);
    EXPECT_EQ(output->getCurrentState(), State::ERROR);
  }
}

TEST(MultiplexerTest, BusSizePropertyReshapesToMultipleInputBuses)
{
  auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());

  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 3);

  ASSERT_EQ(mux->getInputs().size(), 9);
  ASSERT_EQ(mux->getOutputs().size(), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("busSize"), 4);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 3);
  for (std::size_t input = 0; input < 8; ++input)
    EXPECT_EQ(mux->getInputs()[input].size(), 4);
  EXPECT_EQ(mux->getInputs()[8].size(), 3);
  EXPECT_EQ(mux->getOutputs()[0].size(), 4);
}

TEST(MultiplexerTest, BusSizeOneCollapsesMultiBusInputs)
{
  auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());

  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 3);
  mux->setProperty("busSize", 1);

  ASSERT_EQ(mux->getInputs().size(), 2);
  ASSERT_EQ(mux->getOutputs().size(), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("busSize"), 1);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 3);
  EXPECT_EQ(mux->getInputs()[0].size(), 8);
  EXPECT_EQ(mux->getInputs()[1].size(), 3);
  EXPECT_EQ(mux->getOutputs()[0].size(), 1);
}

TEST(MultiplexerTest, BusMuxSelectsMatchingInputBus)
{
  auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());
  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 2);

  const auto inputs    = mux->getInputs();
  const auto selection = inputs[4];
  const auto output    = mux->getOutputs()[0];

  auto      circuit = std::make_shared<Circuit>(Component_set{mux});
  Simulator sim(circuit);

  sim.setBus(inputs[0], 0b0001);
  sim.setBus(inputs[1], 0b1010);
  sim.setBus(inputs[2], 0b1110);
  sim.setBus(inputs[3], 0b1110);
  sim.setBus(selection, 2);
  sim.run(10);

  expectBusStates(output, {State::LOW, State::HIGH, State::HIGH, State::HIGH});
}

TEST(MultiplexerTest, BusMuxSelectionPropagatesToEveryOutputBit)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());
    mux->setProperty("busSize", 3);
    mux->setProperty("selectionSize", 2);

    auto       selection = mux->getInputs()[4];
    const auto output    = mux->getOutputs()[0];

    selection.forceSetCurrentValue(0);
    selection[1]->forceSetCurrentState(state);

    auto      circuit = std::make_shared<Circuit>(Component_set{mux});
    Simulator sim(circuit);

    sim.run(10);
    expectBusStates(output, std::vector<State>(3, state));
  }
}

TEST(DemultiplexerTest, DefaultsAndValidation)
{
  auto demux = std::make_shared<Demultiplexer>();

  EXPECT_EQ(demux->getPropertyValue<int>("selectionSize"), 1);
  EXPECT_EQ(demux->getPropertyValue<int>("busSize"), 1);
  EXPECT_EQ(demux->getPropertyValue<int>("delay"), 0);
  EXPECT_TRUE(demux->getInputs().empty());
  EXPECT_TRUE(demux->getOutputs().empty());

  EXPECT_THROW(demux->setProperty("selectionSize", 0), std::invalid_argument);
  EXPECT_THROW(demux->setProperty("selectionSize", -1), std::invalid_argument);
  EXPECT_THROW(demux->setProperty("selectionSize", 16), std::invalid_argument);
  EXPECT_THROW(demux->setProperty("busSize", 0), std::invalid_argument);
  EXPECT_THROW(demux->setProperty("busSize", -1), std::invalid_argument);
  EXPECT_THROW(demux->setProperty("delay", -1), std::invalid_argument);
}

TEST(DemultiplexerTest, ConstructorValidation)
{
  EXPECT_THROW(Demultiplexer(Bus(1), Bus(0), Bus(1)), std::invalid_argument);
  EXPECT_THROW(Demultiplexer(Bus(1), Bus(16), Bus(2)), std::invalid_argument);
  EXPECT_THROW(Demultiplexer(Bus(2), Bus(1), Bus(2)), std::invalid_argument);
  EXPECT_THROW(Demultiplexer(Bus(1), Bus(2), Bus(3)), std::invalid_argument);
}

TEST(DemultiplexerTest, SelectionSizePropertyReshapesIO)
{
  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));

  demux->setProperty("selectionSize", 4);

  ASSERT_EQ(demux->getInputs().size(), 2);
  ASSERT_EQ(demux->getOutputs().size(), 1);
  EXPECT_EQ(demux->getPropertyValue<int>("selectionSize"), 4);
  EXPECT_EQ(demux->getInputs()[0].size(), 1);
  EXPECT_EQ(demux->getInputs()[1].size(), 4);
  EXPECT_EQ(demux->getOutputs()[0].size(), 16);
}

TEST(DemultiplexerTest, TwoWayRoutesDataToSelectedOutput)
{
  auto data      = Bus(1);
  auto selection = Bus(1);
  auto output    = Bus(2);
  auto demux     = std::make_shared<Demultiplexer>(data, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{demux});
  Simulator sim(circuit);

  sim.setBus(data, 1);
  sim.setBus(selection, 0);
  sim.run(10);
  expectBusStates(output, {State::HIGH, State::LOW});

  sim.setBus(selection, 1);
  sim.run(10);
  expectBusStates(output, {State::LOW, State::HIGH});
}

TEST(DemultiplexerTest, LargerSelectionRoutesDataToSelectedOutput)
{
  auto data      = Bus(1);
  auto selection = Bus(3);
  auto output    = Bus(8);
  auto demux     = std::make_shared<Demultiplexer>(data, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{demux});
  Simulator sim(circuit);

  sim.setBus(data, 1);

  for (unsigned int selected = 0; selected < 8; ++selected) {
    sim.setBus(selection, selected);
    sim.run(10);
    expectBusStates(output, oneHotStates(8, selected));
  }
}

TEST(DemultiplexerTest, UnknownAndErrorSelectionPropagateToAllOutputs)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto data      = Bus(1);
    auto selection = Bus(2);
    auto output    = Bus(4);
    auto demux     = std::make_shared<Demultiplexer>(data, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{demux});
    Simulator sim(circuit);

    selection.forceSetCurrentValue(0);
    selection[1]->forceSetCurrentState(state);
    sim.setBus(data, 1);
    sim.run(10);
    expectBusStates(output, std::vector<State>(4, state));
  }
}

TEST(DemultiplexerTest, UnknownAndErrorDataPropagatesOnlyToSelectedOutput)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto data      = Bus(1);
    auto selection = Bus(2);
    auto output    = Bus(4);
    auto demux     = std::make_shared<Demultiplexer>(data, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{demux});
    Simulator sim(circuit);

    data[0]->forceSetCurrentState(state);
    sim.setBus(selection, 2);
    sim.run(10);
    expectBusStates(output, oneHotStates(4, 2, state));
  }
}

TEST(DemultiplexerTest, BusSizePropertyReshapesToMultipleOutputBuses)
{
  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));

  demux->setProperty("busSize", 4);
  demux->setProperty("selectionSize", 3);

  ASSERT_EQ(demux->getInputs().size(), 2);
  ASSERT_EQ(demux->getOutputs().size(), 8);
  EXPECT_EQ(demux->getPropertyValue<int>("busSize"), 4);
  EXPECT_EQ(demux->getPropertyValue<int>("selectionSize"), 3);
  EXPECT_EQ(demux->getInputs()[0].size(), 4);
  EXPECT_EQ(demux->getInputs()[1].size(), 3);
  for (const auto& output : demux->getOutputs())
    EXPECT_EQ(output.size(), 4);
}

TEST(DemultiplexerTest, BusSizeOneCollapsesMultiBusOutputs)
{
  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));

  demux->setProperty("busSize", 4);
  demux->setProperty("selectionSize", 3);
  demux->setProperty("busSize", 1);

  ASSERT_EQ(demux->getInputs().size(), 2);
  ASSERT_EQ(demux->getOutputs().size(), 1);
  EXPECT_EQ(demux->getPropertyValue<int>("busSize"), 1);
  EXPECT_EQ(demux->getPropertyValue<int>("selectionSize"), 3);
  EXPECT_EQ(demux->getInputs()[0].size(), 1);
  EXPECT_EQ(demux->getInputs()[1].size(), 3);
  EXPECT_EQ(demux->getOutputs()[0].size(), 8);
}

TEST(DemultiplexerTest, BusDemuxRoutesInputBusToSelectedOutputBus)
{
  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));
  demux->setProperty("busSize", 4);
  demux->setProperty("selectionSize", 2);

  const auto data      = demux->getInputs()[0];
  const auto selection = demux->getInputs()[1];
  const auto outputs   = demux->getOutputs();

  auto      circuit = std::make_shared<Circuit>(Component_set{demux});
  Simulator sim(circuit);

  sim.setBus(data, 0b1011);
  sim.setBus(selection, 3);
  sim.run(10);

  expectBusStates(outputs[0], std::vector<State>(4, State::LOW));
  expectBusStates(outputs[1], std::vector<State>(4, State::LOW));
  expectBusStates(outputs[2], std::vector<State>(4, State::LOW));
  expectBusStates(outputs[3], {State::HIGH, State::HIGH, State::LOW, State::HIGH});
}

TEST(DemultiplexerTest, BusDemuxSelectionPropagatesToEveryOutputBit)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));
    demux->setProperty("busSize", 3);
    demux->setProperty("selectionSize", 2);

    auto       selection = demux->getInputs()[1];
    const auto outputs   = demux->getOutputs();

    selection.forceSetCurrentValue(0);
    selection[1]->forceSetCurrentState(state);

    auto      circuit = std::make_shared<Circuit>(Component_set{demux});
    Simulator sim(circuit);

    sim.run(10);
    for (const auto& output : outputs)
      expectBusStates(output, std::vector<State>(3, state));
  }
}

TEST(DemultiplexerTest, BusDemuxDataPropagatesOnlyToSelectedOutputBus)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));
    demux->setProperty("busSize", 3);
    demux->setProperty("selectionSize", 2);

    auto data      = demux->getInputs()[0];
    auto selection = demux->getInputs()[1];
    auto outputs   = demux->getOutputs();

    data.forceSetCurrentValue(0);
    data[1]->forceSetCurrentState(state);

    auto      circuit = std::make_shared<Circuit>(Component_set{demux});
    Simulator sim(circuit);

    sim.setBus(selection, 2);
    sim.run(10);

    expectBusStates(outputs[0], std::vector<State>(3, State::LOW));
    expectBusStates(outputs[1], std::vector<State>(3, State::LOW));
    expectBusStates(outputs[2], {State::LOW, state, State::LOW});
    expectBusStates(outputs[3], std::vector<State>(3, State::LOW));
  }
}

TEST(MuxDemuxSerializationTest, BusSizeAndMultiPortShapeSurviveRoundTrip)
{
  auto mux = std::make_shared<Multiplexer>(Bus(2), Bus(1), std::make_shared<Wire>());
  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 2);

  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));
  demux->setProperty("busSize", 3);
  demux->setProperty("selectionSize", 2);

  Circuit source(Component_set{mux, demux});
  const auto serialized = source.serialize();

  ComponentRegistry registry;
  registerAllComponents(registry);
  const Circuit restored = Circuit::deserialize(serialized, registry);

  auto restoredMux = std::dynamic_pointer_cast<Multiplexer>(
      restored.getComponentByVertexId(source.getVertexId(mux.get()).value()));
  auto restoredDemux = std::dynamic_pointer_cast<Demultiplexer>(
      restored.getComponentByVertexId(source.getVertexId(demux.get()).value()));

  ASSERT_NE(restoredMux, nullptr);
  ASSERT_NE(restoredDemux, nullptr);

  EXPECT_EQ(restoredMux->getPropertyValue<int>("busSize"), 4);
  EXPECT_EQ(restoredMux->getPropertyValue<int>("selectionSize"), 2);
  ASSERT_EQ(restoredMux->getInputs().size(), 5);
  EXPECT_EQ(restoredMux->getInputs()[4].size(), 2);
  EXPECT_EQ(restoredMux->getOutputs()[0].size(), 4);

  EXPECT_EQ(restoredDemux->getPropertyValue<int>("busSize"), 3);
  EXPECT_EQ(restoredDemux->getPropertyValue<int>("selectionSize"), 2);
  ASSERT_EQ(restoredDemux->getOutputs().size(), 4);
  EXPECT_EQ(restoredDemux->getInputs()[0].size(), 3);
  EXPECT_EQ(restoredDemux->getInputs()[1].size(), 2);
  for (const auto& output : restoredDemux->getOutputs())
    EXPECT_EQ(output.size(), 3);
}

TEST(DecoderTest, DefaultsAndValidation)
{
  auto decoder = std::make_shared<Decoder>();

  EXPECT_EQ(decoder->getPropertyValue<int>("selectionSize"), 1);
  EXPECT_EQ(decoder->getPropertyValue<int>("delay"), 0);
  EXPECT_TRUE(decoder->getInputs().empty());
  EXPECT_TRUE(decoder->getOutputs().empty());

  EXPECT_THROW(decoder->setProperty("selectionSize", 0), std::invalid_argument);
  EXPECT_THROW(decoder->setProperty("selectionSize", -1), std::invalid_argument);
  EXPECT_THROW(decoder->setProperty("selectionSize", 16), std::invalid_argument);
  EXPECT_THROW(decoder->setProperty("delay", -1), std::invalid_argument);
}

TEST(DecoderTest, ConstructorValidation)
{
  EXPECT_THROW(Decoder(Bus(1), Bus(0), Bus(1)), std::invalid_argument);
  EXPECT_THROW(Decoder(Bus(1), Bus(16), Bus(2)), std::invalid_argument);
  EXPECT_THROW(Decoder(Bus(2), Bus(1), Bus(2)), std::invalid_argument);
  EXPECT_THROW(Decoder(Bus(1), Bus(2), Bus(3)), std::invalid_argument);
}

TEST(DecoderTest, SelectionSizePropertyReshapesIO)
{
  auto decoder = std::make_shared<Decoder>(Bus(1), Bus(1), Bus(2));

  decoder->setProperty("selectionSize", 4);

  ASSERT_EQ(decoder->getInputs().size(), 2);
  ASSERT_EQ(decoder->getOutputs().size(), 1);
  EXPECT_EQ(decoder->getPropertyValue<int>("selectionSize"), 4);
  EXPECT_EQ(decoder->getInputs()[0].size(), 1);
  EXPECT_EQ(decoder->getInputs()[1].size(), 4);
  EXPECT_EQ(decoder->getOutputs()[0].size(), 16);
}

TEST(DecoderTest, EnabledDecoderDrivesOneHotOutput)
{
  auto enable    = Bus(1);
  auto selection = Bus(3);
  auto output    = Bus(8);
  auto decoder   = std::make_shared<Decoder>(enable, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{decoder});
  Simulator sim(circuit);

  sim.setBus(enable, 1);
  for (unsigned int selected = 0; selected < 8; ++selected) {
    sim.setBus(selection, selected);
    sim.run(10);
    expectBusStates(output, oneHotStates(8, selected));
  }
}

TEST(DecoderTest, DisabledDecoderDrivesAllOutputsLow)
{
  auto enable    = Bus(1);
  auto selection = Bus(2);
  auto output    = Bus(4);
  auto decoder   = std::make_shared<Decoder>(enable, selection, output);

  auto      circuit = std::make_shared<Circuit>(Component_set{decoder});
  Simulator sim(circuit);

  sim.setBus(enable, 0);
  sim.setBus(selection, 3);
  sim.run(10);
  expectBusStates(output, std::vector<State>(4, State::LOW));
}

TEST(DecoderTest, UnknownAndErrorEnablePropagateToAllOutputs)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto enable    = Bus(1);
    auto selection = Bus(2);
    auto output    = Bus(4);
    auto decoder   = std::make_shared<Decoder>(enable, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{decoder});
    Simulator sim(circuit);

    enable[0]->forceSetCurrentState(state);
    sim.setBus(selection, 1);
    sim.run(10);
    expectBusStates(output, std::vector<State>(4, state));
  }
}

TEST(DecoderTest, UnknownAndErrorSelectionPropagateToAllOutputs)
{
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    auto enable    = Bus(1);
    auto selection = Bus(2);
    auto output    = Bus(4);
    auto decoder   = std::make_shared<Decoder>(enable, selection, output);

    auto      circuit = std::make_shared<Circuit>(Component_set{decoder});
    Simulator sim(circuit);

    selection.forceSetCurrentValue(0);
    selection[1]->forceSetCurrentState(state);
    sim.setBus(enable, 1);
    sim.run(10);
    expectBusStates(output, std::vector<State>(4, state));
  }
}
