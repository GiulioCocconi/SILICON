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

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <core/circuit.hpp>
#include <core/memory.hpp>
#include <core/register.hpp>
#include <core/elaboration.hpp>
#include <core/io.hpp>
#include <core/projectCircuitResolver.hpp>
#include <core/projectContext.hpp>
#include <core/subcircuit.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/simulationSession.hpp>
#include <core/simulator.hpp>

using namespace SILICON::core;

using namespace SILICON::simulation;

namespace {

std::shared_ptr<const std::vector<std::uint8_t>>
binarySnapshot(std::vector<std::uint8_t> contents)
{
  return std::make_shared<const std::vector<std::uint8_t>>(std::move(contents));
}

std::shared_ptr<ROM> configuredRom(std::vector<std::uint8_t> contents,
                                   const int                 dataWidth)
{
  auto rom = std::make_shared<ROM>();
  rom->setProperty("dataWidth", dataWidth);
  rom->setBinaryDocument("program", binarySnapshot(std::move(contents)));
  return rom;
}

void expectState(const Bus& bus, const State state)
{
  for (const auto& wire : bus)
    EXPECT_EQ(wire->getCurrentState(), state);
}

}  // namespace

TEST(ROMTest, ValidatesPropertiesAndShapesBusesFromPackedWordCount)
{
  ROM rom;

  EXPECT_THROW(rom.setProperty("dataWidth", 0), std::invalid_argument);
  EXPECT_THROW(rom.setProperty("dataWidth", 65536), std::invalid_argument);

  rom.setProperty("dataWidth", 10);
  rom.setBinaryDocument(
      "program", binarySnapshot((std::vector<std::uint8_t>{0x12, 0x34, 0x56, 0x78})));

  ASSERT_EQ(rom.inputBuses().size(), 3);
  ASSERT_EQ(rom.outputBuses().size(), 1);
  EXPECT_EQ(rom.inputBuses()[0].size(), 2);
  EXPECT_EQ(rom.inputBuses()[1].size(), 1);
  EXPECT_EQ(rom.inputBuses()[2].size(), 1);
  EXPECT_EQ(rom.outputBuses()[0].size(), 10);

  EXPECT_NO_THROW(rom.setProperty("dataWidth", 6));
  EXPECT_EQ(rom.getPropertyValue<int>("dataWidth"), 6);
  EXPECT_EQ(rom.resolvedWordCount(), 6);
  EXPECT_EQ(rom.inputBuses()[0].size(), 3);
}

TEST(ROMTest, RehydrationUsesSavedAddressWidthForPaddedWords)
{
  ROM rom;
  rom.setProperty("dataWidth", 10);
  rom.setProperty("binaryContents", std::string("packed"));
  rom.setInputs({Bus(1), Bus(1), Bus(1)});
  rom.refreshBinaryContents(
      binarySnapshot((std::vector<std::uint8_t>{0xA5, 0x3C, 0x0F})));
  EXPECT_EQ(rom.inputBuses()[0].size(), 1);
  EXPECT_EQ(rom.resolvedWordCount(), 2);
}

TEST(ROMTest, ExplicitOneWordImageUsesValidAddressBus)
{
  ROM rom;
  rom.setProperty("dataWidth", 8);
  rom.setInputs({Bus(1), Bus(1), Bus(1)});
  rom.refreshBinaryContents(binarySnapshot((std::vector<std::uint8_t>{0xA6})), 1);
  EXPECT_EQ(rom.resolvedWordCount(), 1);
  ASSERT_EQ(rom.inputBuses()[0].size(), 1);
  EXPECT_NE(rom.inputBuses()[0][0], nullptr);
}

TEST(ROMTest, ReadsPackedLittleEndianWordsAcrossByteBoundaries)
{
  auto      rom = configuredRom((std::vector<std::uint8_t>{0xA5, 0x3C, 0xF0, 0xC1}), 10);
  auto      circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator simulator(circuit);

  const Bus address = rom->inputBuses()[0];
  const Bus cs      = rom->inputBuses()[1];
  const Bus data    = rom->outputBuses()[0];
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);

  const std::array<std::uint64_t, 4> expected{0x0A5, 0x00F, 0x01F, 0x003};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    ASSERT_EQ(simulator.setBus(address, valueFor(address, index)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, expected[index])) << index;
  }
}

TEST(ROMTest, ChipSelectLowHoldsLastValueAndOutputEnableIsIgnored)
{
  auto rom = configuredRom((std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0x44}), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  const Bus address = rom->inputBuses()[0];
  const Bus cs      = rom->inputBuses()[1];
  Bus       oe      = rom->inputBuses()[2];
  const Bus data    = rom->outputBuses()[0];

  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x22));

  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 0)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 2)),
            Simulator::RunResult::Completed);
  oe.forceSetCurrentValue(BusValue{State::ERROR});
  rom->simulate(simulator);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x22));

  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x33));
}

TEST(ROMTest, OneWordRomChecksAddressStateAndBounds)
{
  auto               rom     = configuredRom((std::vector<std::uint8_t>{0xA6}), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  Bus       address = rom->inputBuses()[0];
  const Bus cs      = rom->inputBuses()[1];
  const Bus data    = rom->outputBuses()[0];
  ASSERT_EQ(address.size(), 1);

  address.forceSetCurrentValue(BusValue{State::ERROR});
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  expectState(data, State::ERROR);
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0xA6));
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  expectState(data, State::UNKNOWN);
}

TEST(ROMTest, MissingAndOutOfBoundsContentIsUnknown)
{
  auto rom = configuredRom((std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40}), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  Bus cs = rom->inputBuses()[1];
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);

  rom->refreshBinaryContents(
      binarySnapshot((std::vector<std::uint8_t>{0x10, 0x20, 0x30})));
  EXPECT_EQ(rom->resolvedWordCount(), 3);
  EXPECT_EQ(rom->inputBuses()[0].size(), 2);
  const auto address = rom->inputBuses()[0];
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 2)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(rom->outputBuses()[0].getCurrentValue(), valueFor(8, 0x30));
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 3)),
            Simulator::RunResult::Completed);
  expectState(rom->outputBuses()[0], State::UNKNOWN);

  rom->refreshBinaryContents(nullptr);
  rom->simulate(simulator);
  expectState(rom->outputBuses()[0], State::UNKNOWN);

  rom->refreshBinaryContents(binarySnapshot((std::vector<std::uint8_t>{0x10, 0x20})));
  EXPECT_EQ(rom->inputBuses()[0].size(), 1);
}

TEST(ROMTest, ControlUnknownsPropagateWhileEnabledContentIsUnavailable)
{
  auto rom = std::make_shared<ROM>();
  rom->setBinaryDocument("missing", nullptr);
  auto      circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator simulator(circuit);

  Bus cs = rom->inputBuses()[1];
  cs.forceSetCurrentValue(BusValue{State::ERROR});
  rom->simulate(simulator);
  expectState(rom->outputBuses()[0], State::ERROR);

  cs.forceSetCurrentValue(BusValue{State::HIGH});
  rom->simulate(simulator);
  expectState(rom->outputBuses()[0], State::UNKNOWN);
}

TEST(ROMTest, BinaryDocumentChangeAcceptsArbitraryDepthAndClearsStaleContent)
{
  auto       rom = configuredRom((std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40}), 8);
  const auto originalSnapshot = rom->binaryContentsSnapshot();
  const auto originalInputs   = rom->inputBuses();

  EXPECT_NO_THROW(
      rom->setBinaryDocument("three_words", binarySnapshot({0x10, 0x20, 0x30})));
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "three_words");
  EXPECT_NE(rom->binaryContentsSnapshot(), originalSnapshot);
  EXPECT_EQ(*rom->binaryContentsSnapshot(),
            (std::vector<std::uint8_t>{0x10, 0x20, 0x30}));
  EXPECT_EQ(rom->resolvedWordCount(), 3);
  EXPECT_EQ(rom->inputBuses(), originalInputs);

  rom->setProperty("binaryContents", std::string("missing"));
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "missing");
  EXPECT_EQ(rom->binaryContentsSnapshot(), nullptr);
  EXPECT_EQ(rom->inputBuses()[0].size(), 1);
}

TEST(ROMTest, SerializationPreservesWiringAndExplicitHydrationFeedsRuntimeSnapshot)
{
  auto rom = configuredRom((std::vector<std::uint8_t>{0x5A, 0xC3, 0x7E}), 8);
  Circuit            original(Component_set{rom});

  ComponentRegistry registry;
  registerAllComponents(registry);
  if (!ComponentRegistry::instance().hasType(ROM::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto restored =
      std::make_shared<Circuit>(Circuit::deserialize(original.serialize(), registry));
  const auto restoredComponent =
      std::dynamic_pointer_cast<ROM>(restored->getComponentByVertexId(0));
  ASSERT_NE(restoredComponent, nullptr);
  EXPECT_EQ(restoredComponent->getPropertyValue<int>("dataWidth"), 8);
  EXPECT_EQ(restoredComponent->getPropertyValue<std::string>("binaryContents"),
            "program");
  EXPECT_EQ(restoredComponent->binaryContentsSnapshot(), nullptr);
  ASSERT_EQ(restoredComponent->inputBuses()[0].size(), 2);
  EXPECT_EQ(restoredComponent->inputBuses()[0][0]->getId(),
            rom->inputBuses()[0][0]->getId());
  EXPECT_EQ(restoredComponent->inputBuses()[0][1]->getId(),
            rom->inputBuses()[0][1]->getId());

  restoredComponent->refreshBinaryContents(rom->binaryContentsSnapshot());
  Session   session(restored);
  const Bus address = restoredComponent->inputBuses()[0];
  const Bus cs      = restoredComponent->inputBuses()[1];
  const Bus data    = restoredComponent->outputBuses()[0];
  ASSERT_EQ(session.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  ASSERT_EQ(session.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0xC3));

  restoredComponent->refreshBinaryContents(
      binarySnapshot((std::vector<std::uint8_t>{0x00, 0x00, 0x00})));
  ASSERT_EQ(session.setBus(address, valueFor(address, 2)), Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x7E));
}

TEST(ROMTest, MetadataUsesMemoryCategory)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  const auto metadata = registry.metadata(ROM::Type);
  EXPECT_EQ(metadata.category, ComponentCategory::Memory);
  EXPECT_EQ(componentCategoryName(metadata.category), "Memory");
}

namespace {

struct RamFixture {
  std::shared_ptr<RAM> ram = std::make_shared<RAM>();
  std::shared_ptr<Circuit> circuit;
  std::unique_ptr<Simulator> sim;

  explicit RamFixture(int width = 8, int depth = 3,
                      std::vector<std::uint8_t> initial = {})
  {
    ram->setProperty("dataWidth", width);
    ram->setProperty("depth", depth);
    if (!initial.empty())
      ram->setBinaryDocument("initial", binarySnapshot(std::move(initial)));
    circuit = std::make_shared<Circuit>(Component_set{ram}, false);
    sim = std::make_unique<Simulator>(circuit);
    set(RAM::Inputs::ReadAddress, 0);
    set(RAM::Inputs::WriteAddress, 0);
    set(RAM::Inputs::WriteData, 0);
    set(RAM::Inputs::WriteEnable, 0);
    set(RAM::Inputs::Clock, 0);
  }

  Bus input(RAM::Inputs port) const { return ram->inputBuses()[static_cast<unsigned>(port)]; }
  Bus output() const { return ram->outputBuses()[0]; }
  void set(RAM::Inputs port, std::uint64_t value)
  {
    EXPECT_EQ(sim->setBus(input(port), valueFor(input(port), value)), Simulator::RunResult::Completed);
  }
  void set(RAM::Inputs port, const BusValue& value)
  {
    EXPECT_EQ(sim->setBus(input(port), value), Simulator::RunResult::Completed);
  }
  void pulse()
  {
    set(RAM::Inputs::Clock, 0);
    set(RAM::Inputs::Clock, 1);
  }
};

} // namespace

TEST(RAMTest, ValidatesGeometryAndExactBinaryImages)
{
  RAM ram;
  EXPECT_THROW(ram.setProperty("dataWidth", 0), std::invalid_argument);
  EXPECT_THROW(ram.setProperty("dataWidth", 65536), std::invalid_argument);
  EXPECT_THROW(ram.setProperty("depth", 0), std::invalid_argument);
  EXPECT_THROW(ram.setProperty("depth", -1), std::invalid_argument);
  EXPECT_THROW(ram.setMemoryDepth(std::numeric_limits<std::size_t>::max()), std::invalid_argument);
  ram.setProperty("depth", 3);
  EXPECT_EQ(ram.inputBuses()[0].size(), 2);
  EXPECT_EQ(ram.inputBuses()[1].size(), 2);
  EXPECT_THROW(ram.setBinaryDocument("wrong", binarySnapshot({1, 2})), std::invalid_argument);
  EXPECT_EQ(ram.getPropertyValue<std::string>("binaryContents"), "");
  ram.setBinaryDocument("initial", binarySnapshot({1, 2, 3}));
  EXPECT_THROW(ram.setProperty("depth", 4), std::invalid_argument);
  EXPECT_EQ(ram.memoryDepth(), 3);
  ram.refreshBinaryContents(binarySnapshot({1}));
  EXPECT_FALSE(ram.memoryImageSnapshot());
  EXPECT_EQ(ram.memoryDepth(), 3);
}

TEST(RAMTest, ReadsPackedInitializationAndChecksBounds)
{
  RamFixture f(10, 3, {0xA5, 0x3C, 0xF0, 0xC1});
  const std::array<std::uint64_t, 3> values{0x0A5, 0x00F, 0x01F};
  for (std::size_t i = 0; i < values.size(); ++i) {
    f.set(RAM::Inputs::ReadAddress, i);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(f.output(), values[i]));
  }
  f.set(RAM::Inputs::ReadAddress, 3);
  expectState(f.output(), State::UNKNOWN);
  f.set(RAM::Inputs::ReadAddress, BusValue{State::UNKNOWN, State::LOW});
  expectState(f.output(), State::UNKNOWN);
  f.set(RAM::Inputs::ReadAddress, BusValue{State::UNKNOWN, State::ERROR});
  expectState(f.output(), State::ERROR);
}

TEST(RAMTest, UninitializedAndSingleWordMemories)
{
  RamFixture f(1, 1);
  expectState(f.output(), State::UNKNOWN);
  f.set(RAM::Inputs::WriteData, 1);
  f.set(RAM::Inputs::WriteEnable, 1);
  f.pulse();
  expectState(f.output(), State::HIGH);
  f.set(RAM::Inputs::WriteAddress, 1);
  f.set(RAM::Inputs::WriteData, 0);
  f.pulse();
  expectState(f.output(), State::HIGH);
  f.set(RAM::Inputs::ReadAddress, 1);
  expectState(f.output(), State::UNKNOWN);
}

TEST(RAMTest, IndependentAddressesMaskedWritesAndUnchangedReadAddress)
{
  RamFixture f(8, 3, {0x12, 0x34, 0x56});
  const auto initial = f.ram->binaryContentsSnapshot();
  f.set(RAM::Inputs::WriteAddress, 1);
  f.set(RAM::Inputs::WriteData, 0xAB);
  f.set(RAM::Inputs::WriteEnable, 0x0F);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.set(RAM::Inputs::ReadAddress, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x3B));
  f.set(RAM::Inputs::WriteData, 0xC0);
  f.set(RAM::Inputs::WriteEnable, 0xF0);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xCB));
  EXPECT_EQ(f.ram->binaryContentsSnapshot(), initial);
  EXPECT_EQ(*initial, (std::vector<std::uint8_t>{0x12, 0x34, 0x56}));
  f.set(RAM::Inputs::WriteAddress, 3);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xCB));
}

TEST(RAMTest, WritesOnlyOnClockEdgesAndDoesNotResetDuringFullEvaluation)
{
  RamFixture f(8, 1, {0x12});
  f.set(RAM::Inputs::WriteData, 0x34);
  f.set(RAM::Inputs::WriteEnable, 0xFF);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  f.set(RAM::Inputs::WriteData, 0x56);
  f.set(RAM::Inputs::Clock, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  f.set(RAM::Inputs::Clock, 0);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(f.sim->simulateBus(f.input(RAM::Inputs::ReadAddress)), Simulator::RunResult::Completed);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
}

TEST(RAMTest, UnknownMasksAndDataPreserveFourStates)
{
  RamFixture f(4, 1, {0x05});
  f.set(RAM::Inputs::WriteData, BusValue{State::HIGH, State::HIGH, State::UNKNOWN, State::ERROR});
  f.set(RAM::Inputs::WriteEnable, BusValue{State::UNKNOWN, State::UNKNOWN, State::HIGH, State::HIGH});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), (BusValue{State::HIGH, State::UNKNOWN, State::UNKNOWN, State::ERROR}));
  f.set(RAM::Inputs::WriteEnable, BusValue{State::ERROR, State::LOW, State::LOW, State::LOW});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue()[0], State::ERROR);
}

TEST(RAMTest, UnknownWriteAddressOnlyAffectsPossibleWordsAndEnabledBits)
{
  RamFixture f(8, 3, {0x55, 0x55, 0xAA});
  f.set(RAM::Inputs::WriteAddress, BusValue{State::UNKNOWN, State::LOW});
  f.set(RAM::Inputs::WriteEnable, 1);
  f.pulse();
  auto expected = valueFor(8, 0x55);
  expected[0] = State::UNKNOWN;
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(RAM::Inputs::ReadAddress, 1);
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(RAM::Inputs::ReadAddress, 2);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xAA));
  f.set(RAM::Inputs::WriteAddress, BusValue{State::UNKNOWN, State::HIGH});
  f.set(RAM::Inputs::WriteData, 1);
  f.pulse();
  expected = valueFor(8, 0xAA);
  expected[0] = State::UNKNOWN;
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(RAM::Inputs::WriteEnable, 0);
  f.set(RAM::Inputs::WriteAddress, BusValue{State::ERROR, State::ERROR});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), expected);
}

TEST(RAMTest, AmbiguousClockMergesPossibleWrites)
{
  RamFixture f(4, 1, {5});
  f.set(RAM::Inputs::WriteData, 7);
  f.set(RAM::Inputs::WriteEnable, 15);
  f.set(RAM::Inputs::Clock, BusValue{State::UNKNOWN});
  EXPECT_EQ(f.output().getCurrentValue(), (BusValue{State::HIGH, State::UNKNOWN, State::HIGH, State::LOW}));
  f.set(RAM::Inputs::Clock, 0);
  f.set(RAM::Inputs::WriteData, 0);
  f.set(RAM::Inputs::WriteEnable, 1);
  f.set(RAM::Inputs::Clock, BusValue{State::ERROR});
  EXPECT_EQ(f.output().getCurrentValue()[0], State::ERROR);
}

TEST(RAMTest, SamplesRegisterOutputsAndReadDataBeforeTheSameEdge)
{
  auto ram = std::make_shared<RAM>();
  ram->setBinaryDocument("initial", binarySnapshot({0x12}));
  const auto clock = ram->inputBuses()[4][0];
  const Bus data(8);
  const auto high = std::make_shared<Wire>(State::HIGH);
  const auto low = std::make_shared<Wire>(State::LOW);
  auto writer = std::make_shared<Register>(data, clock, high, low, ram->inputBuses()[2]);
  const Bus captured(8);
  auto reader = std::make_shared<Register>(ram->outputBuses()[0], clock, high, low, captured);
  auto circuit = std::make_shared<Circuit>(Component_set{ram, writer, reader}, false);
  Simulator sim(circuit);
  const auto set = [&](const Bus& bus, std::uint64_t value) { EXPECT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed); };
  set(ram->inputBuses()[0], 0);
  set(ram->inputBuses()[1], 0);
  set(ram->inputBuses()[3], 0);
  set(Bus{clock}, 0);
  set(data, 0x34);
  set(Bus{clock}, 1);
  EXPECT_EQ(captured.getCurrentValue(), valueFor(8, 0x12));
  set(Bus{clock}, 0);
  set(ram->inputBuses()[3], 0xFF);
  set(data, 0x56);
  set(Bus{clock}, 1);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(captured.getCurrentValue(), valueFor(8, 0x12));
  set(Bus{clock}, 0);
  set(Bus{clock}, 1);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0x56));
  EXPECT_EQ(captured.getCurrentValue(), valueFor(8, 0x34));
}

TEST(RAMTest, SessionsResetFromImmutableInitializationAndKeepTheirOwnContents)
{
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  if (!ComponentRegistry::instance().hasType(RAM::Type))
    registerAllComponents(ComponentRegistry::instance());
  RamFixture f(8, 1, {0x12});
  // A runtime mutation on the source component must not be cloned into a session.
  f.set(RAM::Inputs::WriteEnable, 0xFF);
  f.set(RAM::Inputs::WriteData, 0x34);
  f.pulse();
  {
    Session session(f.circuit);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
    EXPECT_EQ(session.setBus(f.input(RAM::Inputs::Clock), valueFor(1, 0)), Simulator::RunResult::Completed);
    EXPECT_EQ(session.setBus(f.input(RAM::Inputs::Clock), valueFor(1, 1)), Simulator::RunResult::Completed);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
    f.ram->refreshBinaryContents(binarySnapshot({0x56}));
    EXPECT_EQ(session.setBus(f.input(RAM::Inputs::ReadAddress), valueFor(1, 0)), Simulator::RunResult::Completed);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  }
  Session replacement(f.circuit);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));

  auto copy = std::make_shared<Circuit>(Circuit::deserialize(f.circuit->serialize(), registry));
  auto other = std::dynamic_pointer_cast<RAM>(copy->getComponentByVertexId(0));
  ASSERT_TRUE(other);
  other->refreshBinaryContents(f.ram->binaryContentsSnapshot());
  Session independent(copy);
  EXPECT_EQ(independent.setBus(other->inputBuses()[0], valueFor(1, 0)), Simulator::RunResult::Completed);
  EXPECT_EQ(independent.setBus(other->inputBuses()[1], valueFor(1, 0)), Simulator::RunResult::Completed);
  EXPECT_EQ(independent.setBus(other->inputBuses()[3], valueFor(8, 0xFF)), Simulator::RunResult::Completed);
  EXPECT_EQ(independent.setBus(other->inputBuses()[2], valueFor(8, 0x78)), Simulator::RunResult::Completed);
  EXPECT_EQ(independent.setBus(other->inputBuses()[4], valueFor(1, 0)), Simulator::RunResult::Completed);
  EXPECT_EQ(independent.setBus(other->inputBuses()[4], valueFor(1, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(other->outputBuses()[0].getCurrentValue(), valueFor(8, 0x78));
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));
}

TEST(RAMTest, RepeatedSubcircuitInstancesHaveIndependentContents)
{
  if (!ComponentRegistry::instance().hasType(RAM::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto ram = std::make_shared<RAM>();
  ram->setProperty("binaryContents", std::string("initial"));
  Circuit leaf(Component_set{ram}, false);
  SILICON::project::ProjectContext project;
  project.setDocuments({{"circuits/ram.json", leaf.serialize()},
                        {"bin/initial", std::vector<std::uint8_t>{0x12}}});
  SILICON::project::ProjectCircuitResolver resolver(project, ComponentRegistry::instance());
  auto first = std::make_shared<SubcircuitComponent>(&resolver);
  auto second = std::make_shared<SubcircuitComponent>(&resolver);
  first->setProperty("slug", std::string("ram"));
  second->setProperty("slug", std::string("ram"));
  auto root = std::make_shared<Circuit>(Component_set{first, second}, false);
  {
    Session session(root, {}, &resolver);
    for (const auto& instance : {first, second}) {
      for (auto port : {0, 1, 4})
        EXPECT_EQ(session.setBus(instance->inputBuses()[port], valueFor(instance->inputBuses()[port], 0)),
                  Simulator::RunResult::Completed);
      EXPECT_EQ(instance->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
    }
    EXPECT_EQ(session.setBus(first->inputBuses()[2], valueFor(8, 0x34)), Simulator::RunResult::Completed);
    EXPECT_EQ(session.setBus(first->inputBuses()[3], valueFor(8, 0xFF)), Simulator::RunResult::Completed);
    EXPECT_EQ(session.setBus(first->inputBuses()[4], valueFor(1, 1)), Simulator::RunResult::Completed);
    EXPECT_EQ(first->outputBuses()[0].getCurrentValue(), valueFor(8, 0x34));
    EXPECT_EQ(second->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
  }
  Session reset(root, {}, &resolver);
  EXPECT_EQ(first->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(second->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
}

namespace {

class MemoryReadObserver : public Component {
public:
  int evaluations = 0;
  bool* cancel = nullptr;
  explicit MemoryReadObserver(Bus input) : Component({std::move(input)}, {}) {}
  std::string_view typeName() const override { return "MemoryReadObserver"; }
  void simulate(Simulator&) override
  {
    ++evaluations;
    if (cancel)
      *cancel = true;
  }
};

} // namespace

TEST(RAMTest, StateCommitsOnlyReevaluateTheAffectedConeAndCancellationDiscardsWrites)
{
  auto ram = std::make_shared<RAM>();
  ram->setBinaryDocument("initial", binarySnapshot({0x12}));
  auto observer = std::make_shared<MemoryReadObserver>(ram->outputBuses()[0]);
  auto unrelated = std::make_shared<MemoryReadObserver>(Bus(8));
  auto circuit = std::make_shared<Circuit>(Component_set{ram, observer, unrelated}, false);
  Simulator sim(circuit);
  const auto set = [&](std::size_t port, std::uint64_t value) {
    EXPECT_EQ(sim.setBus(ram->inputBuses()[port], valueFor(ram->inputBuses()[port], value)),
              Simulator::RunResult::Completed);
  };
  set(0, 0); set(1, 0); set(2, 0x34); set(3, 0xFF); set(4, 0);
  const auto unrelatedBefore = unrelated->evaluations;
  bool cancel = false;
  observer->cancel = &cancel;
  EXPECT_EQ(sim.setBus(ram->inputBuses()[4], valueFor(1, 1), [&] { return cancel; }),
            Simulator::RunResult::Cancelled);
  observer->cancel = nullptr;
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
  set(4, 0);
  set(0, 0);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
  const auto beforeCommit = observer->evaluations;
  set(4, 1);
  EXPECT_GT(observer->evaluations, beforeCommit);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(unrelated->evaluations, unrelatedBefore);
}

TEST(RAMTest, FeedbackUsesOneSamplePerClockEdge)
{
  auto ram = std::make_shared<RAM>();
  ram->setProperty("dataWidth", 2);
  ram->setBinaryDocument("initial", binarySnapshot({1}));
  const auto high = std::make_shared<Wire>(State::HIGH);
  const auto low = std::make_shared<Wire>(State::LOW);
  auto reg = std::make_shared<Register>(ram->outputBuses()[0], ram->inputBuses()[4][0],
      high, low, ram->inputBuses()[2]);
  auto circuit = std::make_shared<Circuit>(Component_set{ram, reg}, false);
  Simulator sim(circuit);
  const auto set = [&](std::size_t port, std::uint64_t value) {
    EXPECT_EQ(sim.setBus(ram->inputBuses()[port], valueFor(ram->inputBuses()[port], value)),
              Simulator::RunResult::Completed);
  };
  set(0, 0); set(1, 0); set(3, 0); set(4, 0); set(4, 1);
  EXPECT_EQ(ram->inputBuses()[2].getCurrentValue(), valueFor(2, 1));
  set(4, 0); set(3, 3); set(4, 1);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(2, 1));
}
