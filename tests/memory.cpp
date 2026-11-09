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
#include <stdexcept>
#include <string>
#include <string_view>

#include <core/circuit.hpp>
#include <core/memory.hpp>
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
