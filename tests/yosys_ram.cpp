#include "yosys_test_helpers.hpp"

namespace {

std::shared_ptr<RAM> makeRam(bool initialized = true)
{
  auto ram = std::make_shared<RAM>();
  ram->setProperty("depth", 3);
  if (initialized)
    ram->setBinaryDocument("initial", std::make_shared<const std::vector<std::uint8_t>>(
                                          std::vector<std::uint8_t>{0x12, 0x34, 0x56}));
  return ram;
}

std::shared_ptr<RAM> findRam(const Circuit& circuit)
{
  for (const auto& component : componentsIn(circuit))
    if (auto ram = std::dynamic_pointer_cast<RAM>(component))
      return ram;
  return nullptr;
}

Circuit ramWithPorts(const std::shared_ptr<RAM>& ram)
{
  Component_set                    components{ram};
  const std::array<std::string, 5> names{"a", "b", "d", "we", "clk"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto& bus = ram->inputBuses()[i];
    if (bus.size() == 1)
      components.insert(std::make_shared<DummyInputComponent>(bus, names[i]));
    else
      components.insert(std::make_shared<DummyBusInputComponent>(bus, names[i]));
  }
  components.insert(
      std::make_shared<DummyBusOutputComponent>(ram->outputBuses()[0], "q"));
  return Circuit(components, false);
}

nlohmann::json& memoryCell(nlohmann::json& json, std::string_view type = "$mem_v2")
{
  for (auto& cell : json["modules"]["component_test"]["cells"])
    if (cell.at("type") == type)
      return cell;
  throw std::runtime_error("Missing memory cell");
}

std::map<std::string, Bus> memoryCircuitPorts(const Circuit& circuit)
{
  std::map<std::string, Bus> ports;
  for (const auto& component : componentsIn(circuit)) {
    const auto role = component->metadata().portRole;
    if (role == PortRole::Input || role == PortRole::Output)
      ports.emplace(component->getPropertyValue<std::string>("name").value(),
                    role == PortRole::Input ? component->outputBuses()[0]
                                            : component->inputBuses()[0]);
  }
  return ports;
}

}  // namespace

TEST(YosysRamTest, ExportsIndependentAddressesWithImmutableInitialContents)
{
  auto       ram     = makeRam();
  auto       circuit = std::make_shared<Circuit>(ramWithPorts(ram));
  Simulator  simulator(circuit);
  const auto set = [&](std::size_t port, std::uint64_t value) {
    EXPECT_EQ(simulator.setBus(ram->inputBuses()[port],
                               valueFor(ram->inputBuses()[port], value)),
              Simulator::RunResult::Completed);
  };
  set(0, 0);
  set(1, 0);
  set(3, 0);
  set(4, 0);
  set(2, 0xFF);
  set(3, 0xFF);
  set(4, 1);
  EXPECT_EQ(ram->outputBuses()[0].getCurrentValue(), valueFor(8, 0xFF));
  auto        json = exportComponent(ram);
  const auto& cell = memoryCell(json);
  EXPECT_EQ(cell.at("parameters").at("INIT"), "010101100011010000010010");
  EXPECT_EQ(cell.at("parameters").at("WR_CLK_POLARITY"), "1");
  EXPECT_EQ(cell.at("connections").at("WR_EN").size(), 8);
  EXPECT_NE(cell.at("connections").at("RD_ADDR"), cell.at("connections").at("WR_ADDR"));
  EXPECT_EQ(json.at("modules").at("component_test").at("cells").size(), 1);
  EXPECT_THROW((void)SILICON::yosys::deserialize(json.dump(), "component_test"),
               std::runtime_error);
}

#if !defined(__EMSCRIPTEN__) && defined(SILICON_TEST_YOSYS_EXECUTABLE)

TEST(YosysRamTest, PreservesWiderAddressBusesAndAllUnknownInitialization)
{
  auto ram    = makeRam(false);
  auto inputs = ram->inputBuses();
  inputs[0].setSize(4);
  inputs[1].setSize(4);
  ram->setInputs(inputs);
  auto original = nlohmann::json::parse(
      SILICON::yosys::serialize(ramWithPorts(ram), "component_test"));
  auto restored = findRam(SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(original.dump()), "component_test"));
  ASSERT_TRUE(restored);
  EXPECT_FALSE(restored->memoryImageSnapshot());
  EXPECT_EQ(restored->inputBuses().size(), 5);
  EXPECT_EQ(restored->inputBuses()[0].size(), 4);
  auto exported = exportComponent(restored);
  EXPECT_EQ(memoryCell(original).at("parameters"), memoryCell(exported).at("parameters"));
}

TEST(YosysRamTest, RejectsUnsupportedConfigurationsAndMixedUnknownInit)
{
  const auto original        = nlohmann::json::parse(SILICON::yosys::elaborateHierarchy(
      SILICON::yosys::serialize(ramWithPorts(makeRam()), "component_test")));
  const auto rejectParameter = [&](const std::string& name, const std::string& value) {
    auto json                                           = original;
    memoryCell(json, "SILICON_RAM")["parameters"][name] = value;
    EXPECT_THROW((void)SILICON::yosys::deserialize(json.dump(), "component_test"),
                 std::runtime_error)
        << name;
  };
  for (const auto parameter : {"WIDTH", "SIZE", "ABITS"})
    rejectParameter(parameter, "0");
  rejectParameter("INIT", std::string(23, '0') + 'x');
  rejectParameter("INIT", std::string(24, 'z'));
  rejectParameter("INIT", "0");
  for (const auto port : {"WR_DATA", "WR_EN", "RD_ADDR", "WR_CLK", "RD_DATA"}) {
    auto json                                            = original;
    memoryCell(json, "SILICON_RAM")["connections"][port] = nlohmann::json::array();
    EXPECT_THROW((void)SILICON::yosys::deserialize(json.dump(), "component_test"),
                 std::runtime_error)
        << port;
  }
}

  #ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
TEST(YosysRamTest, LowersSinglePortMemoryAndPreservesItsContract)
{
  const auto passes =
      std::format("read_verilog -lib -D SILICON_BLACKBOX \"{}/silicon_cells.v\"\n"
                  "silicon_memory_lower\nopt_clean\nsilicon_validate",
                  SILICON_TEST_YOSYS_RESOURCE_DIR);
  for (bool initialized : {false, true}) {
    auto ram    = makeRam(initialized);
    auto inputs = ram->inputBuses();
    inputs[0].setSize(4);
    inputs[1].setSize(4);
    ram->setInputs(inputs);
    auto source = nlohmann::json::parse(
        SILICON::yosys::serialize(ramWithPorts(ram), "component_test"));
    const auto original = memoryCell(source);
    const auto lowered =
        nlohmann::json::parse(legalizeExpressions(source.dump(), passes));
    const auto& cells = lowered.at("modules").at("component_test").at("cells");
    ASSERT_EQ(cells.size(), 1);
    const auto& cell = cells.begin().value();
    EXPECT_EQ(cell.at("type"), "SILICON_RAM");
    EXPECT_EQ(cell.at("parameters").size(), 5);
    EXPECT_EQ(cell.at("connections").size(), 6);
    for (const auto key : {"WIDTH", "SIZE", "ABITS", "INIT"})
      EXPECT_EQ(cell.at("parameters").at(key), original.at("parameters").at(key));
    const auto& ports = lowered.at("modules").at("component_test").at("ports");
    for (const auto& [key, port] :
         std::array<std::pair<const char*, const char*>, 6>{{{"RD_ADDR", "a"},
                                                             {"WR_ADDR", "b"},
                                                             {"RD_DATA", "q"},
                                                             {"WR_DATA", "d"},
                                                             {"WR_EN", "we"},
                                                             {"WR_CLK", "clk"}}})
      EXPECT_EQ(cell.at("connections").at(key), ports.at(port).at("bits"));
    auto restored =
        findRam(SILICON::yosys::deserialize(lowered.dump(), "component_test"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->memoryDepth(), 3);
    EXPECT_EQ(restored->inputBuses()[0].size(), 4);
    EXPECT_EQ(static_cast<bool>(restored->memoryImageSnapshot()), initialized);
    if (initialized) {
      EXPECT_EQ(*restored->binaryContentsSnapshot(), *ram->binaryContentsSnapshot());
      EXPECT_EQ(restored->getPropertyValue<std::string>("binaryContents"), "initial");
    }
    const auto second =
        nlohmann::json::parse(legalizeExpressions(lowered.dump(), passes));
    EXPECT_EQ(second.at("modules").at("component_test").at("cells"), cells);
  }
}

TEST(YosysRamTest, MemoryLoweringRejectsUnsupportedWritableCells)
{
  const auto original        = exportComponent(makeRam());
  const auto rejectParameter = [&](const std::string& name, const std::string& value) {
    auto json                            = original;
    memoryCell(json)["parameters"][name] = value;
    EXPECT_THROW((void)legalizeExpressions(json.dump(), "silicon_memory_lower"),
                 std::exception)
        << name;
  };
  for (const auto name :
       {"WR_CLK_ENABLE", "WR_CLK_POLARITY", "RD_PORTS", "SIZE", "ABITS"})
    rejectParameter(name, "0");
  for (const auto name : {"OFFSET", "RD_CLK_ENABLE", "RD_TRANSPARENCY_MASK",
                          "RD_COLLISION_X_MASK", "RD_WIDE_CONTINUATION",
                          "WR_WIDE_CONTINUATION", "RD_CE_OVER_SRST", "WR_PRIORITY_MASK"})
    rejectParameter(name, "1");
  rejectParameter("WR_PORTS", "10");
  rejectParameter("INIT", std::string(23, '0') + 'x');
  for (const auto port : {"RD_EN", "RD_ARST", "RD_SRST", "WR_DATA", "WR_EN", "WR_ADDR",
                          "RD_ADDR", "WR_CLK", "RD_CLK"}) {
    auto json                             = original;
    memoryCell(json)["connections"][port] = nlohmann::json::array();
    EXPECT_THROW((void)legalizeExpressions(json.dump(), "silicon_memory_lower"),
                 std::exception)
        << port;
  }
}
  #endif

TEST(YosysRamTest, VerilogRoundTripPreservesIndependentPortsAndMaskedWrites)
{
  for (bool initialized : {false, true}) {
    auto ram    = makeRam(initialized);
    auto source = SILICON::verilog::write(ramWithPorts(ram), "ram");
    for (int round = 0; round < 2; ++round) {
      auto circuit = std::make_shared<Circuit>(importVerilog(source, "ram"));
      EXPECT_EQ(componentTypes(*circuit).count("RAM"), 1);
      auto restored = findRam(*circuit);
      ASSERT_TRUE(restored);
      EXPECT_EQ(restored->memoryDepth(), 3);
      const auto ports = memoryCircuitPorts(*circuit);
      Simulator  simulator(circuit);
      const auto set = [&](const std::string& name, std::uint64_t value) {
        EXPECT_EQ(simulator.setBus(ports.at(name), valueFor(ports.at(name), value)),
                  Simulator::RunResult::Completed);
      };
      set("a", 1);
      set("b", 1);
      set("we", 0);
      set("clk", 0);
      set("we", 0xFF);
      set("d", 0xA5);
      set("clk", 1);
      EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(8, 0xA5));
      set("we", 0);
      EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(8, 0xA5));
      set("clk", 0);
      set("we", 0x0F);
      set("d", 0x3C);
      set("clk", 1);
      EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(8, 0xAC));
      set("we", 0);
      EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(8, 0xAC));
      source = SILICON::verilog::write(*circuit, "ram");
    }
  }
}

TEST(YosysRamTest, RejectsUnsupportedSinglePortConfigurations)
{
  const std::array<std::string, 2> sources{
      "module bad(input c,a,d,w,output reg q); reg m[0:1]; always @(negedge c) if(w) "
      "m[a]<=d; always @* if(!w) q=m[a]; endmodule",
      "module bad(input c,a,d,w,output reg q); reg m[0:1]; initial m[0]=0; always "
      "@(posedge c) if(w) m[a]<=d; always @* if(!w) q=m[a]; endmodule"};
  for (const auto& source : sources)
    EXPECT_THROW((void)importVerilog(source, "bad"), std::exception) << source;
}

TEST(YosysRamTest, ImportsDualReadRamAndPreservesMaskedWrites)
{
  std::string source =
      "module regs(input clk, input [1:0] a,b,waddr, input [3:0] d,mask,"
      "output [3:0] q,r); reg [3:0] mem[0:3];"
      "genvar i; generate for(i=0;i<4;i=i+1) begin "
      "always @(posedge clk) if(mask[i]) mem[waddr][i]<=d[i]; end endgenerate "
      "assign q=mem[a]; assign r=mem[b]; endmodule";
  for (int round = 0; round < 2; ++round) {
    auto       circuit = std::make_shared<Circuit>(importVerilog(source, "regs"));
    const auto ram     = findRam(*circuit);
    ASSERT_TRUE(ram);
    EXPECT_EQ(ram->readPortCount(), 2);
    EXPECT_EQ(componentTypes(*circuit).count("RAM"), 1);
    EXPECT_EQ(componentTypes(*circuit).count("Register"), 0);
    const auto ports = memoryCircuitPorts(*circuit);
    Simulator  simulator(circuit);
    const auto set = [&](const std::string& name, std::uint64_t value) {
      ASSERT_EQ(simulator.setBus(ports.at(name), valueFor(ports.at(name), value)),
                Simulator::RunResult::Completed);
    };
    const auto write = [&](unsigned address, unsigned data, unsigned mask) {
      set("clk", 0);
      set("waddr", address);
      set("d", data);
      set("mask", mask);
      set("clk", 1);
    };
    set("clk", 0);
    set("mask", 0);
    set("a", 1);
    set("b", 2);
    write(1, 10, 15);
    write(2, 5, 15);
    EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(4, 10));
    EXPECT_EQ(ports.at("r").getCurrentValue(), valueFor(4, 5));
    write(1, 3, 3);
    EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(4, 11));
    EXPECT_EQ(ports.at("r").getCurrentValue(), valueFor(4, 5));
    write(2, 15, 0);
    EXPECT_EQ(ports.at("r").getCurrentValue(), valueFor(4, 5));
    set("a", 2);
    set("b", 1);
    EXPECT_EQ(ports.at("q").getCurrentValue(), valueFor(4, 5));
    EXPECT_EQ(ports.at("r").getCurrentValue(), valueFor(4, 11));
    source = SILICON::verilog::write(*circuit, "regs");
  }
}

TEST(YosysRamTest, RejectsMalformedDualReadRamPorts)
{
  auto ram = makeRam(false);
  ram->setProperty("readPorts", 2);
  const auto original = nlohmann::json::parse(SILICON::yosys::elaborateHierarchy(
      SILICON::yosys::serialize(circuitWithBoundaryPorts(ram), "component_test")));
  for (const auto parameter : {"0", "11"}) {
    auto json                                                 = original;
    memoryCell(json, "SILICON_RAM")["parameters"]["RD_PORTS"] = parameter;
    EXPECT_THROW((void)SILICON::yosys::deserialize(json.dump(), "component_test"),
                 std::runtime_error);
  }
  for (const auto port : {"RD_ADDR", "RD_DATA"}) {
    auto json = original;
    memoryCell(json, "SILICON_RAM")["connections"][port].erase(0);
    EXPECT_THROW((void)SILICON::yosys::deserialize(json.dump(), "component_test"),
                 std::runtime_error);
  }
}

TEST(YosysRamTest, ImportsMixedRomAndSinglePortRam)
{
  auto circuit = importVerilog("module mixed(input c,input [1:0] a,input [7:0] d,input "
                               "w,output [7:0] q,output [7:0] r);"
                               "reg [7:0] ram[0:2]; reg [7:0] rom[0:3];"
                               "initial begin rom[0]=1;rom[1]=2;rom[2]=3;rom[3]=4;end "
                               "always @(posedge c) if(w) ram[a]<=d; "
                               "assign q=ram[a];assign r=rom[a];endmodule",
                               "mixed");
  EXPECT_TRUE(findRam(circuit));
  EXPECT_EQ(componentTypes(circuit).count("ROM"), 1);
}

TEST(YosysRamTest, ProjectConversionCreatesAndRehydratesRamBinaryDocument)
{
  const SILICON::project::Document source{
      "code/ram.v", "module ram(input c,a,d,w,output q); reg mem[0:1];"
                    "initial begin mem[0]=0;mem[1]=1;end "
                    "always @(posedge c) if(w) mem[a]<=d; assign q=mem[a];endmodule"};
  SILICON::project::ProjectContext project;
  project.setDocuments({source});
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver(project, registry);
  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Circuit, project.documents().getDocuments(),
      registry, resolver);
  const std::array<std::string, 1> selected{"ram"};
  const auto                       converted = prepared.execute(selected);
  ASSERT_EQ(converted.documents.size(), 2);
  for (const auto& document : converted.documents) {
    if (std::holds_alternative<Circuit>(document.payload))
      project.upsertDocument(
          {document.path, std::get<Circuit>(document.payload).serialize()});
    else
      project.upsertDocument(
          {document.path,
           std::get<SILICON::conversion::BinarySource>(document.payload).contents});
  }
  auto ram = findRam(resolver.resolve("ram").circuit);
  ASSERT_TRUE(ram);
  ASSERT_TRUE(ram->memoryImageSnapshot());
  EXPECT_EQ(ram->memoryImageSnapshot()->word(0), valueFor(1, 0));
  EXPECT_EQ(ram->memoryImageSnapshot()->word(1), valueFor(1, 1));
  project.renameDocument(
      "bin/" + ram->getPropertyValue<std::string>("binaryContents").value(),
      "bin/renamed");
  auto renamed = findRam(resolver.resolve("ram").circuit);
  EXPECT_EQ(renamed->getPropertyValue<std::string>("binaryContents"), "renamed");
  EXPECT_TRUE(renamed->memoryImageSnapshot());
}

#endif
