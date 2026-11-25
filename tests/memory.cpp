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

std::shared_ptr<Memory> configuredRam()
{
  auto memory = std::make_shared<Memory>();
  memory->setProperty("type", std::string("RAM"));
  return memory;
}

std::shared_ptr<Memory> configuredRom(std::vector<std::uint8_t> contents,
                                      const int                 dataWidth)
{
  auto rom = std::make_shared<Memory>();
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

TEST(MemoryTest, DefaultsToRomAndExposesTypeChoices)
{
  Memory memory;
  EXPECT_EQ(memory.typeName(), "Memory");
  EXPECT_EQ(memory.metadata().displayName, "Memory");
  EXPECT_EQ(memory.getPropertyValue<std::string>("type"), "ROM");
  const auto choices = memory.getStringPropertyOptions("type");
  ASSERT_TRUE(choices);
  EXPECT_EQ(choices->get(), (StringPropertyOptions{"ROM", "RAM"}));
  EXPECT_FALSE(memory.isWritable());
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
  EXPECT_FALSE(memory.usesStagedSequentialOutputs());
  EXPECT_EQ(memory.inputBuses().size(), 1);
  EXPECT_EQ(memory.outputBuses().size(), 1);
  const auto info = memory.propertyInfo();
  ASSERT_FALSE(info.empty());
  EXPECT_EQ(info.front().key, "type");
  EXPECT_EQ(std::ranges::find(info, "initialization", &PropertyInfo::key), info.end());
  EXPECT_NE(std::ranges::find(info, "binaryContents", &PropertyInfo::key), info.end());
  EXPECT_NE(std::ranges::find(info, "padding", &PropertyInfo::key), info.end());
}

TEST(MemoryTest, RomAlwaysUsesBinaryDocumentInitialization)
{
  Memory memory;
  memory.setProperty("initialization", std::string(Memory::Zero));
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
  EXPECT_FALSE(memory.initializationError().empty());
  EXPECT_THROW(memory.validateInitialization(), std::runtime_error);

  memory.setProperty("type", std::string("RAM"));
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::Uninitialized);
  memory.setProperty("initialization", std::string(Memory::Zero));
  const auto ramFields = memory.propertyInfo();
  EXPECT_NE(std::ranges::find(ramFields, "initialization", &PropertyInfo::key),
            ramFields.end());

  memory.setProperty("type", std::string("ROM"));
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
  EXPECT_FALSE(memory.memoryImageSnapshot());
  memory.setBinaryDocument("program", binarySnapshot({0xA5}));
  memory.setProperties({{"initialization", std::string(Memory::Uninitialized)}});
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
  ASSERT_TRUE(memory.memoryImageSnapshot());
  EXPECT_EQ(memory.memoryImageSnapshot()->word(0), valueFor(8, 0xA5));

  memory.setBinaryDocument("", nullptr);
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
  EXPECT_THROW(memory.validateInitialization(), std::runtime_error);
  memory.setProperties({{"type", std::string("RAM")}});
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::Uninitialized);
  memory.setProperties(
      {{"type", std::string("ROM")}, {"initialization", std::string(Memory::Zero)}});
  EXPECT_EQ(memory.getPropertyValue<std::string>("initialization"),
            Memory::BinaryDocument);
}

TEST(MemoryTest, TypeSwitchAddsWritePortAndPreservesAllReadConnections)
{
  Memory memory;
  memory.setProperty("readPorts", 3);
  memory.setGeometry(10, 4, 3);
  memory.setBinaryDocument("program", binarySnapshot({0xA5, 0x3C}));
  const auto addresses = memory.inputBuses();
  const auto outputs   = memory.outputBuses();
  for (const auto type : {"RAM", "ROM", "RAM", "ROM"}) {
    memory.setProperty("type", std::string(type));
    const bool writable = std::string_view(type) == "RAM";
    EXPECT_EQ(memory.isWritable(), writable);
    EXPECT_EQ(memory.usesStagedSequentialOutputs(), writable);
    ASSERT_EQ(memory.inputBuses().size(), writable ? 7 : 3);
    for (std::size_t port = 0; port < addresses.size(); ++port)
      EXPECT_EQ(memory.inputBuses()[port], addresses[port]);
    EXPECT_EQ(memory.outputBuses(), outputs);
    EXPECT_EQ(memory.memoryDepth(), 3);
    EXPECT_EQ(memory.memoryImageSnapshot()->word(0), valueFor(10, 0xA5));
    if (writable) {
      EXPECT_EQ(memory.inputBuses()[memory.writeInputIndex(Memory::WriteInputs::Address)]
                    .size(),
                4);
      EXPECT_EQ(
          memory.inputBuses()[memory.writeInputIndex(Memory::WriteInputs::Data)].size(),
          10);
      EXPECT_EQ(
          memory.inputBuses()[memory.writeInputIndex(Memory::WriteInputs::Enable)].size(),
          10);
      EXPECT_EQ(
          memory.inputBuses()[memory.writeInputIndex(Memory::WriteInputs::Clock)].size(),
          1);
    }
  }
}

TEST(MemoryTest, ReadPortCountChangesPreserveReadAndWriteConnections)
{
  for (const auto type : {"ROM", "RAM"}) {
    Memory memory;
    memory.setProperty("type", std::string(type));
    memory.setGeometry(8, 3);
    const std::array<Bus, 4> writeInputs{Bus(3), Bus(8), Bus(8), Bus(1)};
    if (memory.isWritable())
      for (std::size_t input = 0; input < writeInputs.size(); ++input)
        memory.setInput(memory.readPortCount() + input, writeInputs[input], true);

    for (const auto count : {3, 1, 5, 2, 6, 1}) {
      const auto oldInputs  = memory.inputBuses();
      const auto oldOutputs = memory.outputBuses();
      // Exercise both individual property edits and compound/undo-style updates.
      if (count % 2 == 0)
        memory.setProperties({{"readPorts", count}});
      else
        memory.setProperty("readPorts", count);
      const auto inputs  = memory.inputBuses();
      const auto outputs = memory.outputBuses();
      ASSERT_EQ(inputs.size(), count + (memory.isWritable() ? 4 : 0));
      ASSERT_EQ(outputs.size(), count);
      for (std::size_t port = 0; port < std::min(outputs.size(), oldOutputs.size());
           ++port) {
        EXPECT_EQ(inputs[port], oldInputs[port]);
        EXPECT_EQ(outputs[port], oldOutputs[port]);
      }
      for (std::size_t port = 0; port < outputs.size(); ++port) {
        EXPECT_EQ(inputs[port].size(), 3);
        EXPECT_EQ(outputs[port].size(), 8);
        if (memory.isWritable()) {
          for (const auto& writeInput : writeInputs)
            for (const auto& addressWire : inputs[port])
              EXPECT_EQ(std::ranges::find(writeInput, addressWire), writeInput.end());
        }
      }
      if (memory.isWritable()) {
        for (std::size_t input = 0; input < writeInputs.size(); ++input)
          EXPECT_EQ(inputs[count + input], writeInputs[input]);
      }
    }
  }
}

TEST(MemoryTest, WritesUsePreservedConnectionsAfterReadPortCountChanges)
{
  const auto memory = configuredRam();
  memory->setGeometry(8, 2);
  memory->setProperty("initialization", std::string(Memory::Zero));
  const auto original = memory->inputBuses();
  for (const auto count : {3, 1, 5, 2}) {
    memory->setProperty("readPorts", count);
    auto       circuit = std::make_shared<Circuit>(Component_set{memory}, false);
    Simulator  sim(circuit);
    const auto set = [&](const Bus& bus, std::uint64_t value) {
      ASSERT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed);
    };
    for (std::size_t port = 0; port < memory->readPortCount(); ++port)
      set(memory->inputBuses()[port], port % 4);
    // Drive the original write wires after their bus indices have moved.
    set(original[4], 0);
    set(original[1], 1);
    set(original[2], 0xA5);
    set(original[3], 0x0F);
    set(original[4], 1);
    for (std::size_t port = 0; port < memory->readPortCount(); ++port)
      EXPECT_EQ(memory->outputBuses()[port].getCurrentValue(),
                valueFor(8, port % 4 == 1 ? 5 : 0));
  }
}

TEST(MemoryTest, RejectsInvalidTypeAtomicallyForSingleAndBulkEdits)
{
  Memory     memory;
  const auto properties = memory.getProperties();
  const auto inputs     = memory.inputBuses();
  EXPECT_THROW(memory.setProperty("type", std::string("invalid")), std::invalid_argument);
  auto invalid            = properties;
  invalid.at("type")      = std::string("invalid");
  invalid.at("readPorts") = 3;
  EXPECT_THROW(memory.setProperties(invalid), std::invalid_argument);
  EXPECT_EQ(memory.getProperties(), properties);
  EXPECT_EQ(memory.inputBuses(), inputs);
}

TEST(MemoryTest, BulkEditsRestoreModeAndReadPortCount)
{
  Memory     memory;
  const auto romProperties = memory.getProperties();
  memory.setProperties({{"type", std::string("RAM")}, {"readPorts", 2}});
  EXPECT_TRUE(memory.isWritable());
  EXPECT_EQ(memory.inputBuses().size(), 6);
  EXPECT_EQ(memory.outputBuses().size(), 2);
  memory.setProperties(romProperties);
  EXPECT_FALSE(memory.isWritable());
  EXPECT_EQ(memory.inputBuses().size(), 1);
  EXPECT_EQ(memory.outputBuses().size(), 1);
}

TEST(ROMTest, GeometryRemainsConfiguredWhenContentsAndWordWidthChange)
{
  Memory rom;
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
  Memory rom;
  rom.setGeometry(10, 4);
  rom.setProperty("binaryContents", std::string("packed"));
  const auto inputs = rom.inputBuses();
  rom.refreshBinaryContents(binarySnapshot({0xA5, 0x3C, 0x0F}));
  EXPECT_EQ(rom.inputBuses(), inputs);
  EXPECT_EQ(rom.memoryDepth(), 16);
}

TEST(ROMTest, ExplicitOneWordImageUsesValidAddressBus)
{
  Memory rom;
  rom.setProperty("dataWidth", 8);
  rom.setInputs({Bus(1)});
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
  const Bus data    = rom->outputBuses()[0];

  const std::array<std::uint64_t, 4> expected{0x0A5, 0x00F, 0x01F, 0x003};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    ASSERT_EQ(simulator.setBus(address, valueFor(address, index)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, expected[index])) << index;
  }
}

TEST(ROMTest, ReadsEveryAddressWithoutControlInputs)
{
  auto memory = configuredRom({0x11, 0x22, 0x33, 0x44}, 8);
  ASSERT_EQ(memory->inputBuses().size(), 1);
  auto      circuit = std::make_shared<Circuit>(Component_set{memory});
  Simulator simulator(circuit);
  for (unsigned address = 0; address < 4; ++address) {
    ASSERT_EQ(simulator.setBus(memory->inputBuses()[0], valueFor(2, address)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(memory->outputBuses()[0].getCurrentValue(),
              valueFor(8, 0x11 * (address + 1)));
  }
}

TEST(ROMTest, OneWordRomChecksAddressStateAndBounds)
{
  auto               rom     = configuredRom((std::vector<std::uint8_t>{0xA6}), 8);
  auto               circuit = std::make_shared<Circuit>(Component_set{rom});
  Simulator          simulator(circuit);

  Bus       address = rom->inputBuses()[0];
  const Bus data    = rom->outputBuses()[0];
  ASSERT_EQ(address.size(), 1);

  ASSERT_EQ(simulator.setBus(address, BusValue{State::ERROR}),
            Simulator::RunResult::Completed);
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
  const auto address = rom->inputBuses()[0];
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

TEST(ROMTest, MissingBinaryDocumentPreventsSimulation)
{
  auto memory = std::make_shared<Memory>();
  memory->setBinaryDocument("missing", nullptr);
  auto circuit = std::make_shared<Circuit>(Component_set{memory});
  EXPECT_THROW(Simulator simulator(circuit), std::runtime_error);
}

TEST(MemoryTest, ReadPortsPreservePackedPaddingAndFourStateAddressesInBothModes)
{
  for (const auto type : {"ROM", "RAM"}) {
    for (const auto padding : {Memory::Zero, Memory::Unknown}) {
      SCOPED_TRACE(std::string(type) + "/" + std::string(padding));
      auto memory = std::make_shared<Memory>();
      memory->setProperty("type", std::string(type));
      memory->setProperty("readPorts", 3);
      // Explicit depth also permits addresses wider than a machine word.
      memory->setGeometry(10, 65, 3);
      memory->setBinaryDocument("packed", binarySnapshot({0xA5, 0x3C}));
      memory->setProperty("padding", std::string(padding));
      auto      circuit = std::make_shared<Circuit>(Component_set{memory}, false);
      Simulator sim(circuit);

      auto partial = valueFor(10, 0xF);
      if (padding == Memory::Unknown)
        for (std::size_t bit = 6; bit < 10; ++bit)
          partial[bit] = State::UNKNOWN;
      const std::array<BusValue, 3> expected{
          valueFor(10, 0xA5), partial,
          BusValue(10, padding == Memory::Unknown ? State::UNKNOWN : State::LOW)};
      for (std::size_t port = 0; port < 3; ++port) {
        const auto& address = memory->inputBuses()[port];
        ASSERT_EQ(sim.setBus(address, valueFor(65, port)),
                  Simulator::RunResult::Completed);
      }
      for (std::size_t port = 0; port < 3; ++port)
        EXPECT_EQ(memory->outputBuses()[port].getCurrentValue(), expected[port]);

      const auto& address = memory->inputBuses()[1];
      for (const auto state : {State::UNKNOWN, State::ERROR}) {
        auto value = valueFor(65, 0);
        value[0]   = state;
        ASSERT_EQ(sim.setBus(address, value), Simulator::RunResult::Completed);
        expectState(memory->outputBuses()[1], state);
      }
      for (const auto bit : {0, 64}) {
        auto value = valueFor(65, bit == 0 ? 3 : 0);
        if (bit == 64)
          value[64] = State::HIGH;
        ASSERT_EQ(sim.setBus(address, value), Simulator::RunResult::Completed);
        expectState(memory->outputBuses()[1], State::UNKNOWN);
      }
      EXPECT_EQ(memory->outputBuses()[0].getCurrentValue(), expected[0]);
      EXPECT_EQ(memory->outputBuses()[2].getCurrentValue(), expected[2]);

      memory->refreshBinaryContents(binarySnapshot({}));
      ASSERT_EQ(sim.simulateBus(memory->outputBuses()[0]),
                Simulator::RunResult::Completed);
      for (const auto port : {0, 2})
        expectState(memory->outputBuses()[port],
                    padding == Memory::Unknown ? State::UNKNOWN : State::LOW);
      memory->refreshBinaryContents(binarySnapshot({1, 2, 3, 4, 5}));
      EXPECT_THROW(memory->simulate(sim), std::runtime_error);
    }
  }
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
  if (!ComponentRegistry::instance().hasType(Memory::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto restored =
      std::make_shared<Circuit>(Circuit::deserialize(original.serialize(), registry));
  const auto restoredComponent =
      std::dynamic_pointer_cast<Memory>(restored->getComponentByVertexId(0));
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
  const Bus data    = restoredComponent->outputBuses()[0];
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
  const auto metadata = registry.metadata(Memory::Type);
  EXPECT_EQ(metadata.category, ComponentCategory::Memory);
  EXPECT_EQ(componentCategoryName(metadata.category), "Memory");
}

namespace {

struct RamFixture {
  std::shared_ptr<Memory>    ram = configuredRam();
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
    set(0, 0);
    set(Memory::WriteInputs::Address, 0);
    set(Memory::WriteInputs::Data, 0);
    set(Memory::WriteInputs::Enable, 0);
    set(Memory::WriteInputs::Clock, 0);
  }

  Bus  input(Memory::WriteInputs port) const { return input(ram->writeInputIndex(port)); }
  Bus  input(std::size_t port) const { return ram->inputBuses()[port]; }
  Bus  output() const { return ram->outputBuses()[0]; }
  void set(Memory::WriteInputs port, std::uint64_t value)
  {
    EXPECT_EQ(sim->setBus(input(port), valueFor(input(port), value)),
              Simulator::RunResult::Completed);
  }
  void set(Memory::WriteInputs port, const BusValue& value)
  {
    EXPECT_EQ(sim->setBus(input(port), value), Simulator::RunResult::Completed);
  }
  void set(std::size_t port, std::uint64_t value)
  {
    EXPECT_EQ(sim->setBus(input(port), valueFor(input(port), value)), Simulator::RunResult::Completed);
  }
  void set(std::size_t port, const BusValue& value)
  {
    EXPECT_EQ(sim->setBus(input(port), value), Simulator::RunResult::Completed);
  }
  void pulse()
  {
    set(Memory::WriteInputs::Clock, 0);
    set(Memory::WriteInputs::Clock, 1);
  }
};

} // namespace

TEST(RAMTest, ValidatesGeometryAndBinaryImages)
{
  Memory ram;
  ram.setProperty("type", std::string("RAM"));
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
    f.set(0, i);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(f.output(), values[i]));
  }
  f.set(0, 3);
  expectState(f.output(), State::UNKNOWN);
  f.set(0, BusValue{State::UNKNOWN, State::LOW});
  expectState(f.output(), State::UNKNOWN);
  f.set(0, BusValue{State::UNKNOWN, State::ERROR});
  expectState(f.output(), State::ERROR);
}

TEST(RAMTest, UninitializedAndSingleWordMemories)
{
  RamFixture f(1, 1);
  expectState(f.output(), State::UNKNOWN);
  f.set(Memory::WriteInputs::Data, 1);
  f.set(Memory::WriteInputs::Enable, 1);
  f.pulse();
  expectState(f.output(), State::HIGH);
  f.set(Memory::WriteInputs::Address, 1);
  f.set(Memory::WriteInputs::Data, 0);
  f.pulse();
  expectState(f.output(), State::HIGH);
  f.set(0, 1);
  expectState(f.output(), State::UNKNOWN);
}

TEST(RAMTest, IndependentAddressesMaskedWritesAndUnchangedReadAddress)
{
  RamFixture f(8, 3, {0x12, 0x34, 0x56});
  const auto initial = f.ram->binaryContentsSnapshot();
  f.set(Memory::WriteInputs::Address, 1);
  f.set(Memory::WriteInputs::Data, 0xAB);
  f.set(Memory::WriteInputs::Enable, 0x0F);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.set(0, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x3B));
  f.set(Memory::WriteInputs::Data, 0xC0);
  f.set(Memory::WriteInputs::Enable, 0xF0);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xCB));
  EXPECT_EQ(f.ram->binaryContentsSnapshot(), initial);
  EXPECT_EQ(*initial, (std::vector<std::uint8_t>{0x12, 0x34, 0x56}));
  f.set(Memory::WriteInputs::Address, 3);
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xCB));
}

TEST(MemoryTest, SwitchingBetweenRamAndRomRestoresInitializationAfterWrites)
{
  RamFixture f(8, 2, {0x12, 0x34});
  for (const auto type : {"RAM", "ROM", "RAM", "ROM"}) {
    f.ram->setProperty("type", std::string(type));
    f.sim = std::make_unique<Simulator>(f.circuit);
    f.set(0, 0);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
    if (f.ram->isWritable()) {
      f.set(Memory::WriteInputs::Address, 0);
      f.set(Memory::WriteInputs::Data, 0x56);
      f.set(Memory::WriteInputs::Enable, 0xFF);
      f.pulse();
      EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));
    }
    EXPECT_EQ(f.ram->memoryImageSnapshot()->word(0), valueFor(8, 0x12));
  }
}

TEST(MemoryTest, SharedImagesKeepRomAndWritableInstancesIndependent)
{
  const auto image = std::make_shared<const MemoryImage>(
      MemoryImage::fromBytes(std::vector<std::uint8_t>{0x12, 0x34}, 8, 2));
  const std::array memories{std::make_shared<Memory>(), configuredRam(), configuredRam()};
  Component_set    components;
  for (const auto& memory : memories) {
    memory->setMemoryImage(image);
    EXPECT_EQ(memory->memoryImageSnapshot(), image);
    components.insert(memory);
  }
  auto       circuit = std::make_shared<Circuit>(components, false);
  Simulator  sim(circuit);
  const auto set = [&](const std::shared_ptr<Memory>& memory, std::size_t input,
                       std::uint64_t value) {
    const auto& bus = memory->inputBuses()[input];
    ASSERT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed);
  };
  for (const auto& memory : memories)
    set(memory, 0, 0);
  for (std::size_t index = 1; index < memories.size(); ++index) {
    const auto& memory = memories[index];
    set(memory, memory->writeInputIndex(Memory::WriteInputs::Address), 0);
    set(memory, memory->writeInputIndex(Memory::WriteInputs::Data), 0x50 + index);
    set(memory, memory->writeInputIndex(Memory::WriteInputs::Enable), 0xFF);
    set(memory, memory->writeInputIndex(Memory::WriteInputs::Clock), 0);
    set(memory, memory->writeInputIndex(Memory::WriteInputs::Clock), 1);
    EXPECT_EQ(memories[0]->outputBuses()[0].getCurrentValue(), valueFor(8, 0x12));
    EXPECT_EQ(memories[1]->outputBuses()[0].getCurrentValue(), valueFor(8, 0x51));
    EXPECT_EQ(memories[2]->outputBuses()[0].getCurrentValue(),
              valueFor(8, index == 1 ? 0x12 : 0x52));
    EXPECT_EQ(image->word(0), valueFor(8, 0x12));
  }
}

TEST(RAMTest, WritesOnlyOnClockEdgesAndDoesNotResetDuringFullEvaluation)
{
  RamFixture f(8, 1, {0x12});
  f.set(Memory::WriteInputs::Data, 0x34);
  f.set(Memory::WriteInputs::Enable, 0xFF);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  f.set(Memory::WriteInputs::Data, 0x56);
  f.set(Memory::WriteInputs::Clock, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  f.set(Memory::WriteInputs::Clock, 0);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(f.sim->simulateBus(f.input(0)), Simulator::RunResult::Completed);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
}

TEST(RAMTest, UnknownMasksAndDataPreserveFourStates)
{
  RamFixture f(4, 1, {0x05});
  f.set(Memory::WriteInputs::Data,
        BusValue{State::HIGH, State::HIGH, State::UNKNOWN, State::ERROR});
  f.set(Memory::WriteInputs::Enable,
        BusValue{State::UNKNOWN, State::UNKNOWN, State::HIGH, State::HIGH});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), (BusValue{State::HIGH, State::UNKNOWN, State::UNKNOWN, State::ERROR}));
  f.set(Memory::WriteInputs::Enable,
        BusValue{State::ERROR, State::LOW, State::LOW, State::LOW});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue()[0], State::ERROR);
}

TEST(RAMTest, UnknownWriteAddressOnlyAffectsPossibleWordsAndEnabledBits)
{
  RamFixture f(8, 3, {0x55, 0x55, 0xAA});
  f.set(Memory::WriteInputs::Address, BusValue{State::UNKNOWN, State::LOW});
  f.set(Memory::WriteInputs::Enable, 1);
  f.pulse();
  auto expected = valueFor(8, 0x55);
  expected[0] = State::UNKNOWN;
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(0, 1);
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(0, 2);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0xAA));
  f.set(Memory::WriteInputs::Address, BusValue{State::UNKNOWN, State::HIGH});
  f.set(Memory::WriteInputs::Data, 1);
  f.pulse();
  expected = valueFor(8, 0xAA);
  expected[0] = State::UNKNOWN;
  EXPECT_EQ(f.output().getCurrentValue(), expected);
  f.set(Memory::WriteInputs::Enable, 0);
  f.set(Memory::WriteInputs::Address, BusValue{State::ERROR, State::ERROR});
  f.pulse();
  EXPECT_EQ(f.output().getCurrentValue(), expected);
}

TEST(RAMTest, AmbiguousClockMergesPossibleWrites)
{
  RamFixture f(4, 1, {5});
  f.set(Memory::WriteInputs::Data, 7);
  f.set(Memory::WriteInputs::Enable, 15);
  f.set(Memory::WriteInputs::Clock, BusValue{State::UNKNOWN});
  EXPECT_EQ(f.output().getCurrentValue(), (BusValue{State::HIGH, State::UNKNOWN, State::HIGH, State::LOW}));
  f.set(Memory::WriteInputs::Clock, 0);
  f.set(Memory::WriteInputs::Data, 0);
  f.set(Memory::WriteInputs::Enable, 1);
  f.set(Memory::WriteInputs::Clock, BusValue{State::ERROR});
  EXPECT_EQ(f.output().getCurrentValue()[0], State::ERROR);
}

TEST(RAMTest, SamplesRegisterOutputsAndReadDataBeforeTheSameEdge)
{
  auto ram = configuredRam();
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
  const auto address1 = f.ram->inputBuses()[1];
  const auto clock    = f.input(Memory::WriteInputs::Clock)[0];
  const auto high     = std::make_shared<Wire>(State::HIGH);
  const auto low      = std::make_shared<Wire>(State::LOW);
  const Bus  captured0(8), captured1(8);
  auto       reader0 = std::make_shared<Register>(read0, clock, high, low, captured0);
  auto       reader1 = std::make_shared<Register>(read1, clock, high, low, captured1);
  f.circuit = std::make_shared<Circuit>(Component_set{f.ram, reader0, reader1}, false);
  f.sim     = std::make_unique<Simulator>(f.circuit);
  f.set(0, 0);
  ASSERT_EQ(f.sim->setBus(address1, valueFor(1, 1)), Simulator::RunResult::Completed);
  f.set(Memory::WriteInputs::Address, 1);
  f.set(Memory::WriteInputs::Data, 0x56);
  f.set(Memory::WriteInputs::Enable, 0xFF);
  f.pulse();
  EXPECT_EQ(read0.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(read1.getCurrentValue(), valueFor(8, 0x56));
  EXPECT_EQ(captured0.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(captured1.getCurrentValue(), valueFor(8, 0x34));

  f.set(0, 1);
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
  if (!ComponentRegistry::instance().hasType(Memory::Type))
    registerAllComponents(ComponentRegistry::instance());
  RamFixture f(8, 1, {0x12});
  // A runtime mutation on the source component must not be cloned into a session.
  f.set(Memory::WriteInputs::Enable, 0xFF);
  f.set(Memory::WriteInputs::Data, 0x34);
  f.pulse();
  {
    Session session(f.circuit);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
    EXPECT_EQ(session.setBus(f.input(Memory::WriteInputs::Clock), valueFor(1, 0)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(session.setBus(f.input(Memory::WriteInputs::Clock), valueFor(1, 1)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
    f.ram->refreshBinaryContents(binarySnapshot({0x56}));
    EXPECT_EQ(session.setBus(f.input(0), valueFor(1, 0)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
  }
  Session replacement(f.circuit);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));

  auto copy = std::make_shared<Circuit>(Circuit::deserialize(f.circuit->serialize(), registry));
  auto other = std::dynamic_pointer_cast<Memory>(copy->getComponentByVertexId(0));
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
  if (!ComponentRegistry::instance().hasType(Memory::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto ram = configuredRam();
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
  auto ram = configuredRam();
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
  auto ram = configuredRam();
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
  const auto  data   = f.input(Memory::WriteInputs::Data);
  const auto  clock  = f.input(Memory::WriteInputs::Clock)[0];
  auto        invert = std::make_shared<NotGate>(read[0], data[0]);
  auto gate = std::make_shared<AndGate>(std::vector<Wire_ptr>{read[0], clock}, data[1]);
  invert->setProperty("delay", 0);
  gate->setProperty("delay", 0);
  f.circuit = std::make_shared<Circuit>(Component_set{f.ram, invert, gate}, false);
  f.sim     = std::make_unique<Simulator>(f.circuit);
  f.set(Memory::WriteInputs::Enable, 3);
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
  f.set(Memory::WriteInputs::Address, 1);
  f.set(Memory::WriteInputs::Data, 0x34);
  f.set(Memory::WriteInputs::Enable, 0xFF);

  observer->fail = true;
  EXPECT_THROW(f.sim->setBus(f.input(Memory::WriteInputs::Clock), valueFor(1, 1)),
               std::runtime_error);
  observer->fail = false;
  f.set(Memory::WriteInputs::Clock, 0);
  f.set(0, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x56));

  f.set(0, 0);
  const auto before = observer->evaluations;
  f.set(Memory::WriteInputs::Clock, 1);
  // The clock's forward cone runs once. Writing an unread word produces no
  // output transition and must not schedule an owner/cone reevaluation.
  EXPECT_EQ(observer->evaluations, before + 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x12));
  f.set(0, 1);
  EXPECT_EQ(f.output().getCurrentValue(), valueFor(8, 0x34));
}

TEST(RAMTest, SimultaneousIndependentClocksExchangePreEdgeReadData)
{
  RamFixture a(8, 1, {0x12});
  RamFixture b(8, 1, {0x34});
  a.ram->setInput(a.ram->writeInputIndex(Memory::WriteInputs::Data), b.output(), true);
  b.ram->setInput(b.ram->writeInputIndex(Memory::WriteInputs::Data), a.output(), true);
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
  target.ram->setInput(target.ram->writeInputIndex(Memory::WriteInputs::Clock),
                       Bus{generatedClock}, true);
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
  set(source.input(Memory::WriteInputs::Data), 1);
  set(source.input(Memory::WriteInputs::Enable), 3);
  set(target.input(Memory::WriteInputs::Data), 0x34);
  set(target.input(Memory::WriteInputs::Enable), 0xFF);
  set(source.input(Memory::WriteInputs::Clock), 1);
  EXPECT_EQ(captured.getCurrentValue(), valueFor(8, 0x12));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x34));
  EXPECT_EQ(sim.getCurrentTime(), 0);
}

TEST(RAMTest, CancellationOfGeneratedClockPassKeepsEarlierCommittedBatches)
{
  RamFixture source(2, 1, {0});
  RamFixture target(8, 1, {0x12});
  const auto generatedClock = source.output()[0];
  target.ram->setInput(target.ram->writeInputIndex(Memory::WriteInputs::Clock),
                       Bus{generatedClock}, true);
  auto observer = std::make_shared<MemoryReadObserver>(target.output());
  auto circuit =
      std::make_shared<Circuit>(Component_set{source.ram, target.ram, observer}, false);
  Simulator  sim(circuit);
  const auto set = [&](const Bus& bus, std::uint64_t value) {
    ASSERT_EQ(sim.setBus(bus, valueFor(bus, value)), Simulator::RunResult::Completed);
  };
  set(source.input(Memory::WriteInputs::Data), 1);
  set(source.input(Memory::WriteInputs::Enable), 3);
  set(target.input(Memory::WriteInputs::Data), 0x34);
  set(target.input(Memory::WriteInputs::Enable), 0xFF);
  bool cancel              = false;
  observer->cancel         = &cancel;
  observer->cancelWhenHigh = generatedClock;
  EXPECT_EQ(sim.setBus(source.input(Memory::WriteInputs::Clock), valueFor(1, 1),
                       [&] { return cancel; }),
            Simulator::RunResult::Cancelled);
  observer->cancel = nullptr;
  EXPECT_EQ(source.output().getCurrentValue(), valueFor(2, 1));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x12));
  set(source.input(Memory::WriteInputs::Clock), 0);
  ASSERT_EQ(sim.simulateBus(target.output()), Simulator::RunResult::Completed);
  EXPECT_EQ(source.output().getCurrentValue(), valueFor(2, 1));
  EXPECT_EQ(target.output().getCurrentValue(), valueFor(8, 0x12));
}

namespace {

std::array<std::shared_ptr<Memory>, 2> memories()
{
  return {configuredRam(), std::make_shared<Memory>()};
}

}  // namespace

TEST(MemoryGeometryTest, AddressWidthResizesEveryAddressBusInBothDirections)
{
  for (const auto& memory : memories()) {
    SCOPED_TRACE(memory->typeName());
    const auto ram = memory->isWritable() ? memory : nullptr;
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
        const auto writeAddress = ram->writeInputIndex(Memory::WriteInputs::Address);
        EXPECT_EQ(inputs[writeAddress].size(), width);
        EXPECT_EQ(inputs[writeAddress][0], initialInputs[writeAddress][0]);
        for (std::size_t port = 1; port < ram->readPortCount(); ++port) {
          EXPECT_EQ(inputs[port].size(), width);
          EXPECT_EQ(inputs[port][0], initialInputs[port][0]);
        }
        for (std::size_t index = writeAddress + 1; index < inputs.size(); ++index)
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
              memory->isWritable() ? Memory::Uninitialized : Memory::BinaryDocument);
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
    if (memory->isWritable()) {
      memory->setProperty("initialization", std::string(Memory::Zero));
      ASSERT_TRUE(memory->memoryImageSnapshot());
      EXPECT_EQ(memory->memoryImageSnapshot()->word(2), valueFor(10, 0));
    }
    memory->setBinaryDocument("packed", binarySnapshot({0xA5, 0x3C}));
    auto image = memory->memoryImageSnapshot();
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
    if (memory->isWritable()) {
      memory->setProperty("initialization", std::string(Memory::Uninitialized));
      EXPECT_FALSE(memory->memoryImageSnapshot());
      memory->setProperty("initialization", std::string(Memory::BinaryDocument));
    }
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
    if (const auto ram = memory->isWritable() ? memory : nullptr) {
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
      if (!memory->isWritable() && policy != Memory::BinaryDocument)
        continue;
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

TEST(MemoryInitializationTest, ZeroAndUninitializedPoliciesExecuteForRam)
{
  const auto memory = configuredRam();
  memory->setGeometry(8, 2);
  memory->setProperty("initialization", std::string(Memory::Zero));
  auto      circuit = std::make_shared<Circuit>(Component_set{memory}, false);
  Simulator sim(circuit);
  ASSERT_EQ(sim.setBus(memory->inputBuses()[0], valueFor(2, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(memory->outputBuses()[0].getCurrentValue(), valueFor(8, 0));
  memory->setProperty("initialization", std::string(Memory::Uninitialized));
  ASSERT_EQ(sim.setBus(memory->inputBuses()[0], valueFor(2, 2)),
            Simulator::RunResult::Completed);
  expectState(memory->outputBuses()[0], State::UNKNOWN);
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
    EXPECT_EQ(fields[0].key, "type");
    EXPECT_EQ(fields[1].key, "addressWidth");
    EXPECT_EQ(fields[2].key, "dataWidth");
    EXPECT_EQ(std::ranges::find(fields, "initialization", &PropertyInfo::key)
                  != fields.end(),
              memory->isWritable());
    EXPECT_EQ(std::ranges::find(fields, "binaryContents", &PropertyInfo::key)
                  != fields.end(),
              !memory->isWritable());
    EXPECT_EQ(std::ranges::find(fields, "padding", &PropertyInfo::key) != fields.end(),
              !memory->isWritable());
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
