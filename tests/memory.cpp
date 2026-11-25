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
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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
  const auto words = (contents.size() * 8 + dataWidth - 1) / dataWidth;
  rom->setGeometry(dataWidth,
                   static_cast<int>(std::max<std::size_t>(1, std::bit_width(words - 1))),
                   static_cast<int>(words));
  rom->setBinaryDocument("program", binarySnapshot(std::move(contents)));
  return rom;
}

void expectState(const Bus& bus, const State state)
{
  for (const auto& wire : bus)
    EXPECT_EQ(wire->getCurrentState(), state);
}

}  // namespace

TEST(ROMTest, GeometryRemainsConfiguredWhenContentsAndWordWidthChange)
{
  ROM rom;
  EXPECT_THROW(rom.setProperty("dataWidth", 0), std::invalid_argument);
  EXPECT_THROW(rom.setProperty("dataWidth", 65536), std::invalid_argument);
  rom.setGeometry(10, 3);
  rom.setBinaryDocument("program", binarySnapshot({0x12, 0x34, 0x56, 0x78}));
  EXPECT_EQ(rom.memoryDepth(), 8);
  EXPECT_EQ(rom.inputBuses()[0].size(), 3);
  EXPECT_EQ(rom.outputBuses()[0].size(), 10);
  rom.setProperty("dataWidth", 6);
  EXPECT_EQ(rom.memoryDepth(), 8);
  EXPECT_EQ(rom.inputBuses()[0].size(), 3);
  EXPECT_EQ(rom.outputBuses()[0].size(), 6);
}

TEST(ROMTest, HydrationPreservesConfiguredAddressWidth)
{
  ROM rom;
  rom.setGeometry(10, 4);
  rom.setProperty("binaryContents", std::string("packed"));
  const auto inputs = rom.inputBuses();
  rom.refreshBinaryContents(binarySnapshot({0xA5, 0x3C, 0x0F}));
  EXPECT_EQ(rom.inputBuses(), inputs);
  EXPECT_EQ(rom.memoryDepth(), 16);
}

TEST(ROMTest, ExplicitOneWordImageUsesValidAddressBus)
{
  ROM rom;
  rom.setProperty("dataWidth", 8);
  rom.setInputs({Bus(1), Bus(1), Bus(1)});
  rom.setGeometry(8, 1, 1);
  rom.refreshBinaryContents(binarySnapshot((std::vector<std::uint8_t>{0xA6})));
  EXPECT_EQ(rom.memoryDepth(), 1);
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

TEST(ROMTest, ShortImagesKeepGeometryAndUnknownTails)
{
  auto rom = configuredRom({0x10, 0x20, 0x30, 0x40}, 8);
  rom->setProperty("padding", std::string(Memory::Unknown));
  auto       circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator  simulator(circuit);
  const auto cs      = rom->inputBuses()[1];
  const auto address = rom->inputBuses()[0];
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  rom->refreshBinaryContents(binarySnapshot({0x10, 0x20, 0x30}));
  EXPECT_EQ(rom->memoryDepth(), 4);
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 2)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(rom->outputBuses()[0].getCurrentValue(), valueFor(8, 0x30));
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 3)),
            Simulator::RunResult::Completed);
  expectState(rom->outputBuses()[0], State::UNKNOWN);
  rom->refreshBinaryContents(nullptr);
  EXPECT_THROW(rom->simulate(simulator), std::runtime_error);
  EXPECT_EQ(rom->inputBuses()[0], address);
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
  EXPECT_THROW(rom->simulate(simulator), std::runtime_error);
}

TEST(ROMTest, BinaryDocumentChangesPreserveGeometryAndClearStaleContent)
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
  EXPECT_EQ(rom->memoryDepth(), 4);
  EXPECT_EQ(rom->inputBuses(), originalInputs);

  rom->setProperty("binaryContents", std::string("missing"));
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "missing");
  EXPECT_EQ(rom->binaryContentsSnapshot(), nullptr);
  EXPECT_EQ(rom->inputBuses()[0].size(), 2);
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
    ram->setGeometry(width,
                     static_cast<int>(std::max(1, std::bit_width(static_cast<unsigned>(
                                                      depth > 0 ? depth - 1 : 0)))),
                     depth);
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

TEST(RAMTest, ValidatesGeometryAndBinaryImages)
{
  RAM ram;
  EXPECT_THROW(ram.setProperty("dataWidth", 0), std::invalid_argument);
  EXPECT_THROW(ram.setProperty("dataWidth", 65536), std::invalid_argument);
  EXPECT_NO_THROW(ram.setProperty("depthOverride", 0));
  EXPECT_THROW(ram.setProperty("depthOverride", -1), std::invalid_argument);
  EXPECT_THROW(ram.setGeometry(8, 1, std::numeric_limits<int>::max()),
               std::invalid_argument);
  ram.setGeometry(8, 2, 3);
  EXPECT_EQ(ram.inputBuses()[0].size(), 2);
  EXPECT_EQ(ram.inputBuses()[1].size(), 2);
  ram.setBinaryDocument("larger", binarySnapshot({1, 2, 3, 4}));
  EXPECT_EQ(ram.memoryDepth(), 4);
  ram.setBinaryDocument("initial", binarySnapshot({1, 2, 3}));
  ram.setGeometry(8, 2, 3);
  EXPECT_THROW(ram.setProperty("depthOverride", 5), std::invalid_argument);
  EXPECT_EQ(ram.memoryDepth(), 3);
  ram.refreshBinaryContents(binarySnapshot({1}));
  EXPECT_TRUE(ram.memoryImageSnapshot());
  EXPECT_EQ(ram.memoryDepth(), 3);
}

TEST(RAMTest, ReadsPackedInitializationAndChecksBounds)
{
  RamFixture                         f(10, 3, {0xA5, 0x3C, 0xF0, 0x01});
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

TEST(RAMTest, IndependentReadPortsPublishWritesAndAddressChangesWhileClockIsHigh)
{
  RamFixture f(8, 2, {0x12, 0x34});
  f.ram->setProperty("readPorts", 2);
  const auto read0    = f.ram->outputBuses()[0];
  const auto read1    = f.ram->outputBuses()[1];
  const auto address1 = f.ram->inputBuses()[RAM::readAddressIndex(1)];
  const auto clock    = f.input(RAM::Inputs::Clock)[0];
  const auto high     = std::make_shared<Wire>(State::HIGH);
  const auto low      = std::make_shared<Wire>(State::LOW);
  const Bus  captured0(8), captured1(8);
  auto       reader0 = std::make_shared<Register>(read0, clock, high, low, captured0);
  auto       reader1 = std::make_shared<Register>(read1, clock, high, low, captured1);
  f.circuit = std::make_shared<Circuit>(Component_set{f.ram, reader0, reader1}, false);
  f.sim     = std::make_unique<Simulator>(f.circuit);
  f.set(RAM::Inputs::ReadAddress, 0);
  ASSERT_EQ(f.sim->setBus(address1, valueFor(1, 1)), Simulator::RunResult::Completed);
  f.set(RAM::Inputs::WriteAddress, 1);
  f.set(RAM::Inputs::WriteData, 0x56);
  f.set(RAM::Inputs::WriteEnable, 0xFF);
  f.pulse();
  EXPECT_EQ(read0.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(read1.getCurrentValue(), valueFor(8, 0x56));
  EXPECT_EQ(captured0.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(captured1.getCurrentValue(), valueFor(8, 0x34));

  f.set(RAM::Inputs::ReadAddress, 1);
  ASSERT_EQ(f.sim->setBus(address1, valueFor(1, 0)), Simulator::RunResult::Completed);
  EXPECT_EQ(read0.getCurrentValue(), valueFor(8, 0x56));
  EXPECT_EQ(read1.getCurrentValue(), valueFor(8, 0x12));
  // Address-only evaluation must not replay the write edge or the reader captures.
  EXPECT_EQ(captured1.getCurrentValue(), valueFor(8, 0x34));
  for (const auto state : {State::UNKNOWN, State::ERROR}) {
    ASSERT_EQ(f.sim->setBus(address1, BusValue{state}), Simulator::RunResult::Completed);
    expectState(read1, state);
    EXPECT_EQ(read0.getCurrentValue(), valueFor(8, 0x56));
  }
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
  Wire_ptr cancelWhenHigh;
  bool     fail = false;
  explicit MemoryReadObserver(Bus input) : Component({std::move(input)}, {}) {}
  std::string_view typeName() const override { return "MemoryReadObserver"; }
  void simulate(Simulator&) override
  {
    ++evaluations;
    if (cancel && (!cancelWhenHigh || cancelWhenHigh->getCurrentState() == State::HIGH))
      *cancel = true;
    if (fail)
      throw std::runtime_error("Read observer failed");
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

TEST(RAMTest, CombinationalFeedbackCannotReplaceTheFirstSccWriteSample)
{
  RamFixture  f(2, 1, {1});
  const auto& read   = f.ram->outputBuses()[0];
  const auto  data   = f.input(RAM::Inputs::WriteData);
  const auto  clock  = f.input(RAM::Inputs::Clock)[0];
  auto        invert = std::make_shared<NotGate>(read[0], data[0]);
  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{read[0], clock}, data[1]);
  invert->setProperty("delay", 0);
  gate->setProperty("delay", 0);
  f.circuit = std::make_shared<Circuit>(Component_set{f.ram, invert, gate}, false);
  f.sim     = std::make_unique<Simulator>(f.circuit);
  f.set(RAM::Inputs::WriteEnable, 3);
  EXPECT_EQ(data.getCurrentValue(), valueFor(2, 0));

  // The AND gate changes data to 2 during SCC settling on this edge. The RAM
  // must keep its first sample (0), even though the SCC visits it again.
  f.pulse();
  EXPECT_EQ(read.getCurrentValue(), valueFor(2, 0));
  f.pulse();
  EXPECT_EQ(read.getCurrentValue(), valueFor(2, 1));
}

TEST(RAMTest, UnobservedWritesNeedNoExtraPassAndFailedPassesRestoreStorage)
{
  RamFixture f(8, 2, {0x12, 0x56});
  auto       observer = std::make_shared<MemoryReadObserver>(f.output());
  f.circuit           = std::make_shared<Circuit>(Component_set{f.ram, observer}, false);
  f.sim               = std::make_unique<Simulator>(f.circuit);
  f.set(RAM::Inputs::WriteAddress, 1);
  f.set(RAM::Inputs::WriteData, 0x34);
  f.set(RAM::Inputs::WriteEnable, 0xFF);

  observer->fail = true;
  EXPECT_THROW(f.sim->setBus(f.input(RAM::Inputs::Clock), valueFor(1, 1)),
               std::runtime_error);
  observer->fail = false;
  f.set(RAM::Inputs::Clock, 0);
  f.set(RAM::Inputs::ReadAddress, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));

  f.set(RAM::Inputs::ReadAddress, 0);
  const auto before = observer->evaluations;
  f.set(RAM::Inputs::Clock, 1);
  // The clock's forward cone runs once. Writing an unread word produces no
  // output transition and must not schedule an owner/cone reevaluation.
  EXPECT_EQ(observer->evaluations, before + 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.set(RAM::Inputs::ReadAddress, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
}

TEST(RAMTest, SimultaneousIndependentClocksExchangePreEdgeReadData)
{
  RamFixture a(8, 1, {0x12});
  RamFixture b(8, 1, {0x34});
  a.ram->setInput(static_cast<unsigned>(RAM::Inputs::WriteData), b.output(), true);
  b.ram->setInput(static_cast<unsigned>(RAM::Inputs::WriteData), a.output(), true);
  auto      circuit = std::make_shared<Circuit>(Component_set{a.ram, b.ram}, false);
  Simulator sim(circuit);
  for (const auto& ram : {a.ram, b.ram})
    ASSERT_EQ(sim.setBus(ram->inputBuses()[3], valueFor(8, 0xFF)),
              Simulator::RunResult::Completed);

  for (int edge = 0; edge < 2; ++edge) {
    for (const auto& ram : {a.ram, b.ram}) {
      ASSERT_EQ(sim.setBus(ram->inputBuses()[4], valueFor(1, 0)),
                Simulator::RunResult::Completed);
      sim.updateWire(ram->inputBuses()[4][0], State::HIGH, 1, {});
    }
    ASSERT_EQ(sim.run(1), Simulator::RunResult::Completed);
    EXPECT_EQ(a.output().getCurrentValue(), valueFor(8, edge == 0 ? 0x34 : 0x12));
    EXPECT_EQ(b.output().getCurrentValue(), valueFor(8, edge == 0 ? 0x12 : 0x34));
  }
  EXPECT_EQ(sim.getCurrentTime(), 2);
}

TEST(RAMTest, GeneratedClockCapturesOldReadDataAndPublishesWithoutTimeAdvance)
{
  RamFixture source(2, 1, {0});
  RamFixture target(8, 1, {0x12});
  const auto generatedClock = source.output()[0];
  target.ram->setInput(static_cast<unsigned>(RAM::Inputs::Clock), Bus{generatedClock},
                       true);
  const Bus  captured(8);
  const auto high = std::make_shared<Wire>(State::HIGH);
  const auto low  = std::make_shared<Wire>(State::LOW);
  auto       reader =
      std::make_shared<Register>(target.output(), generatedClock, high, low, captured);
  auto circuit =
      std::make_shared<Circuit>(Component_set{source.ram, target.ram, reader}, false);
  Simulator  sim(circuit);
  const auto set = [&](const Bus& bus, std::uint64_t value) {
    ASSERT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed);
  };
  set(source.input(RAM::Inputs::WriteData), 1);
  set(source.input(RAM::Inputs::WriteEnable), 3);
  set(target.input(RAM::Inputs::WriteData), 0x34);
  set(target.input(RAM::Inputs::WriteEnable), 0xFF);
  set(source.input(RAM::Inputs::Clock), 1);
  EXPECT_EQ(captured.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(sim.getCurrentTime(), 0);
}

TEST(RAMTest, CancellationOfGeneratedClockPassKeepsEarlierCommittedBatches)
{
  RamFixture source(2, 1, {0});
  RamFixture target(8, 1, {0x12});
  const auto generatedClock = source.output()[0];
  target.ram->setInput(static_cast<unsigned>(RAM::Inputs::Clock), Bus{generatedClock},
                       true);
  auto observer = std::make_shared<MemoryReadObserver>(target.output());
  auto circuit =
      std::make_shared<Circuit>(Component_set{source.ram, target.ram, observer}, false);
  Simulator  sim(circuit);
  const auto set = [&](const Bus& bus, std::uint64_t value) {
    ASSERT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed);
  };
  set(source.input(RAM::Inputs::WriteData), 1);
  set(source.input(RAM::Inputs::WriteEnable), 3);
  set(target.input(RAM::Inputs::WriteData), 0x34);
  set(target.input(RAM::Inputs::WriteEnable), 0xFF);
  bool cancel              = false;
  observer->cancel         = &cancel;
  observer->cancelWhenHigh = generatedClock;
  EXPECT_EQ(sim.setBus(source.input(RAM::Inputs::Clock), valueFor(1, 1),
                       [&] { return cancel; }),
            Simulator::RunResult::Cancelled);
  observer->cancel = nullptr;
  EXPECT_EQ(source.output().getCurrentValue(), valueFor(2, 1));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x12));
  set(source.input(RAM::Inputs::Clock), 0);
  ASSERT_EQ(sim.simulateBus(target.output()), Simulator::RunResult::Completed);
  EXPECT_EQ(source.output().getCurrentValue(), valueFor(2, 1));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x12));
}

namespace {

std::array<std::shared_ptr<Memory>, 2> memories()
{
  return {std::make_shared<RAM>(), std::make_shared<ROM>()};
}

}  // namespace

TEST(MemoryGeometryTest, AddressWidthResizesEveryAddressBusInBothDirections)
{
  for (const auto& memory : memories()) {
    SCOPED_TRACE(memory->typeName());
    const auto ram = std::dynamic_pointer_cast<RAM>(memory);
    if (ram)
      ram->setProperty("readPorts", 3);
    const auto initialInputs  = memory->inputBuses();
    const auto initialOutputs = memory->outputBuses();
    for (const auto width : {1, 5, 4, 2, 1, 6, 1}) {
      memory->setProperty("addressWidth", width);
      EXPECT_EQ(memory->memoryDepth(), std::size_t{1} << width);
      EXPECT_EQ(memory->capacityBits(), 8 * (std::size_t{1} << width));
      EXPECT_EQ(memory->getPropertyValue<int>("depthOverride"), 0);
      const auto inputs = memory->inputBuses();
      EXPECT_EQ(inputs[0].size(), width);
      EXPECT_EQ(inputs[0][0], initialInputs[0][0]);
      if (ram) {
        EXPECT_EQ(inputs[1].size(), width);
        EXPECT_EQ(inputs[1][0], initialInputs[1][0]);
        for (std::size_t port = 1; port < ram->readPortCount(); ++port) {
          const auto index = RAM::readAddressIndex(port);
          EXPECT_EQ(inputs[index].size(), width);
          EXPECT_EQ(inputs[index][0], initialInputs[index][0]);
        }
        for (std::size_t index = 2; index <= 4; ++index)
          EXPECT_EQ(inputs[index], initialInputs[index]);
      }
      EXPECT_EQ(memory->outputBuses(), initialOutputs);
    }
    EXPECT_FALSE(memory->getProperty("effectiveDepth"));
    EXPECT_FALSE(memory->getProperty("capacityBits"));
  }
}

TEST(MemoryGeometryTest, ExplicitDepthIsIndependentOfAddressWidth)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 5, 3);
    memory->setProperty("addressWidth", 2);
    EXPECT_EQ(memory->addressWidth(), 2);
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_EQ(memory->capacityBits(), 30);
    EXPECT_THROW(memory->setProperty("addressWidth", 1), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("depthOverride", 5), std::invalid_argument);
    EXPECT_EQ(memory->memoryDepth(), 3);
    memory->setGeometry(10, 2);
    EXPECT_EQ(memory->memoryDepth(), 4);
    memory->setProperty("addressWidth", 1);
    EXPECT_EQ(memory->memoryDepth(), 2);
  }
}

TEST(MemoryGeometryTest, DocumentSelectionGrowsButNeverShrinksGeometry)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(8, 2);
    memory->setBinaryDocument("original", binarySnapshot({1, 2}));
    EXPECT_EQ(memory->memoryDepth(), 4);
    const auto lowWire = memory->inputBuses()[0][0];
    memory->setBinaryDocument("larger", binarySnapshot({1, 2, 3, 4, 5}));
    EXPECT_EQ(memory->addressWidth(), 3);
    EXPECT_EQ(memory->memoryDepth(), 8);
    EXPECT_EQ(memory->getPropertyValue<int>("depthOverride"), 0);
    EXPECT_EQ(memory->inputBuses()[0][0], lowWire);
    memory->setBinaryDocument("smaller", binarySnapshot({3}));
    EXPECT_EQ(memory->addressWidth(), 3);
    EXPECT_EQ(memory->memoryDepth(), 8);
    memory->setBinaryDocument("", nullptr);
    EXPECT_EQ(memory->getPropertyValue<std::string>("initialization"),
              Memory::Uninitialized);
    EXPECT_EQ(memory->memoryDepth(), 8);
  }
}

TEST(MemoryGeometryTest, ExternalDocumentChangesReportMismatchWithoutChangingGeometry)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(8, 2);
    memory->setBinaryDocument("image", binarySnapshot({1, 2}));
    const auto inputs = memory->inputBuses();
    memory->refreshBinaryContents(binarySnapshot({1, 2, 3, 4, 5}));
    EXPECT_FALSE(memory->memoryImageSnapshot());
    EXPECT_FALSE(memory->initializationError().empty());
    EXPECT_THROW(memory->validateInitialization(), std::runtime_error);
    EXPECT_EQ(memory->memoryDepth(), 4);
    EXPECT_EQ(memory->inputBuses(), inputs);
    memory->refreshBinaryContents(binarySnapshot({1}));
    EXPECT_TRUE(memory->initializationError().empty());
    EXPECT_EQ(memory->inputBuses(), inputs);
    memory->refreshBinaryContents(nullptr);
    EXPECT_FALSE(memory->initializationError().empty());
    EXPECT_EQ(memory->inputBuses(), inputs);
  }
}

TEST(MemoryGeometryTest, ManualGeometryEditsRejectTruncationAtomically)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 3);
    memory->setBinaryDocument("packed",
                              binarySnapshot({0xA5, 0x3C, 0xFF, 0xFF, 0xFF, 0xFF}));
    const auto properties = memory->getProperties();
    const auto inputs     = memory->inputBuses();
    const auto image      = memory->memoryImageSnapshot();
    EXPECT_THROW(memory->setProperty("addressWidth", 2), std::invalid_argument);
    EXPECT_THROW(memory->setGeometry(10, 2), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("dataWidth", 5), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("depthOverride", 4), std::invalid_argument);
    EXPECT_EQ(memory->getProperties(), properties);
    EXPECT_EQ(memory->inputBuses(), inputs);
    EXPECT_EQ(memory->memoryImageSnapshot(), image);
    memory->setBinaryDocument("smaller", binarySnapshot({1}));
    memory->setProperty("addressWidth", 1);
    EXPECT_EQ(memory->memoryDepth(), 2);
  }
}

TEST(MemoryInitializationTest, PoliciesAndPaddingPreservePackedBitOrdering)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 2, 3);
    EXPECT_FALSE(memory->memoryImageSnapshot());
    memory->setProperty("initialization", std::string(Memory::Zero));
    auto image = memory->memoryImageSnapshot();
    ASSERT_TRUE(image);
    EXPECT_EQ(image->word(2), valueFor(10, 0));
    memory->setBinaryDocument("packed", binarySnapshot({0xA5, 0x3C}));
    image = memory->memoryImageSnapshot();
    EXPECT_EQ(image->word(0), valueFor(10, 0xA5));
    EXPECT_EQ(image->word(1), valueFor(10, 0xF));
    EXPECT_EQ(image->word(2), valueFor(10, 0));
    memory->setProperty("padding", std::string(Memory::Unknown));
    image = memory->memoryImageSnapshot();
    EXPECT_EQ(image->knownBitCount(), 16);
    auto partial = valueFor(10, 0xF);
    for (std::size_t bit = 6; bit < 10; ++bit)
      partial[bit] = State::UNKNOWN;
    EXPECT_EQ(image->word(1), partial);
    EXPECT_EQ(image->word(2), BusValue(10, State::UNKNOWN));
    EXPECT_EQ(image->bytes(), (std::vector<std::uint8_t>{0xA5, 0x3C}));
    memory->setProperty("initialization", std::string(Memory::Uninitialized));
    EXPECT_FALSE(memory->memoryImageSnapshot());
    memory->setProperty("initialization", std::string(Memory::BinaryDocument));
    EXPECT_EQ(memory->memoryImageSnapshot()->word(1), partial);
    memory->setBinaryDocument("empty", binarySnapshot({}));
    EXPECT_EQ(memory->memoryImageSnapshot()->knownBitCount(), 0);
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_THROW(memory->setProperty("initialization", std::string("random")),
                 std::invalid_argument);
    EXPECT_THROW(memory->setProperty("padding", std::string("random")),
                 std::invalid_argument);
  }
}

TEST(MemoryImageTest, PaddingBitsMustBeZeroAndOversizedImagesAreRejected)
{
  EXPECT_THROW((void)MemoryImage::fromBytes(std::vector<std::uint8_t>{1, 2, 0xF3}, 10, 2),
               std::invalid_argument);
  const auto image = MemoryImage::fromBytes(std::vector<std::uint8_t>{1, 2, 3}, 10, 2);
  EXPECT_EQ(image.bits().size(), 20);
  EXPECT_EQ(image.bytes(), (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_THROW((void)MemoryImage::fromBytes(std::vector<std::uint8_t>{1, 2, 3, 0}, 10, 2),
               std::invalid_argument);
  EXPECT_THROW(MemoryImage(MemoryImage::Bits{}, 0, 1), std::invalid_argument);
  EXPECT_THROW(MemoryImage(MemoryImage::Bits{}, 1, 0), std::invalid_argument);
  EXPECT_THROW(MemoryImage(MemoryImage::Bits{}, 10, 2), std::invalid_argument);
  EXPECT_THROW(
      MemoryImage(MemoryImage::Bits{}, 2, std::numeric_limits<std::size_t>::max()),
      std::invalid_argument);
}

TEST(MemoryGeometryTest, SelectionGrowsExplicitDepthWithoutChangingImportedAddressWidths)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 5, 2);
    memory->setBinaryDocument("canonical_padding", binarySnapshot({0xFF, 0xFF, 0x0F}));
    EXPECT_EQ(memory->memoryDepth(), 2);
    EXPECT_EQ(memory->addressWidth(), 5);
    memory->setBinaryDocument("larger", binarySnapshot({0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_EQ(memory->addressWidth(), 5);
    EXPECT_EQ(memory->memoryImageSnapshot()->word(2), valueFor(10, 15));
    memory->setBinaryDocument("smaller", binarySnapshot({0xFF}));
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_EQ(memory->addressWidth(), 5);
    memory->refreshBinaryContents(binarySnapshot({1, 2, 3, 4, 5}));
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_EQ(memory->addressWidth(), 5);
    EXPECT_FALSE(memory->initializationError().empty());
  }
}

TEST(MemoryGeometryTest, GeometryAndPortLimitsAreValidatedBeforeAllocation)
{
  for (const auto& memory : memories()) {
    const auto properties = memory->getProperties();
    const auto inputs     = memory->inputBuses();
    for (const auto width : {0, -1, 65536}) {
      EXPECT_THROW(memory->setProperty("dataWidth", width), std::invalid_argument);
      EXPECT_THROW(memory->setProperty("addressWidth", width), std::invalid_argument);
    }
    EXPECT_THROW(memory->setProperty("addressWidth", 63), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("addressWidth", 64), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("addressWidth", 21), std::invalid_argument);
    EXPECT_THROW(memory->setProperty("depthOverride", -1), std::invalid_argument);
    EXPECT_THROW(memory->setGeometry(8, 1, std::numeric_limits<int>::max()),
                 std::invalid_argument);
    EXPECT_THROW(memory->setGeometry(65535, 20), std::invalid_argument);
    EXPECT_THROW(memory->setMemoryImage(nullptr), std::invalid_argument);
    EXPECT_EQ(memory->getProperties(), properties);
    EXPECT_EQ(memory->inputBuses(), inputs);
    memory->setGeometry(8, 64, 3);  // Wider imported buses do not imply huge allocations.
    EXPECT_EQ(memory->memoryDepth(), 3);
    EXPECT_EQ(memory->addressWidth(), 64);
    if (const auto ram = std::dynamic_pointer_cast<RAM>(memory)) {
      EXPECT_THROW(ram->setProperty("readPorts", 65535), std::invalid_argument);
      EXPECT_EQ(ram->readPortCount(), 1);
    }
  }
}

TEST(MemoryGeometryTest,
     SerializationAndElaborationPreserveIndependentGeometryAndPolicies)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  CircuitElaborator elaborator(registry);
  for (const auto& memory : memories()) {
    for (const auto policy :
         {Memory::Uninitialized, Memory::Zero, Memory::BinaryDocument}) {
      memory->setGeometry(10, 5, 3);
      memory->setBinaryDocument("packed", binarySnapshot({0xA5, 0x3C}));
      memory->setProperty("padding", std::string(Memory::Unknown));
      memory->setProperty("initialization", std::string(policy));
      Circuit    circuit(Component_set{memory}, false);
      auto       restored = Circuit::deserialize(circuit.serialize(), registry);
      const auto copy =
          std::dynamic_pointer_cast<Memory>(restored.getComponentByVertexId(0));
      ASSERT_TRUE(copy);
      EXPECT_EQ(copy->getProperties(), memory->getProperties());
      EXPECT_EQ(copy->memoryDepth(), 3);
      EXPECT_EQ(copy->addressWidth(), 5);
      const auto inputs = copy->inputBuses();
      for (std::size_t bit = 0; bit < inputs[0].size(); ++bit)
        EXPECT_EQ(inputs[0][bit]->getId(), memory->inputBuses()[0][bit]->getId());
      copy->refreshBinaryContents(memory->binaryContentsSnapshot());
      EXPECT_EQ(copy->inputBuses(), inputs);
      const auto runtime = elaborator.elaborate(restored);
      const auto clone =
          std::dynamic_pointer_cast<Memory>(runtime->getComponentByVertexId(0));
      EXPECT_EQ(clone->getProperties(), memory->getProperties());
      EXPECT_EQ(clone->memoryDepth(), 3);
      EXPECT_EQ(clone->addressWidth(), 5);
      if (const auto image = memory->memoryImageSnapshot()) {
        ASSERT_TRUE(clone->memoryImageSnapshot());
        EXPECT_EQ(clone->memoryImageSnapshot()->word(1), image->word(1));
      } else {
        EXPECT_FALSE(clone->memoryImageSnapshot());
      }
      memory->refreshBinaryContents(binarySnapshot({0}));
      if (policy == Memory::BinaryDocument) {
        EXPECT_EQ(clone->memoryImageSnapshot()->word(0), valueFor(10, 0xA5));
      }
    }
  }
}

TEST(MemoryGeometryTest, ProjectResolverUpdatesInitializationWithoutResizing)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 4, 3);
    memory->setBinaryDocument("packed", binarySnapshot({1, 2, 3}));
    SILICON::project::ProjectContext project;
    project.setDocuments(
        {{"circuits/memory.json", Circuit(Component_set{memory}, false).serialize()},
         {"bin/packed", std::vector<std::uint8_t>{1, 2, 3}}});
    SILICON::project::ProjectCircuitResolver resolver(project, registry);
    const auto                               resolve = [&] {
      return std::dynamic_pointer_cast<Memory>(
          resolver.resolve("memory").circuit.getComponentByVertexId(0));
    };
    EXPECT_TRUE(resolve()->initializationError().empty());
    project.upsertDocument({"bin/packed", std::vector<std::uint8_t>{1, 2, 3, 4, 5}});
    EXPECT_FALSE(resolve()->initializationError().empty());
    EXPECT_EQ(resolve()->memoryDepth(), 3);
    EXPECT_EQ(resolve()->addressWidth(), 4);
    project.removeDocument("bin/packed");
    EXPECT_FALSE(resolve()->initializationError().empty());
    EXPECT_EQ(resolve()->memoryDepth(), 3);
    project.upsertDocument({"bin/packed", std::vector<std::uint8_t>{0xFF}});
    EXPECT_TRUE(resolve()->initializationError().empty());
    EXPECT_EQ(resolve()->memoryDepth(), 3);
  }
}

TEST(MemoryGeometryTest, DeserializationDoesNotInferGeometryFromSavedBuses)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 5);
    auto json = nlohmann::json::parse(Circuit(Component_set{memory}, false).serialize());
    json["components"][0]["properties"].erase("addressWidth");
    EXPECT_THROW((void)Circuit::deserialize(json.dump(), registry),
                 std::invalid_argument);
  }
}

TEST(MemoryGeometryTest, DeserializationRejectsInvalidGeometryWithoutNumericNarrowing)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  for (const auto& memory : memories()) {
    auto json = nlohmann::json::parse(Circuit(Component_set{memory}, false).serialize());
    for (const auto key : {"depthOverride", "dataWidth", "addressWidth"}) {
      for (const auto& invalid :
           {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("2"),
            nlohmann::json(nullptr), nlohmann::json(true),
            nlohmann::json(std::uint64_t{1} << 32)}) {
        auto candidate                                = json;
        candidate["components"][0]["properties"][key] = invalid;
        EXPECT_THROW((void)Circuit::deserialize(candidate.dump(), registry),
                     std::invalid_argument);
      }
    }
  }
}

TEST(MemoryInitializationTest, ZeroAndUninitializedPoliciesExecuteForBothMemoryTypes)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(8, 2);
    memory->setProperty("initialization", std::string(Memory::Zero));
    auto      circuit = std::make_shared<Circuit>(Component_set{memory}, false);
    Simulator sim(circuit);
    if (std::dynamic_pointer_cast<ROM>(memory)) {
      ASSERT_EQ(sim.setBus(memory->inputBuses()[1], valueFor(1, 1)),
                Simulator::RunResult::Completed);
    }
    ASSERT_EQ(sim.setBus(memory->inputBuses()[0], valueFor(2, 1)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(memory->outputBuses()[0].getCurrentValue(), valueFor(8, 0));
    memory->setProperty("initialization", std::string(Memory::Uninitialized));
    ASSERT_EQ(sim.setBus(memory->inputBuses()[0], valueFor(2, 2)),
              Simulator::RunResult::Completed);
    expectState(memory->outputBuses()[0], State::UNKNOWN);
  }
}

TEST(MemoryGeometryTest,
     AutomaticSelectionGrowthHandlesPackedWordWidthsAndAllocationLimits)
{
  for (const auto& memory : memories()) {
    for (const auto width : {1, 6, 10, 16}) {
      memory->setBinaryDocument("", nullptr);
      memory->setGeometry(width, 1);
      auto previousCapacity = memory->capacityBits();
      for (const auto bytes : {1, 3, 7, 1, 0}) {
        memory->setBinaryDocument("packed",
                                  binarySnapshot(std::vector<std::uint8_t>(bytes, 0xFF)));
        EXPECT_GE(memory->capacityBits(), static_cast<std::size_t>(bytes * 8));
        EXPECT_GE(memory->capacityBits(), previousCapacity);
        EXPECT_EQ(memory->memoryDepth(), std::size_t{1} << memory->addressWidth());
        EXPECT_TRUE(memory->initializationError().empty());
        previousCapacity = memory->capacityBits();
      }
    }
    memory->setBinaryDocument("", nullptr);
    memory->setGeometry(1, 1);
    const auto before = memory->getProperties();
    const auto inputs = memory->inputBuses();
    EXPECT_THROW(
        memory->setBinaryDocument("too_large", binarySnapshot(std::vector<std::uint8_t>(
                                                   Memory::MaxDepth / 8 + 1, 0xFF))),
        std::invalid_argument);
    EXPECT_EQ(memory->getProperties(), before);
    EXPECT_EQ(memory->inputBuses(), inputs);
  }
}

TEST(MemoryGeometryTest, DeserializationRejectsBusesThatDisagreeWithConfiguredGeometry)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 5, 3);
    const auto original =
        nlohmann::json::parse(Circuit(Component_set{memory}, false).serialize());
    for (const auto name : {"inputs", "outputs"}) {
      auto invalid = original;
      invalid["components"][0][name][0].erase(0);
      EXPECT_THROW((void)Circuit::deserialize(invalid.dump(), registry),
                   std::invalid_argument);
      invalid = original;
      invalid["components"][0][name].push_back(nlohmann::json::array());
      EXPECT_THROW((void)Circuit::deserialize(invalid.dump(), registry),
                   std::invalid_argument);
    }
  }
}

TEST(MemoryGeometryTest, PropertyDescriptionsKeepDerivedInformationOutOfConfiguration)
{
  for (const auto& memory : memories()) {
    const auto configuration = memory->getProperties();
    auto       fields        = memory->propertyInfo();
    ASSERT_GE(fields.size(), 5);
    EXPECT_EQ(fields[0].key, "addressWidth");
    EXPECT_EQ(fields[1].key, "dataWidth");
    EXPECT_EQ(std::ranges::find(fields, "binaryContents", &PropertyInfo::key),
              fields.end());
    EXPECT_EQ(std::ranges::find(fields, "padding", &PropertyInfo::key), fields.end());
    EXPECT_TRUE(std::ranges::find(fields, "depthOverride", &PropertyInfo::key)->advanced);
    EXPECT_TRUE(
        std::ranges::find(fields, "effectiveDepth", &PropertyInfo::key)->readOnly);
    EXPECT_TRUE(std::ranges::find(fields, "capacity", &PropertyInfo::key)->readOnly);
    EXPECT_EQ(memory->getProperties(), configuration);
    EXPECT_FALSE(memory->getProperty("effectiveDepth"));
    EXPECT_FALSE(memory->getProperty("capacity"));
    EXPECT_FALSE(memory->getProperty("depth"));
    memory->setProperty("initialization", std::string(Memory::BinaryDocument));
    fields = memory->propertyInfo();
    EXPECT_NE(std::ranges::find(fields, "binaryContents", &PropertyInfo::key),
              fields.end());
    EXPECT_NE(std::ranges::find(fields, "padding", &PropertyInfo::key), fields.end());
    memory->setGeometry(10, 5, 3);
    fields = memory->propertyInfo();
    EXPECT_FALSE(
        std::ranges::find(fields, "depthOverride", &PropertyInfo::key)->advanced);
    EXPECT_EQ(std::get<std::string>(
                  std::ranges::find(fields, "effectiveDepth", &PropertyInfo::key)->value),
              "3 words");
    EXPECT_EQ(std::get<std::string>(
                  std::ranges::find(fields, "capacity", &PropertyInfo::key)->value),
              "30 bits (4 bytes)");
  }
}

TEST(MemoryGeometryTest,
     CompoundConfigurationValidatesTogetherAndKeepsCurrentDocumentBytes)
{
  for (const auto& memory : memories()) {
    memory->setGeometry(10, 5, 3);
    memory->setBinaryDocument("image", binarySnapshot({1, 2, 3}));
    const auto before = memory->getProperties();
    memory->setGeometry(10, 2, 4);
    memory->refreshBinaryContents(binarySnapshot({1, 2, 3, 4, 5}));
    const auto current = memory->binaryContentsSnapshot();
    memory->setProperties(before);
    EXPECT_EQ(memory->getProperties(), before);
    EXPECT_EQ(memory->binaryContentsSnapshot(), current);
    EXPECT_FALSE(memory->initializationError().empty());
    EXPECT_THROW(memory->setProperties({{"addressWidth", 1}, {"depthOverride", 3}}),
                 std::invalid_argument);
    EXPECT_THROW(memory->setProperties({{"initialization", std::string("invalid")}}),
                 std::invalid_argument);
    EXPECT_EQ(memory->getProperties(), before);
  }
}
