/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
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

std::shared_ptr<const std::string> binarySnapshot(std::string contents)
{
  return std::make_shared<const std::string>(std::move(contents));
}

std::shared_ptr<ROM> configuredRom(std::string contents, const int dataWidth)
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
  rom.setBinaryDocument("program", binarySnapshot(std::string("\x12\x34\x56\x78", 4)));

  ASSERT_EQ(rom.inputBuses().size(), 3);
  ASSERT_EQ(rom.outputBuses().size(), 1);
  EXPECT_EQ(rom.inputBuses()[0].size(), 2);
  EXPECT_EQ(rom.inputBuses()[1].size(), 1);
  EXPECT_EQ(rom.inputBuses()[2].size(), 1);
  EXPECT_EQ(rom.outputBuses()[0].size(), 10);

  EXPECT_THROW(rom.setProperty("dataWidth", 6), std::invalid_argument);
  EXPECT_EQ(rom.getPropertyValue<int>("dataWidth"), 10);
}

TEST(ROMTest, ReadsPackedLittleEndianWordsAcrossByteBoundaries)
{
  auto      rom     = configuredRom(std::string("\xA5\x3C\xF0\xC1", 4), 10);
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
  auto               rom = configuredRom(std::string("\x11\x22\x33\x44", 4), 8);
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

TEST(ROMTest, OneWordRomIgnoresAddressState)
{
  auto               rom     = configuredRom(std::string("\xA6", 1), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  Bus       address = rom->inputBuses()[0];
  const Bus cs      = rom->inputBuses()[1];
  const Bus data    = rom->outputBuses()[0];
  ASSERT_EQ(address.size(), 1);

  address.forceSetCurrentValue(BusValue{State::ERROR});
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0xA6));

  ASSERT_EQ(simulator.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0xA6));
}

TEST(ROMTest, MissingAndExternallyInvalidatedContentIsUnknown)
{
  auto               rom = configuredRom(std::string("\x10\x20\x30\x40", 4), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  Bus cs = rom->inputBuses()[1];
  ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);

  rom->refreshBinaryContents(binarySnapshot(std::string("\x10\x20\x30", 3)));
  rom->simulate(simulator);
  EXPECT_EQ(rom->inputBuses()[0].size(), 1);
  expectState(rom->outputBuses()[0], State::UNKNOWN);

  rom->refreshBinaryContents(nullptr);
  rom->simulate(simulator);
  expectState(rom->outputBuses()[0], State::UNKNOWN);

  rom->refreshBinaryContents(binarySnapshot(std::string("\x10\x20", 2)));
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

TEST(ROMTest, BinaryDocumentChangeIsTransactionalAndClearsStaleContent)
{
  auto rom = configuredRom(std::string("\x10\x20\x30\x40", 4), 8);
  const auto originalSnapshot = rom->binaryContentsSnapshot();
  const auto originalInputs   = rom->inputBuses();

  EXPECT_THROW(
      rom->setBinaryDocument("invalid", binarySnapshot(std::string("\x10\x20\x30", 3))),
      std::invalid_argument);
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "program");
  EXPECT_EQ(rom->binaryContentsSnapshot(), originalSnapshot);
  EXPECT_EQ(rom->inputBuses(), originalInputs);

  rom->setProperty("binaryContents", std::string("missing"));
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "missing");
  EXPECT_EQ(rom->binaryContentsSnapshot(), nullptr);
  EXPECT_EQ(rom->inputBuses()[0].size(), 1);
}

TEST(ROMTest, SerializationPreservesWiringAndExplicitHydrationFeedsRuntimeSnapshot)
{
  auto    rom = configuredRom(std::string("\x5A\xC3\x7E\x81", 4), 8);
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
      binarySnapshot(std::string("\x00\x00\x00\x00", 4)));
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
