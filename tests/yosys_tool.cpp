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

#ifndef __EMSCRIPTEN__
TEST(YosysToolTest, RejectsMissingConfiguredExecutable)
{
  const auto missing = std::filesystem::temp_directory_path()
                       / "silicon_yosys_executable_that_does_not_exist";
  try {
    (void)SILICON::yosys::runScript(
        "help", SILICON::yosys::ToolOptions{.executable                 = missing,
                                            .technologyLibraryDirectory = std::nullopt});
    FAIL() << "Expected a missing Yosys executable to be rejected";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("executable-validation phase"),
              std::string::npos);
    EXPECT_NE(std::string(error.what()).find(missing.string()), std::string::npos);
  }
}

  #if !defined(_WIN32)
TEST(YosysToolTest, ReportsProcessCreationFailure)
{
  const auto path =
      std::filesystem::temp_directory_path()
      / std::format("silicon_yosys_non_executable_{}",
                    std::chrono::steady_clock::now().time_since_epoch().count());
  {
    std::ofstream file(path);
    ASSERT_TRUE(file.good());
    file << "not an executable";
  }
  std::filesystem::permissions(path, std::filesystem::perms::owner_read,
                               std::filesystem::perm_options::replace);

  std::string message;
  try {
    (void)SILICON::yosys::runScript(
        "help", SILICON::yosys::ToolOptions{.executable                 = path,
                                            .technologyLibraryDirectory = std::nullopt});
  } catch (const std::runtime_error& error) {
    message = error.what();
  }
  std::filesystem::remove(path);

  EXPECT_NE(message.find("process-creation phase"), std::string::npos);
  EXPECT_NE(message.find("'yosys' logs"), std::string::npos);
}

TEST(YosysToolTest, ReportsPathDiscoveryFailure)
{
  std::string message;
  {
    const PathEnvironmentGuard emptyPath;
    try {
      (void)SILICON::yosys::runScript("help");
    } catch (const std::runtime_error& error) {
      message = error.what();
    }
  }
  EXPECT_NE(message.find("executable-discovery phase"), std::string::npos);
  EXPECT_NE(message.find("PATH"), std::string::npos);
}
  #endif
#endif
#ifdef SILICON_TEST_YOSYS_EXECUTABLE

TEST(YosysToolTest, RunsScriptsWithPathDiscoveryAndExplicitSelection)
{
  YosysLogCapture logCapture;
  const auto      discovered = SILICON::yosys::runScript("help echo");
  EXPECT_FALSE(discovered.standardOutput.empty());

  const SILICON::yosys::ToolOptions explicitOptions{
      .executable                 = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
      .technologyLibraryDirectory = std::nullopt};
  const auto explicitResult = SILICON::yosys::runScript("help echo", explicitOptions);
  EXPECT_FALSE(explicitResult.standardOutput.empty());
  EXPECT_EQ(explicitResult.standardError, discovered.standardError);
  EXPECT_NE(logCapture.text().find("stdout:"), std::string::npos);
}

TEST(YosysToolTest, LogsCapturedDiagnosticsAndReferencesLogs)
{
  YosysLogCapture logCapture;
  std::string     message;
  try {
    (void)SILICON::yosys::runScript("this_command_does_not_exist");
  } catch (const std::runtime_error& error) {
    message = error.what();
  }

  EXPECT_NE(message.find("script-execution phase"), std::string::npos);
  EXPECT_NE(message.find("exit status"), std::string::npos);
  EXPECT_NE(message.find("'yosys' logs"), std::string::npos);
  EXPECT_EQ(message.find("stdout:"), std::string::npos);
  EXPECT_EQ(message.find("stderr:"), std::string::npos);

  const auto logs = logCapture.text();
  EXPECT_NE(logs.find("stderr:"), std::string::npos);
  EXPECT_NE(logs.find("this_command_does_not_exist"), std::string::npos);
  EXPECT_NE(logs.find("Command failed with exit status"), std::string::npos);
}

TEST(YosysToolTest, RejectsInvalidMultiSourceInputs)
{
  using SILICON::verilog::SourceFile;

  const std::array unsafe{
      SourceFile{.path = "../top.v", .contents = "module top; endmodule"}};
  EXPECT_THROW((void)SILICON::verilog::read(unsafe, "../top.v"), std::invalid_argument);

  const std::array duplicate{
      SourceFile{.path = "code/top.v", .contents = "module top; endmodule"},
      SourceFile{.path = "code/top.v", .contents = "module other; endmodule"}};
  EXPECT_THROW((void)SILICON::verilog::read(duplicate, "code/top.v"),
               std::invalid_argument);

  const std::array SOURCES{
      SourceFile{.path = "code/helper.v", .contents = "module helper; endmodule"}};
  EXPECT_THROW((void)SILICON::verilog::read(SOURCES, "code/top.v"),
               std::invalid_argument);
}

TEST(YosysToolTest, RejectsImplicitNets)
{
  YosysLogCapture logCapture;
  std::string     message;
  try {
    (void)SILICON::verilog::read(
        "module top(input [7:0] a, output [7:0] y); assign y = a + B; endmodule");
  } catch (const std::runtime_error& error) {
    message = error.what();
  }

  EXPECT_NE(message.find("script-execution phase"), std::string::npos);
  EXPECT_NE(logCapture.text().find("Identifier `\\B' is implicitly declared"),
            std::string::npos);
}

TEST(YosysToolTest, ResolvesTransitiveProjectIncludesWithoutParsingUnrelatedFiles)
{
  using SILICON::verilog::SourceFile;

  constexpr std::array SOURCES{
      SourceFile{.path = "code/top.v", .contents = R"(`include "helper.v"
module top(input a, output y);
  helper child(.a(a), .y(y));
endmodule
)"},
      SourceFile{.path = "code/helper.v", .contents = R"(`include "defs.v"
module helper(input a, output y);
  assign y = `APPLY(a);
endmodule
)"},
      SourceFile{.path = "code/defs.v", .contents = "`define APPLY(signal) ~signal\n"},
      SourceFile{.path     = "code/unrelated.v",
                 .contents = "this is deliberately invalid Verilog\n"}};

  const auto designJson = SILICON::verilog::read(SOURCES, "code/top.v");
  const auto design     = SILICON::yosys::Json::parse(designJson);
  ASSERT_TRUE(design.at("modules").contains("top"));
  ASSERT_TRUE(design.at("modules").contains("helper"));
  EXPECT_FALSE(design.at("modules").contains("unrelated"));

  const auto graph = SILICON::yosys::moduleDependencyGraph(designJson);
  EXPECT_EQ(graph.modules(), (std::vector<std::string>{"helper", "top"}));
  EXPECT_EQ(graph.dependenciesOf("top"), (std::vector<std::string>{"helper"}));

  const auto& helperCells = design.at("modules").at("helper").at("cells");
  ASSERT_EQ(helperCells.size(), 1);
  EXPECT_EQ(helperCells.begin().value().at("type"), "$not");
}

TEST(YosysToolTest, ReportsMissingProjectIncludeThroughYosysDiagnostics)
{
  constexpr std::array SOURCES{SILICON::verilog::SourceFile{
      .path = "code/top.v", .contents = "`include \"missing.v\"\n"}};
  YosysLogCapture      logCapture;
  std::string          message;
  try {
    (void)SILICON::verilog::read(SOURCES, "code/top.v");
  } catch (const std::runtime_error& error) {
    message = error.what();
  }

  EXPECT_NE(message.find("script-execution phase"), std::string::npos);
  EXPECT_NE(logCapture.text().find("missing.v"), std::string::npos);
}

TEST(YosysToolTest, BuildsDependencyGraphFromMultiModuleVerilog)
{
  constexpr std::string_view SOURCE = R"(
    module leaf(input a, output reg y);
      always @* y = ~a;
    endmodule
    module helper(input a, output y0, output y1);
      leaf first(.a(a), .y(y0));
      leaf second(.a(a), .y(y1));
    endmodule
    module unused(input a, output y);
      assign y = a;
    endmodule
    module top(
      input a,
      input b,
      output y0,
      output y1,
      output y2,
      output and_y,
      output or_y,
      output not_y
    );
      helper helper_instance(.a(a), .y0(y0), .y1(y1));
      leaf leaf_instance(.a(a), .y(y2));
      assign and_y = a & b;
      assign or_y = a | b;
      assign not_y = ~a;
    endmodule
  )";

  const auto designJson = SILICON::verilog::read(SOURCE);
  const auto design     = SILICON::yosys::Json::parse(designJson);

  std::set<std::string> topCellTypes;
  for (const auto& cell : design.at("modules").at("top").at("cells"))
    topCellTypes.insert(cell.at("type").get<std::string>());
  EXPECT_TRUE(topCellTypes.contains("$and"));
  EXPECT_TRUE(topCellTypes.contains("$or"));
  EXPECT_TRUE(topCellTypes.contains("$not"));

  const auto graph = SILICON::yosys::moduleDependencyGraph(designJson);

  EXPECT_EQ(graph.modules(),
            (std::vector<std::string>{"helper", "leaf", "top", "unused"}));
  EXPECT_EQ(graph.dependenciesOf("top"), (std::vector<std::string>{"helper", "leaf"}));
  EXPECT_EQ(graph.dependenciesOf("helper"), (std::vector<std::string>{"leaf"}));
  EXPECT_EQ(graph.dependenciesOf("leaf"), std::vector<std::string>{});
  EXPECT_EQ(graph.dependenciesOf("unused"), std::vector<std::string>{});

  const auto top = SILICON::yosys::deserialize(designJson, "top");
  EXPECT_EQ(componentTypes(top).count("Subcircuit"), 2);
}

TEST(YosysToolTest, ImportsCombinationalVerilogPreservingHelperHierarchy)
{
  constexpr std::string_view SOURCE = R"(
    module helper(input a, input b, output y);
      assign y = a & b;
    endmodule
    module unused(input a, output y);
      assign y = ~a;
    endmodule
    module selected(input a, input b, output y);
      helper instance(.a(a), .b(b), .y(y));
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "selected");
  // Elaboration preserves the module hierarchy, so the helper instance is kept
  // as a subcircuit rather than flattened into its gates.
  EXPECT_EQ(componentTypes(circuit),
            (std::multiset<std::string>{"Subcircuit", "DummyInputComponent",
                                        "DummyInputComponent", "DummyOutputComponent"}));
  EXPECT_THROW((void)importVerilog(SOURCE, "missing"), std::runtime_error);
}

TEST(YosysToolTest, ImportsLogicalOperatorsWithVectorTruthSemantics)
{
  const auto verify = [](const std::string_view expression,
                         const std::string_view yosysCell,
                         const std::string_view siliconComponent, const auto& expected) {
    const auto SOURCE = std::format("module top(input [2:0] a, input [2:0] b, output y); "
                                    "assign y = {}; endmodule",
                                    expression);

    const auto hierarchicalJson =
        SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(SOURCE));
    EXPECT_NE(hierarchicalJson.find(yosysCell), std::string::npos);

    const std::array circuits{
        std::make_shared<Circuit>(SILICON::yosys::deserialize(hierarchicalJson, "top")),
        std::make_shared<Circuit>(importVerilog(SOURCE, "top")),
    };
    for (const auto& circuit : circuits) {
      EXPECT_EQ(componentTypes(*circuit).count(std::string(siliconComponent)), 1);
      EXPECT_EQ(componentTypes(*circuit).count("WireSplitter"), 0);
      EXPECT_EQ(componentTypes(*circuit).count("WireMerger"), 0);
      for (unsigned int a = 0; a < 8; ++a) {
        for (unsigned int b = 0; b < 8; ++b) {
          SCOPED_TRACE(std::format("{} with a={} b={}", expression, a, b));
          EXPECT_EQ(evaluateBinaryCircuit(circuit, a, b),
                    valueFor(1, expected(a != 0, b != 0)));
        }
      }
    }
  };

  verify("a && b", "$logic_and", "AndGate",
         [](const bool a, const bool b) { return a && b; });
  verify("a || b", "$logic_or", "OrGate",
         [](const bool a, const bool b) { return a || b; });
}

TEST(YosysToolTest, ImportsLogicalNotAsOneGate)
{
  constexpr std::string_view SOURCE =
      "module top(input [2:0] a, output y); assign y = !a; endmodule";
  auto circuit = std::make_shared<Circuit>(importVerilog(SOURCE, "top"));

  EXPECT_EQ(componentTypes(*circuit).count("NotGate"), 1);
  EXPECT_EQ(componentTypes(*circuit).count("OrGate"), 0);
  EXPECT_EQ(componentTypes(*circuit).count("WireSplitter"), 0);
  ASSERT_TRUE(findComponent<NotGate>(*circuit));
  EXPECT_EQ(findComponent<NotGate>(*circuit)->getPropertyValue<int>("size"), 1);
  const auto serialized = SILICON::yosys::serialize(*circuit, "top");
  EXPECT_NE(serialized.find("$logic_not"), std::string::npos);

  const auto input  = findNamedComponent<DummyBusInputComponent>(*circuit, "a");
  const auto output = findNamedComponent<DummyOutputComponent>(*circuit, "y");
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);

  input->setBusValue(valueFor(input->outputBuses()[0], 0));
  Simulator simulator(circuit);
  ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), valueFor(1, 1));

  ASSERT_EQ(
      simulator.setBus(input->outputBuses()[0], valueFor(input->outputBuses()[0], 2)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), valueFor(1, 0));
}

TEST(YosysToolTest, KeepsAluLogicalOperationsAndVectorNotCompact)
{
  constexpr std::string_view SOURCE = R"(
    module alu(
      input [2:0] opcode,
      input [7:0] OperandA,
      input B,
      output reg [7:0] result
    );
      always @* begin
        case (opcode)
          3'b000: result = OperandA + B;
          3'b001: result = OperandA - B;
          3'b100: result = OperandA && B;
          3'b101: result = OperandA || B;
          3'b110: result = ~OperandA;
          3'b111: result = OperandA ^ OperandA;
          default: result = 0;
        endcase
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "alu");
  const auto    types   = componentTypes(circuit);
  EXPECT_EQ(types.count("AndGate"), 1);
  EXPECT_EQ(types.count("OrGate"), 1);
  EXPECT_EQ(types.count("NotGate"), 1);
  EXPECT_EQ(types.count("Multiplexer"), 1);
  EXPECT_LT(types.size(), 25);
  ASSERT_TRUE(findComponent<NotGate>(circuit));
  EXPECT_EQ(findComponent<NotGate>(circuit)->getPropertyValue<int>("size"), 8);
}

TEST(YosysToolTest, ImportsOnlyASingleDiscoveredModule)
{
  const auto discover = [](const std::string_view SOURCE) -> std::string {
    const auto modules =
        SILICON::yosys::moduleDependencyGraph(SILICON::verilog::read(SOURCE)).modules();
    if (modules.size() != 1)
      throw std::runtime_error("Verilog source must declare exactly one module");
    return modules.front();
  };

  const std::string_view SOURCE =
      "module sole(input a, output y); assign y = ~a; endmodule";
  const auto top     = discover(SOURCE);
  const auto circuit = importVerilog(SOURCE, top);
  EXPECT_EQ(top, "sole");

  EXPECT_THROW((void)discover(""), std::runtime_error);
  EXPECT_THROW((void)discover("module first; endmodule module second; endmodule"),
               std::runtime_error);
}

TEST(YosysToolTest, ImportsZeroExtendedOutputAsUnsignedExtender)
{
  constexpr std::string_view SOURCE = R"(
    module a(output [7:0] bus_out, input in);
      assign bus_out = { 7'h00, in };
    endmodule
  )";

  const Circuit circuit  = importVerilog(SOURCE, "a");
  const auto    extender = findComponent<Extender>(circuit);
  ASSERT_TRUE(extender);
  EXPECT_EQ(extender->getPropertyValue<int>("inSize"), 1);
  EXPECT_EQ(extender->getPropertyValue<int>("outSize"), 8);
  EXPECT_EQ(extender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UNSIGNED_MODE));
  EXPECT_FALSE(findComponent<ConstantComponent>(circuit));
}

TEST(YosysToolTest, RaisesSharedConstantEqualityComparisonsToDecoder)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #else
  constexpr std::string_view SOURCE = R"(
    module top(input [2:0] select, output [1:0] matches);
      assign matches[0] = select == 3'd1;
      assign matches[1] = 3'd6 == select;
    endmodule
  )";

  auto circuit = std::make_shared<Circuit>(importVerilog(SOURCE, "top"));
  auto decoder = findComponent<Decoder>(*circuit);
  ASSERT_TRUE(decoder);
  EXPECT_EQ(decoder->getPropertyValue<int>("selectionSize"), 3);
  EXPECT_EQ(componentTypes(*circuit).count("Decoder"), 1);

  Simulator simulator(circuit);
  ASSERT_EQ(
      simulator.setBus(decoder->inputBuses()[1], valueFor(decoder->inputBuses()[1], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::HIGH);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::LOW);

  ASSERT_EQ(
      simulator.setBus(decoder->inputBuses()[1], valueFor(decoder->inputBuses()[1], 6)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::LOW);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::HIGH);

  ASSERT_EQ(
      simulator.setBus(decoder->inputBuses()[1], valueFor(decoder->inputBuses()[1], 3)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::LOW);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::LOW);
  #endif
}

TEST(YosysToolTest, LegalizationAcceptsTechnologyMapPathWithSpaces)
{
#ifndef SILICON_TEST_YOSYS_PLUGIN_PATH
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#else
  const auto directory = std::filesystem::temp_directory_path()
      / std::format("silicon technology map {}",
                    std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(directory);
  try {
    // This design needs no technology cells, but the pipeline must still load
    // the map through the nested techmap invocation.
    { std::ofstream file(directory / "silicon_cells.v"); file << "// empty library\n"; }
    { std::ofstream file(directory / "silicon_techmap.v"); file << "// empty map\n"; }
    const SILICON::yosys::ToolOptions options{
        .executable = std::nullopt,
        .technologyLibraryDirectory = directory};
    const auto raw = SILICON::yosys::readVerilog(
        "module top(input a, output y); assign y = a; endmodule", options);
    const auto legalized = SILICON::yosys::elaborateHierarchy(raw, options);
    EXPECT_NO_THROW((void)SILICON::yosys::deserialize(legalized, "top"));
  } catch (...) {
    std::filesystem::remove_all(directory);
    throw;
  }
  std::filesystem::remove_all(directory);
#endif
}

TEST(YosysToolTest, PmgenMatchesSelectedPmuxWithUnselectedPredicates)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_PATH
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #else
  constexpr std::string_view SOURCE = R"(
    module top(
      input [1:0] select,
      input [3:0] lane0,
      input [3:0] lane1,
      input [3:0] lane2,
      input [3:0] lane3,
      output reg [3:0] y
    );
      always @* begin
        case (select)
          2'd0: y = lane0;
          2'd1: y = lane1;
          2'd2: y = lane2;
          2'd3: y = lane3;
        endcase
      end
    endmodule
  )";

  EXPECT_NO_THROW(runPluginScript(SOURCE,
                                  "hierarchy -check -top top\n"
                                  "proc\n"
                                  "muxpack\n"
                                  "select top/t:$pmux\n"
                                  "silicon_pmux_bmux\n"
                                  "select -assert-count 1 top/t:$bmux\n"
                                  "select -assert-count 0 top/t:$pmux\n",
                                  "selected_pmux"));
  #endif
}

TEST(YosysToolTest, PmgenLeavesInvalidGroupsAndRaisesValidSignedGroup)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_PATH
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #else
  constexpr std::string_view SOURCE = R"(
    module top(
      input [2:0] first,
      input [2:0] second,
      input signed [2:0] signed_select,
      output [5:0] matches
    );
      assign matches[0] = first == 3'd1;
      assign matches[1] = first == 3'd3;
      assign matches[2] = first == 3'd3;
      assign matches[3] = second == 3'd2;
      assign matches[4] = signed_select == 3'sd1;
      assign matches[5] = signed_select == 3'sd2;
    endmodule
  )";

  EXPECT_NO_THROW(runPluginScript(SOURCE,
                                  "hierarchy -check -top top\n"
                                  "proc\n"
                                  "silicon_eq_decoder\n"
                                  "select -assert-count 4 top/t:$eq\n"
                                  "select -assert-count 1 top/t:$demux\n",
                                  "invalid_eq_groups"));
  #endif
}

TEST(YosysToolTest, FoldsPrivateClockedRomAddressInPlugin)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_PATH
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #else
  constexpr std::string_view SOURCE = R"(
    module top(input clk, input en, input [1:0] addr, output [7:0] data);
      reg [1:0] saved_addr;
      reg [7:0] rom [0:3];
      initial begin
        rom[0] = 8'h12; rom[1] = 8'h34;
        rom[2] = 8'h56; rom[3] = 8'h78;
      end
      always @(posedge clk) if (en) saved_addr <= addr;
      assign data = rom[saved_addr];
    endmodule
  )";
  EXPECT_NO_THROW(runPluginScript(SOURCE,
                                  "hierarchy -check -top top\n"
                                  "proc\n"
                                  "memory_collect\n"
                                  "opt -nosdff\n"
                                  "memory_dff\n"
                                  "select -assert-count 1 top/t:$dffe\n"
                                  "select -assert-count 1 top/t:$mux\n"
                                  "silicon_memrd_address\n"
                                  "select -assert-count 0 top/t:$dffe\n"
                                  "select -assert-count 0 top/t:$mux\n"
                                  "select -assert-count 1 top/t:$mem_v2\n",
                                  "private_rom_address"));
  #endif
}

TEST(YosysToolTest, KeepsRomAddressRegisterWithExternalConsumer)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_PATH
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #else
  constexpr std::string_view SOURCE = R"(
    module top(input clk, input en, input [1:0] addr,
               output [1:0] saved, output [7:0] data);
      reg [1:0] saved_addr;
      reg [7:0] rom [0:3];
      initial begin
        rom[0] = 8'h12; rom[1] = 8'h34;
        rom[2] = 8'h56; rom[3] = 8'h78;
      end
      always @(posedge clk) if (en) saved_addr <= addr;
      assign saved = saved_addr;
      assign data = rom[saved_addr];
    endmodule
  )";
  EXPECT_NO_THROW(runPluginScript(SOURCE,
                                  "hierarchy -check -top top\n"
                                  "proc\n"
                                  "memory_collect\n"
                                  "opt -nosdff\n"
                                  "memory_dff\n"
                                  "silicon_memrd_address\n"
                                  "select -assert-count 1 top/t:$dffe\n"
                                  "select -assert-count 1 top/t:$mux\n",
                                  "shared_rom_address"));
  #endif
}

TEST(YosysToolTest, LowersPriorityMuxCellsBeforeImport)
{
  constexpr std::string_view SOURCE = R"(
    module top(
      input [1:0] fallback,
      input [1:0] lane0,
      input [1:0] lane1,
      input [1:0] lane2,
      input [2:0] select,
      output reg [1:0] y
    );
      always @* begin
        y = fallback;
        (* parallel_case *)
        casez (select)
          3'b??1: y = lane0;
          3'b?1?: y = lane1;
          3'b1??: y = lane2;
        endcase
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "top");
  EXPECT_GT(componentTypes(circuit).count("Multiplexer"), 0);
}

TEST(YosysToolTest, ImportsSequentialVerilog)
{
  constexpr std::string_view SOURCE  = R"(
    module storage(input d, input clk, output reg q);
      always @(posedge clk)
        q <= d;
    endmodule
  )";
  const Circuit              circuit = importVerilog(SOURCE, "storage");
  EXPECT_TRUE(componentTypes(circuit).contains("DFlipFlop"));
}

TEST(YosysToolTest, FoldsSparseCaseIntoOneWideMultiplexer)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #endif
  constexpr std::string_view SOURCE = R"(
    module top(
      input [7:0] a,
      input [7:0] b,
      input [3:0] select,
      output reg [7:0] y
    );
      always @* begin
        case (select)
          4'h1: y = a;
          4'h6: y = b;
          4'hd: y = a ^ b;
          default: y = 8'h00;
        endcase
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "top");
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  EXPECT_EQ(componentTypes(circuit).count("Decoder"), 0);
  EXPECT_EQ(componentTypes(circuit).count("OrGate"), 0);
}

TEST(YosysToolTest, KeepsSynchronousResetAsScalarMuxAndDFlipFlop)
{
  constexpr std::string_view SOURCE = R"(
    module sdff_test(
      input wire clk,
      input wire rst,
      input wire d,
      output reg q
    );
      always @(posedge clk) begin
        if (rst)
          q <= 1'b0;
        else
          q <= d;
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "sdff_test");
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  EXPECT_EQ(componentTypes(circuit).count("DFlipFlop"), 1);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 0);

  const auto mux = findComponent<Multiplexer>(circuit);
  ASSERT_TRUE(mux);
  ASSERT_EQ(mux->inputBuses().size(), 3);
  EXPECT_EQ(mux->inputBuses()[0].size(), 1);
  EXPECT_EQ(mux->inputBuses()[1].size(), 1);
  EXPECT_EQ(mux->inputBuses()[2].size(), 1);
}

TEST(YosysToolTest, FoldsExhaustiveCaseIntoOneWideMultiplexer)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #endif
  constexpr std::string_view SOURCE = R"(
    module mux(
      input [3:0] bus_1,
      input [3:0] bus_2,
      input [3:0] bus_3,
      input [3:0] bus_4,
      input [1:0] bus_in,
      output reg [3:0] bus_out
    );
      always @* begin
        case (bus_in)
          2'h0: bus_out = bus_1;
          2'h1: bus_out = bus_2;
          2'h2: bus_out = bus_3;
          2'h3: bus_out = bus_4;
          default: bus_out = 4'hx;
        endcase
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "mux");
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  EXPECT_EQ(componentTypes(circuit).count("Decoder"), 0);
  EXPECT_EQ(componentTypes(circuit).count("OrGate"), 0);

  const auto mux = findComponent<Multiplexer>(circuit);
  ASSERT_TRUE(mux);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 2);
  EXPECT_EQ(mux->getPropertyValue<int>("busSize"), 4);
  EXPECT_EQ(mux->inputBuses().size(), 5);
}

TEST(YosysToolTest, ImportsCaseLiteralLanesAsSizedConstants)
{
  #ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
  #endif
  constexpr std::string_view SOURCE = R"(
    module my_mux(input [1:0] a, input [3:0] b, c, output reg [3:0] o);
      always @(a, b, c) begin
        case (a)
          2'b00: o = b;
          2'b01: o = c;
          2'b11: o = 4;
          default: o = 3;
        endcase
      end
    endmodule
  )";

  const Circuit circuit = importVerilog(SOURCE, "my_mux");
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  EXPECT_EQ(componentTypes(circuit).count("ConstantComponent"), 2);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 0);
  EXPECT_EQ(componentTypes(circuit).count("WireSplitter"), 0);

  std::set<BusValue> values;
  for (const auto& component : componentsIn(circuit)) {
    if (auto constant = std::dynamic_pointer_cast<ConstantComponent>(component)) {
      EXPECT_EQ(constant->getPropertyValue<int>("size"), 4);
      values.insert(*constant->getPropertyValue<BusValue>("value"));
    }
  }
  EXPECT_EQ(values,
            (std::set<BusValue>{busValueFromBits("0011"), busValueFromBits("0100")}));
}

TEST(YosysTest, ImportsScalarActiveHighNativeDlatch)
{
  using SILICON::yosys::SerializationContext;

  auto latch =
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>());
  auto  design                = exportComponent(latch);
  auto& cell                  = onlyModule(design)["cells"].begin().value();
  cell["type"]                = "$dlatch";
  cell["parameters"]["WIDTH"] = SerializationContext::parameter(1);
  cell["connections"].erase("QN");
  cell["port_directions"].erase("QN");

  EXPECT_TRUE(findComponent<DLatch>(SILICON::yosys::deserialize(design.dump())));

  cell["parameters"]["EN_POLARITY"] = SerializationContext::parameter(0, 1);
  EXPECT_THROW((void)SILICON::yosys::deserialize(design.dump()), std::runtime_error);

  cell["parameters"]["EN_POLARITY"] = SerializationContext::parameter(1, 1);
  cell["parameters"]["WIDTH"]       = SerializationContext::parameter(2);
  EXPECT_THROW((void)SILICON::yosys::deserialize(design.dump()), std::runtime_error);
}

#endif
