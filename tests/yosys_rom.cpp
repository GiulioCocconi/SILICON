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

#include "yosys_test_helpers.hpp"

#if !defined(__EMSCRIPTEN__) && defined(SILICON_TEST_YOSYS_EXECUTABLE)
TEST(YosysRomTest, NormalizesMemoryBeforeJsonExport)
{
  const auto json = nlohmann::json::parse(SILICON::yosys::elaborateHierarchy(
      SILICON::verilog::read(
          "module rom(input a, output [7:0] q); reg [7:0] mem [0:1]; "
          "initial begin mem[0]=8'h12; mem[1]=8'h34; end "
          "assign q=mem[a]; endmodule")));
  const auto& cells = json.at("modules").at("rom").at("cells");
  std::size_t canonical = 0;
  for (const auto& [name, cell] : cells.items()) {
    (void)name;
    EXPECT_NE(cell.at("type"), "$mem_v2");
    if (cell.at("type") == "SILICON_ROM") {
      ++canonical;
      EXPECT_TRUE(cell.at("parameters").contains("SIZE"));
      EXPECT_TRUE(cell.at("parameters").contains("INIT"));
      EXPECT_TRUE(cell.at("connections").contains("SELECT"));
    }
  }
  EXPECT_EQ(canonical, 1);
}

TEST(YosysRomTest, ImportsInitializedArrayAndCreatesBinaryDocument)
{
  const SILICON::project::Document source{
      "code/rom.v",
      "module rom(input [1:0] addr, input cs, output reg [7:0] data);\n"
      "reg [7:0] table [0:3];\n"
      "initial begin table[0]=8'h12; table[1]=8'h34; table[2]=8'h56; table[3]=8'h78; end\n"
      "always @* if (cs) data=table[addr];\nendmodule\n"};
  const std::vector<SILICON::project::Document> documents{source};
  SILICON::project::ProjectContext project;
  project.setDocuments(documents);
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver(project, registry);
  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Circuit, documents, registry, resolver);
  const std::array<std::string, 1> selected{"rom"};
  const auto result = prepared.execute(selected);
  ASSERT_EQ(result.documents.size(), 2);
  const auto binary = std::ranges::find_if(result.documents, [](const auto& document) {
    return std::holds_alternative<SILICON::conversion::BinarySource>(document.payload);
  });
  ASSERT_NE(binary, result.documents.end());
  EXPECT_EQ(std::get<SILICON::conversion::BinarySource>(binary->payload).contents,
            std::string("\x12\x34\x56\x78", 4));
  const auto circuit = std::ranges::find_if(result.documents, [](const auto& document) {
    return std::holds_alternative<Circuit>(document.payload);
  });
  ASSERT_NE(circuit, result.documents.end());
  const auto& imported = std::get<Circuit>(circuit->payload);
  std::size_t roms = 0;
  for (const auto& [component, vertex] : imported.getComponentToVertex()) {
    EXPECT_FALSE(std::dynamic_pointer_cast<DLatch>(imported.getComponentByVertexId(vertex)));
    const auto rom = std::dynamic_pointer_cast<ROM>(imported.getComponentByVertexId(vertex));
    if (!rom) continue;
    ++roms;
    EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "rom_table");
    EXPECT_FALSE(rom->getPropertyValue<int>("wordCount").has_value());
    EXPECT_EQ(rom->resolvedWordCount(), 4);
    EXPECT_EQ(rom->inputBuses()[0].size(), 2);
    auto simulationCircuit = std::make_shared<Circuit>(Component_set{rom});
    Simulator simulator(simulationCircuit);
    const auto address = rom->inputBuses()[0];
    const auto cs = rom->inputBuses()[1];
    const auto data = rom->outputBuses()[0];
    ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 1)), Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(address, valueFor(address, 2)), Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x56));
    ASSERT_EQ(simulator.setBus(cs, valueFor(cs, 0)), Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(address, valueFor(address, 3)), Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x56));
  }
  EXPECT_EQ(roms, 1);
}

TEST(YosysRomTest, ExportsSelfContainedInitializedArrayAndReimportsIt)
{
  auto rom = std::make_shared<ROM>();
  rom->setProperty("dataWidth", 10);
  const Bus address(1), cs(1), oe(1), data(10);
  rom->setProperty("binaryContents", std::string("program"));
  rom->setInputs({address, cs, oe});
  rom->setOutputs({data});
  rom->refreshBinaryContents(std::make_shared<const std::string>(std::string("\xA5\x3C\x0F", 3)));
  Circuit circuit(Component_set{
      rom, std::make_shared<DummyInputComponent>(address, "address"),
      std::make_shared<DummyInputComponent>(cs, "cs"),
      std::make_shared<DummyOutputComponent>(data, "data")}, false);
  const auto source = SILICON::verilog::write(circuit, "romtest");
  EXPECT_NE(source.find("initial"), std::string::npos);
  EXPECT_NE(source.find(SILICON::yosys::attributes::BINARY_DOCUMENT), std::string::npos);
  EXPECT_EQ(source.find("readmem"), std::string::npos);
  const auto imported = SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(source)), "romtest");
  std::size_t roms = 0;
  for (const auto& [component, vertex] : imported.getComponentToVertex()) {
    const auto restored = std::dynamic_pointer_cast<ROM>(imported.getComponentByVertexId(vertex));
    if (!restored) continue;
    ++roms;
    EXPECT_EQ(restored->resolvedWordCount(), 2);
    ASSERT_TRUE(restored->binaryContentsSnapshot());
    EXPECT_EQ(*restored->binaryContentsSnapshot(), std::string("\xA5\x3C\x0F", 3));
  }
  EXPECT_EQ(roms, 1);
}

TEST(YosysRomTest, SharesBinaryDocumentAcrossReadPorts)
{
  const std::string sourceText =
      "module multi(input a, input b, output [7:0] x, output [7:0] y);\n"
      "reg [7:0] table [0:1];\n"
      "initial begin table[0]=8'h12; table[1]=8'h34; end\n"
      "assign x=table[a]; assign y=table[b]; endmodule\n";
  const auto imported = SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(sourceText)), "multi");
  std::size_t roms = 0;
  for (const auto& [component, vertex] : imported.getComponentToVertex()) {
    const auto rom = std::dynamic_pointer_cast<ROM>(imported.getComponentByVertexId(vertex));
    if (!rom) continue;
    ++roms;
    EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "multi_table");
    ASSERT_TRUE(rom->binaryContentsSnapshot());
    EXPECT_EQ(*rom->binaryContentsSnapshot(), std::string("\x12\x34", 2));
  }
  EXPECT_EQ(roms, 2);
  const SILICON::project::Document source{"code/multi.v", sourceText};
  const std::vector<SILICON::project::Document> documents{source};
  SILICON::project::ProjectContext project;
  project.setDocuments(documents);
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver(project, registry);
  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Circuit, documents, registry, resolver);
  const std::array<std::string, 1> selected{"multi"};
  const auto result = prepared.execute(selected);
  EXPECT_EQ(std::ranges::count_if(result.documents, [](const auto& document) {
              return std::holds_alternative<SILICON::conversion::BinarySource>(document.payload);
            }), 1);
}

TEST(YosysRomTest, VerilogAttributeSetsBinaryDocumentName)
{
  const SILICON::project::Document source{
      "code/named.v",
      "module named(input a, output [7:0] q);\n"
      "(* silicon_mem_slug = \"firmware\" *) reg [7:0] table [0:1];\n"
      "initial begin table[0]=8'h12; table[1]=8'h34; end\n"
      "assign q=table[a]; endmodule\n"};
  const std::vector<SILICON::project::Document> documents{source};
  SILICON::project::ProjectContext project;
  project.setDocuments(documents);
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver(project, registry);
  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Circuit, documents, registry, resolver);
  const std::array<std::string, 1> selected{"named"};
  const auto converted = prepared.execute(selected);
  const auto binary = std::ranges::find_if(converted.documents, [](const auto& document) {
    return std::holds_alternative<SILICON::conversion::BinarySource>(document.payload);
  });
  ASSERT_NE(binary, converted.documents.end());
  EXPECT_EQ(binary->path, "bin/firmware");
  EXPECT_EQ(std::get<SILICON::conversion::BinarySource>(binary->payload).contents,
            std::string("\x12\x34", 2));
  const auto circuit = std::ranges::find_if(converted.documents, [](const auto& document) {
    return std::holds_alternative<Circuit>(document.payload);
  });
  ASSERT_NE(circuit, converted.documents.end());
  for (const auto& [component, vertex] : std::get<Circuit>(circuit->payload).getComponentToVertex()) {
    const auto rom = std::dynamic_pointer_cast<ROM>(
        std::get<Circuit>(circuit->payload).getComponentByVertexId(vertex));
    if (rom) {
      EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "firmware");
    }
  }
}

TEST(YosysRomTest, CircuitConversionHydratesBinaryDocument)
{
  auto rom = std::make_shared<ROM>();
  rom->setProperty("dataWidth", 10);
  const Bus address(1), cs(1), data(10);
  rom->setProperty("binaryContents", std::string("program"));
  rom->setInputs({address, cs, Bus(1)});
  rom->setOutputs({data});
  rom->refreshBinaryContents(std::make_shared<const std::string>(std::string("\xA5\x3C\x0F", 3)));
  Circuit circuit(Component_set{
      rom, std::make_shared<DummyInputComponent>(address, "address"),
      std::make_shared<DummyInputComponent>(cs, "cs"),
      std::make_shared<DummyOutputComponent>(data, "data")}, false);
  const SILICON::project::Document source{
      "circuits/romtop.json",
      nlohmann::json{{"circuit", nlohmann::json::parse(circuit.serialize())}}.dump()};
  const std::vector<SILICON::project::Document> documents{
      source, {"bin/program", std::string("\xA5\x3C\x0F", 3)}};
  SILICON::project::ProjectContext project;
  project.setDocuments(documents);
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver(project, registry);
  const auto converted = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Verilog, documents, registry, resolver).execute({});
  ASSERT_EQ(converted.documents.size(), 1);
  const auto& verilog = std::get<SILICON::conversion::VerilogSource>(converted.documents[0].payload).contents;
  EXPECT_NE(verilog.find("initial"), std::string::npos);
  EXPECT_EQ(verilog.find("readmem"), std::string::npos);
}

TEST(YosysRomTest, ImportsClockedReadWithAddressRegister)
{
  auto circuit = std::make_shared<Circuit>(SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(
          "module syncrom(input clk, input [1:0] addr, output reg [7:0] q);\n"
          "(* silicon_mem_slug = \"clocked_program\" *) reg [7:0] mem [0:3];\n"
          "initial begin mem[0]=8'h12; mem[1]=8'h34; mem[2]=8'h56; mem[3]=8'h78; end\n"
          "always @(posedge clk) q <= mem[addr]; endmodule\n")), "syncrom"));
  std::shared_ptr<DummyBusInputComponent> addressInput;
  std::shared_ptr<DummyInputComponent> clockInput;
  std::shared_ptr<DummyBusOutputComponent> output;
  std::shared_ptr<ROM> rom;
  std::size_t registers = 0;
  for (const auto& [component, vertex] : circuit->getComponentToVertex()) {
    const auto item = circuit->getComponentByVertexId(vertex);
    if (auto input = std::dynamic_pointer_cast<DummyBusInputComponent>(item);
        input && input->getPropertyValue<std::string>("name") == "addr") addressInput = input;
    if (auto input = std::dynamic_pointer_cast<DummyInputComponent>(item);
        input && input->getPropertyValue<std::string>("name") == "clk") clockInput = input;
    if (auto out = std::dynamic_pointer_cast<DummyBusOutputComponent>(item)) output = out;
    if (auto memory = std::dynamic_pointer_cast<ROM>(item)) rom = memory;
    registers += std::dynamic_pointer_cast<Register>(item) != nullptr;
  }
  ASSERT_TRUE(addressInput && clockInput && output && rom);
  EXPECT_EQ(registers, 1);
  EXPECT_EQ(rom->resolvedWordCount(), 4);
  EXPECT_EQ(rom->getPropertyValue<std::string>("binaryContents"), "clocked_program");
  Simulator simulator(circuit);
  const auto addressBus = addressInput->outputBuses()[0];
  const auto clockBus = clockInput->outputBuses()[0];
  ASSERT_EQ(simulator.setBus(addressBus, valueFor(addressBus, 1)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(clockBus, valueFor(clockBus, 0)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(clockBus, valueFor(clockBus, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), valueFor(output->inputBuses()[0], 0x34));
  ASSERT_EQ(simulator.setBus(addressBus, valueFor(addressBus, 2)), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), valueFor(output->inputBuses()[0], 0x34));
  ASSERT_EQ(simulator.setBus(clockBus, valueFor(clockBus, 0)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(clockBus, valueFor(clockBus, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), valueFor(output->inputBuses()[0], 0x56));
  const auto exported = SILICON::verilog::write(*circuit, "syncrom_roundtrip");
  EXPECT_NE(exported.find("initial"), std::string::npos);
  EXPECT_NE(exported.find(SILICON::yosys::attributes::BINARY_DOCUMENT), std::string::npos);
  EXPECT_EQ(exported.find("readmem"), std::string::npos);
  const auto restored = SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(exported)),
      "syncrom_roundtrip");
  std::size_t restoredRoms = 0, restoredRegisters = 0;
  for (const auto& [component, vertex] : restored.getComponentToVertex()) {
    const auto item = restored.getComponentByVertexId(vertex);
    restoredRoms += std::dynamic_pointer_cast<ROM>(item) != nullptr;
    restoredRegisters += std::dynamic_pointer_cast<Register>(item) != nullptr;
  }
  EXPECT_EQ(restoredRoms, 1);
  EXPECT_EQ(restoredRegisters, 1);
}

TEST(YosysRomTest, ClockedCaseTableDoesNotGrowOnRoundTrip)
{
  std::string source =
      "module case_rom(input clk, input en, input [5:0] addr, output [19:0] dout);\n"
      "(* rom_style = \"block\" *) reg [19:0] data;\n"
      "always @(posedge clk) if (en) case (addr)\n";
  for (unsigned address = 0; address < 64; ++address)
    source += std::format("6'd{}: data <= 20'd{};\n", address, address * 17 + 3);
  source += "endcase\nassign dout = data;\nendmodule\n";

  const auto countTypes = [](const Circuit& circuit) {
    std::map<std::string, std::size_t> counts;
    for (const auto& [component, vertex] : circuit.getComponentToVertex())
      ++counts[std::string(circuit.getComponentByVertexId(vertex)->typeName())];
    return counts;
  };
  const auto first = SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(source)), "case_rom");
  const auto exported = SILICON::verilog::write(first, "case_rom");
  auto second = std::make_shared<Circuit>(SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(exported)), "case_rom"));
  const auto third = SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(
          SILICON::verilog::write(*second, "case_rom"))), "case_rom");
  EXPECT_EQ(countTypes(first)["ROM"], 1);
  EXPECT_EQ(countTypes(first)["Register"], 1);
  EXPECT_EQ(countTypes(*second), countTypes(first)) << exported;
  EXPECT_EQ(countTypes(third), countTypes(*second));

  std::map<std::string, Bus> inputs;
  Bus output;
  for (const auto& [component, vertex] : second->getComponentToVertex()) {
    const auto item = second->getComponentByVertexId(vertex);
    if (std::dynamic_pointer_cast<DummyInputComponent>(item)
        || std::dynamic_pointer_cast<DummyBusInputComponent>(item))
      inputs.emplace(item->getPropertyValue<std::string>("name").value_or(""),
                     item->outputBuses()[0]);
    if (std::dynamic_pointer_cast<DummyBusOutputComponent>(item))
      output = item->inputBuses()[0];
  }
  ASSERT_TRUE(inputs.contains("clk") && inputs.contains("en") && inputs.contains("addr"));
  ASSERT_EQ(output.size(), 20);
  Simulator simulator(second);
  const auto set = [&](const std::string& name, const std::uint64_t value) {
    return simulator.setBus(inputs.at(name), valueFor(inputs.at(name), value));
  };
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  ASSERT_EQ(set("en", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("addr", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 20));
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  ASSERT_EQ(set("en", 0), Simulator::RunResult::Completed);
  ASSERT_EQ(set("addr", 2), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 20));
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  ASSERT_EQ(set("en", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 37));
}

TEST(YosysRomTest, ClockedReadEnableHoldsAddressOnFallingEdges)
{
  auto circuit = std::make_shared<Circuit>(SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(
          "module syncrom(input clk, input en, input [1:0] addr, output reg [7:0] q);\n"
          "reg [7:0] mem [0:3];\n"
          "initial begin mem[0]=8'h12; mem[1]=8'h34; mem[2]=8'h56; mem[3]=8'h78; end\n"
          "always @(negedge clk) if (en) q <= mem[addr]; endmodule\n")), "syncrom"));
  std::map<std::string, Bus> inputs;
  Bus output;
  std::size_t registers = 0;
  std::size_t inverters = 0;
  for (const auto& [component, vertex] : circuit->getComponentToVertex()) {
    const auto item = circuit->getComponentByVertexId(vertex);
    if (std::dynamic_pointer_cast<DummyInputComponent>(item)
        || std::dynamic_pointer_cast<DummyBusInputComponent>(item))
      inputs.emplace(item->getPropertyValue<std::string>("name").value_or(""), item->outputBuses()[0]);
    if (std::dynamic_pointer_cast<DummyBusOutputComponent>(item)) output = item->inputBuses()[0];
    registers += std::dynamic_pointer_cast<Register>(item) != nullptr;
    inverters += std::dynamic_pointer_cast<NotGate>(item) != nullptr;
  }
  ASSERT_TRUE(inputs.contains("clk") && inputs.contains("en") && inputs.contains("addr"));
  ASSERT_EQ(output.size(), 8);
  EXPECT_EQ(registers, 1);
  EXPECT_EQ(inverters, 1);
  Simulator simulator(circuit);
  const auto set = [&](const std::string& name, const std::uint64_t value) {
    return simulator.setBus(inputs.at(name), valueFor(inputs.at(name), value));
  };
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("en", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("addr", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 0x34));
  ASSERT_EQ(set("addr", 2), Simulator::RunResult::Completed);
  ASSERT_EQ(set("en", 0), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 0x34));
  ASSERT_EQ(set("en", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 1), Simulator::RunResult::Completed);
  ASSERT_EQ(set("clk", 0), Simulator::RunResult::Completed);
  EXPECT_EQ(output.getCurrentValue(), valueFor(output, 0x56));
}

TEST(YosysRomTest, RejectsUnsupportedMemoryBehavior)
{
  const std::array<std::string, 7> sources{
      "module bad(input a, input d, output q); reg mem [0:1]; initial mem[0]=0; assign q=mem[a]; endmodule",
      "module bad(input a, output q); reg mem [0:1]; initial begin mem[0]=0; mem[1]=1'bx; end assign q=mem[a]; endmodule",
      "module bad(input a, input d, output q); reg mem [0:2]; initial begin mem[0]=0; mem[1]=1; mem[2]=0; end assign q=mem[a]; endmodule",
      "module bad(input a, input d, output q); reg mem [1:2]; initial begin mem[1]=0; mem[2]=1; end assign q=mem[a]; endmodule",
      "module bad(input a, input d, output q); reg mem [0:1]; initial begin mem[0]=0; mem[1]=1; end always @* mem[a]=d; assign q=mem[a]; endmodule",
      "module bad(input a, output q); (* silicon_mem_slug = \"bad/name\" *) reg mem [0:1]; initial begin mem[0]=0; mem[1]=1; end assign q=mem[a]; endmodule",
      "module bad(input a, output q); reg mem [0:0]; initial mem[0]=1; assign q=mem[a]; endmodule"};
  for (const auto& source : sources)
    EXPECT_THROW(static_cast<void>(SILICON::yosys::deserialize(
        SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(source)), "bad")),
        std::exception) << source;
}
#endif
