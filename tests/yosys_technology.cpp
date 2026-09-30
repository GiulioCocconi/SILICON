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

#ifdef SILICON_TEST_YOSYS_EXECUTABLE

TEST(YosysToolTest, MapsVerilogToSiliconTechnologyCells)
{
  const auto mappedTypes = [](const std::string_view source, const std::string_view top) {
    return cellTypes(onlyModule(nlohmann::json::parse(
        SILICON::yosys::serialize(importVerilog(source, top), top))));
  };

  EXPECT_EQ(mappedTypes(R"(
      module top(input d, input en, output reg q);
        always @* if (en) q <= d;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_DLATCH"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input d, input clk, output reg q);
        always @(posedge clk) q <= d;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_DFF"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input d, input clk, output reg q);
        always @(negedge clk) q <= d;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_DFF"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input d, input en, input clk, output reg q);
        always @(posedge clk) if (en) q <= d;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_DFFE"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input en, input clk, output reg [3:0] q);
        always @(posedge clk) if (en) q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$pos", "SILICON_PIPO"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input clk, output reg [3:0] q);
        always @(negedge clk) q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$not", "$pos", "$pos", "SILICON_PIPO"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input en, input clk, output reg [3:0] q);
        always @(posedge clk) if (!en) q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$not", "$pos", "SILICON_PIPO"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input clear, input clk, output reg [3:0] q);
        always @(posedge clk or posedge clear)
          if (clear) q <= 4'b0;
          else q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$pos", "SILICON_PIPO"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input clear, input clk, output reg [3:0] q);
        always @(posedge clk or negedge clear)
          if (!clear) q <= 4'b0;
          else q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$not", "$pos", "SILICON_PIPO"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input en, input clear, input clk, output reg [3:0] q);
        always @(posedge clk or posedge clear)
          if (clear) q <= 4'b0;
          else if (en) q <= d;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_PIPO"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] d, input en, input clear, input clk, output reg [3:0] q);
        always @(posedge clk or negedge clear)
          if (!clear) q <= 4'b0;
          else if (!en) q <= d;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$not", "$not", "SILICON_PIPO"}));
  const auto asyncTypes = mappedTypes(R"(
      module top(input d, input clk, input set, input clear, output reg q);
        always @(posedge clk or posedge set or posedge clear)
          if (clear) q <= 1'b0;
          else if (set) q <= 1'b1;
          else q <= d;
      endmodule
    )",
                                      "top");
  EXPECT_TRUE(asyncTypes.contains("SILICON_DFFSR"));
  EXPECT_EQ(mappedTypes(R"(
      module top(input [3:0] a, input [3:0] b, output [4:0] y);
        assign y = a + b;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$pos", "$pos", "SILICON_ADDER"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(
        input [3:0] a, input [3:0] b,
        output [3:0] and_y, output [3:0] or_y, output [3:0] xor_y
      );
        assign and_y = a & b;
        assign or_y = a | b;
        assign xor_y = a ^ b;
      endmodule
    )",
                        "top"),
            (std::multiset<std::string>{"$and", "$or", "$xor"}));
  EXPECT_EQ(mappedTypes(R"(
      module top(input a, input b, output sum, output cout);
        assign sum = a ^ b;
        assign cout = a & b;
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_HALF_ADDER"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input a, input b, input cin, output sum, output cout);
        assign sum = a ^ b ^ cin;
        assign cout = (a & b) | (a & cin) | (b & cin);
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_FULL_ADDER"});
  EXPECT_EQ(mappedTypes(R"(
      module top(input j, input k, input clk, output q, output qn);
        SILICON_JKFF #(
          .CLK_POLARITY(1), .SET_POLARITY(1), .CLR_POLARITY(1)
        ) cell(.J(j), .K(k), .CLK(clk), .SET(1'b0), .CLR(1'b0), .Q(q), .QN(qn));
      endmodule
    )",
                        "top"),
            std::multiset<std::string>{"SILICON_JKFF"});
}

TEST(YosysToolTest, ImportedTechnologyCellsPreserveRepresentativeBehavior)
{
  constexpr std::string_view latchSource = R"(
    module top(input d, input en, output reg q);
      always @* if (en) q <= d;
    endmodule
  )";
  auto latchCircuit = std::make_shared<Circuit>(importVerilog(latchSource, "top"));
  auto latch        = findComponent<DLatch>(*latchCircuit);
  ASSERT_TRUE(latch);
  Simulator latchSimulator(latchCircuit);
  EXPECT_EQ(
      latchSimulator.setBus(latch->inputBuses()[0], valueFor(latch->inputBuses()[0], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(latch->outputBuses()[0][0]->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(
      latchSimulator.setBus(latch->inputBuses()[1], valueFor(latch->inputBuses()[1], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(latch->outputBuses()[0][0]->getCurrentState(), State::HIGH);
  EXPECT_EQ(latch->outputBuses()[1][0]->getCurrentState(), State::LOW);
  EXPECT_EQ(
      latchSimulator.setBus(latch->inputBuses()[1], valueFor(latch->inputBuses()[1], 0)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      latchSimulator.setBus(latch->inputBuses()[0], valueFor(latch->inputBuses()[0], 0)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(latch->outputBuses()[0][0]->getCurrentState(), State::HIGH);

  const auto simulateDff = [](const std::string_view edge) {
    const auto source = std::format(
        R"(
          module top(input d, input clk, output reg q);
            always @({} clk) q <= d;
          endmodule
        )",
        edge);
    auto circuit = std::make_shared<Circuit>(importVerilog(source, "top"));
    auto dff     = findComponent<DFlipFlop>(*circuit);
    if (!dff)
      throw std::runtime_error("Mapped D flip-flop was not reconstructed");
    Simulator  simulator(circuit);
    const bool positive = edge == "posedge";
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[1],
                               valueFor(dff->inputBuses()[1], positive ? 0 : 1)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[0], valueFor(dff->inputBuses()[0], 1)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[1],
                               valueFor(dff->inputBuses()[1], positive ? 1 : 0)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(dff->outputBuses()[0][0]->getCurrentState(), State::HIGH);
    EXPECT_EQ(dff->outputBuses()[1][0]->getCurrentState(), State::LOW);
  };
  simulateDff("posedge");
  simulateDff("negedge");

  constexpr std::string_view enabledSource = R"(
    module top(input d, input en, input clk, output reg q);
      always @(posedge clk) if (en) q <= d;
    endmodule
  )";
  auto enabledCircuit = std::make_shared<Circuit>(importVerilog(enabledSource, "top"));
  auto dffe           = findComponent<EFlipFlop>(*enabledCircuit);
  ASSERT_TRUE(dffe);
  Simulator enabledSimulator(enabledCircuit);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[2], valueFor(dffe->inputBuses()[2], 0)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[0], valueFor(dffe->inputBuses()[0], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[1], valueFor(dffe->inputBuses()[1], 0)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[2], valueFor(dffe->inputBuses()[2], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(dffe->outputBuses()[0][0]->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[2], valueFor(dffe->inputBuses()[2], 0)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[1], valueFor(dffe->inputBuses()[1], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(
      enabledSimulator.setBus(dffe->inputBuses()[2], valueFor(dffe->inputBuses()[2], 1)),
      Simulator::RunResult::Completed);
  EXPECT_EQ(dffe->outputBuses()[0][0]->getCurrentState(), State::HIGH);

  constexpr std::string_view adderSource = R"(
    module top(input [3:0] a, input [3:0] b, output [4:0] y);
      assign y = a + b;
    endmodule
  )";
  auto adderCircuit = std::make_shared<Circuit>(importVerilog(adderSource, "top"));
  std::map<std::string, std::shared_ptr<DummyBusInputComponent>> inputs;
  std::shared_ptr<DummyBusOutputComponent>                       output;
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(adderCircuit->getGraph()))) {
    const auto& component = adderCircuit->getGraph()[vertex].component;
    if (auto input = std::dynamic_pointer_cast<DummyBusInputComponent>(component))
      inputs.emplace(input->getPropertyValue<std::string>("name").value_or(""), input);
    if (auto candidate = std::dynamic_pointer_cast<DummyBusOutputComponent>(component))
      output = std::move(candidate);
  }
  ASSERT_TRUE(inputs.contains("a"));
  ASSERT_TRUE(inputs.contains("b"));
  ASSERT_TRUE(output);
  inputs.at("a")->setBusValue(valueFor(inputs.at("a")->outputBuses()[0], 15));
  inputs.at("b")->setBusValue(valueFor(inputs.at("b")->outputBuses()[0], 1));
  Simulator adderSimulator(adderCircuit);
  ASSERT_EQ(adderSimulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(),
            valueFor(output->inputBuses()[0], 16));
}

TEST(YosysToolTest, ExportsAdderAsBehavioralExpression)
{
  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4),
                                            std::make_shared<Wire>());
  const auto verilog = SILICON::verilog::write(circuitWithBoundaryPorts(adder), "top");
  EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(verilog.find(" + "), std::string::npos);

  const Circuit restored      = importVerilog(verilog, "top");
  const auto    restoredAdder = findComponent<AdderNBits>(restored);
  ASSERT_TRUE(restoredAdder);
  EXPECT_EQ(restoredAdder->getPropertyValue<int>("size"), 4);
}

TEST(YosysToolTest, ExportsAdderWithUnusedCarryOutput)
{
  Bus     a(4);
  Bus     b(4);
  Bus     sum(4);
  auto    adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{a, b}, sum, nullptr);
  Circuit circuit(Component_set{adder, std::make_shared<DummyBusInputComponent>(a, "a"),
                                std::make_shared<DummyBusInputComponent>(b, "b"),
                                std::make_shared<DummyBusOutputComponent>(sum, "sum")},
                  false);

  const auto verilog = SILICON::verilog::write(circuit, "top");
  EXPECT_NE(verilog.find(" + "), std::string::npos);
  EXPECT_NO_THROW((void)importVerilog(verilog, "top"));
}

TEST(YosysToolTest, TechnologyCellsExportAsBehavioralVerilog)
{
  auto component = std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>());
  const Circuit circuit = circuitWithBoundaryPorts(component);
  const auto    verilog = SILICON::verilog::write(circuit, "top");
  EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(verilog.find("assign output_0"), std::string::npos);
  EXPECT_NE(verilog.find("assign output_1"), std::string::npos);
  EXPECT_EQ(verilog.find("_auto_"), std::string::npos);
  EXPECT_EQ(verilog.find("$silicon"), std::string::npos);
  EXPECT_EQ(verilog.find("$techmap"), std::string::npos);
  EXPECT_EQ(verilog.find("silicon_cells.v"), std::string::npos);
  EXPECT_EQ(verilog.find("wire _00_"), std::string::npos);
  EXPECT_NE(verilog.find("module top(input input_0, input input_1, input input_2, "
                         "output output_0, output output_1);"),
            std::string::npos);
  EXPECT_NE(verilog.find("assign output_0 = input_0 ^ input_1 ^ input_2;"),
            std::string::npos);
  EXPECT_NE(verilog.find("(input_0 & input_1)"), std::string::npos);
  EXPECT_NE(verilog.find("(input_0 & input_2)"), std::string::npos);
  EXPECT_NE(verilog.find("(input_1 & input_2)"), std::string::npos);

  const Circuit restored = importVerilog(verilog, "top");
  EXPECT_TRUE(componentTypes(restored).contains("FullAdder"));
  EXPECT_EQ(cellTypes(onlyModule(
                nlohmann::json::parse(SILICON::yosys::serialize(restored, "top")))),
            std::multiset<std::string>{"SILICON_FULL_ADDER"});

  for (unsigned value = 0; value < 8; ++value) {
    auto simulated = std::make_shared<Circuit>(importVerilog(verilog, "top"));
    auto adder     = findComponent<FullAdder>(*simulated);
    ASSERT_TRUE(adder);
    Simulator simulator(simulated);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[0],
                               valueFor(adder->inputBuses()[0], value & 1U)),
              Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[1],
                               valueFor(adder->inputBuses()[1], (value >> 1U) & 1U)),
              Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[2],
                               valueFor(adder->inputBuses()[2], (value >> 2U) & 1U)),
              Simulator::RunResult::Completed);
    const unsigned result =
        static_cast<unsigned>(adder->outputBuses()[0][0]->getCurrentState()
                              == State::HIGH)
        | (static_cast<unsigned>(adder->outputBuses()[1][0]->getCurrentState()
                                 == State::HIGH)
           << 1U);
    EXPECT_EQ(result, (value & 1U) + ((value >> 1U) & 1U) + ((value >> 2U) & 1U));
  }

  auto dff = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), nullptr, nullptr,
      std::make_shared<Wire>(), std::make_shared<Wire>());
  dff->setProperty("triggerEdge", std::string("NET"));
  Circuit dffBoundary(
      Component_set{
          dff,
          std::make_shared<DummyInputComponent>(dff->inputBuses()[0], "d"),
          std::make_shared<DummyInputComponent>(dff->inputBuses()[1], "clk"),
          std::make_shared<DummyOutputComponent>(dff->outputBuses()[0], "q"),
          std::make_shared<DummyOutputComponent>(dff->outputBuses()[1], "qn"),
      },
      false);
  const auto dffVerilog = SILICON::verilog::write(dffBoundary, "top");
  EXPECT_EQ(dffVerilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(dffVerilog.find("always @"), std::string::npos);
  auto dffCircuit  = std::make_shared<Circuit>(importVerilog(dffVerilog, "top"));
  auto restoredDff = findComponent<DFlipFlop>(*dffCircuit);
  ASSERT_TRUE(restoredDff);
  EXPECT_EQ(restoredDff->getPropertyValue<std::string>("triggerEdge"),
            std::optional<std::string>("NET"));
  Simulator dffSimulator(dffCircuit);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[1],
                                valueFor(restoredDff->inputBuses()[1], 1)),
            Simulator::RunResult::Completed);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[0],
                                valueFor(restoredDff->inputBuses()[0], 1)),
            Simulator::RunResult::Completed);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[1],
                                valueFor(restoredDff->inputBuses()[1], 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(restoredDff->outputBuses()[0][0]->getCurrentState(), State::HIGH);
  EXPECT_EQ(restoredDff->outputBuses()[1][0]->getCurrentState(), State::LOW);
}

TEST(YosysToolTest, ReportsMissingTechnologyLibrary)
{
  const auto missing = std::filesystem::temp_directory_path()
                       / "silicon_yosys_library_that_does_not_exist";
  try {
    const SILICON::yosys::ToolOptions options{
        .executable = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
        .technologyLibraryDirectory = missing,
    };
    (void)SILICON::yosys::elaborateHierarchy("{}", options);
    FAIL() << "Expected a missing technology library to be rejected";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("resource-discovery phase"),
              std::string::npos);
    EXPECT_NE(std::string(error.what()).find(missing.string()), std::string::npos);
  }
}

TEST(YosysToolTest, UnifiedTechnologyCellSourceSupportsSimulationMode)
{
  EXPECT_NO_THROW((void)SILICON::yosys::runScript(
      std::format("read_verilog \"{}/silicon_cells.v\"\n",
                  SILICON_TEST_YOSYS_RESOURCE_DIR),
      {.executable                 = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
       .technologyLibraryDirectory = std::nullopt}));
}

TEST(YosysToolTest, ExportsParseableStructuralVerilog)
{
  auto    a = std::make_shared<Wire>();
  auto    b = std::make_shared<Wire>();
  auto    y = std::make_shared<Wire>();
  Circuit circuit(
      Component_set{
          std::make_shared<DummyInputComponent>(Bus{a}, "a"),
          std::make_shared<DummyInputComponent>(Bus{b}, "b"),
          std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, y),
          std::make_shared<DummyOutputComponent>(Bus{y}, "y"),
      },
      false);

  const auto verilog = SILICON::verilog::write(circuit, "top");
  EXPECT_NE(verilog.find("module top(input a, input b, output y);"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  input a;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  wire a;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  output y;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  wire y;"), std::string::npos);
  EXPECT_EQ(verilog.find("_auto_"), std::string::npos);
  EXPECT_EQ(verilog.find("$silicon"), std::string::npos);
  EXPECT_NO_THROW({
    const Circuit reparsed = importVerilog(verilog, "top");
    EXPECT_TRUE(componentTypes(reparsed).contains("AndGate"));
  });
}

TEST(VerilogPostprocessingTest, NormalizesPortsAndInlinesGeneratedNets)
{
  constexpr std::string_view raw = R"(module top(a, y);
  input a;
  output y;
  wire a;
  wire y;
  wire _1_;
  assign _1_ = a & a;
  assign y = _1_ | a;
endmodule
)";

  const auto normalized = SILICON::verilog::postprocess(raw);
  EXPECT_NE(normalized.find("module top(input a, output y);"), std::string::npos);
  EXPECT_EQ(normalized.find("wire a;"), std::string::npos);
  EXPECT_EQ(normalized.find("wire y;"), std::string::npos);
  EXPECT_EQ(normalized.find("wire _1_;"), std::string::npos);
  EXPECT_EQ(normalized.find("assign _1_"), std::string::npos);
  EXPECT_NE(normalized.find("assign y = (a & a) | a;"), std::string::npos);
}

TEST(YosysToolTest, ExportsWideMuxAsCaseStatement)
{
  auto mux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 2);
  Circuit circuit = circuitWithBoundaryPorts(mux);

  const auto verilog = SILICON::verilog::write(circuit, "top");
  #ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  EXPECT_NE(verilog.find("output reg [3:0] output_0"), std::string::npos);
  EXPECT_NE(verilog.find("case (input_4)"), std::string::npos);
  EXPECT_NE(verilog.find("2'h0:"), std::string::npos);
  EXPECT_NE(verilog.find("output_0 = input_0"), std::string::npos);
  EXPECT_NE(verilog.find("default:"), std::string::npos);
  EXPECT_EQ(verilog.find("$auto$verilog_backend"), std::string::npos);
  #else
  EXPECT_NE(verilog.find("$auto$bmuxmap"), std::string::npos);
  #endif
  #ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  EXPECT_EQ(verilog.find("$auto$bmuxmap"), std::string::npos);
  #endif
  EXPECT_NO_THROW((void)importVerilog(verilog, "top"));
}

TEST(YosysToolTest, ExportsTwoInputNorWithoutIntermediateNets)
{
  auto    set   = std::make_shared<Wire>();
  auto    reset = std::make_shared<Wire>();
  auto    q     = std::make_shared<Wire>();
  auto    nq    = std::make_shared<Wire>();
  Circuit circuit(
      Component_set{
          std::make_shared<DummyInputComponent>(Bus{set}, "set"),
          std::make_shared<DummyInputComponent>(Bus{reset}, "reset"),
          std::make_shared<NorGate>(std::vector<Wire_ptr>{set, q}, nq),
          std::make_shared<NorGate>(std::vector<Wire_ptr>{nq, reset}, q),
          std::make_shared<DummyOutputComponent>(Bus{q}, "q"),
          std::make_shared<DummyOutputComponent>(Bus{nq}, "nq"),
      },
      false);

  const auto verilog = SILICON::verilog::write(circuit, "sr_latch");
  EXPECT_NE(verilog.find("assign nq = ~("), std::string::npos);
  EXPECT_NE(verilog.find("assign q = ~("), std::string::npos);
  EXPECT_EQ(verilog.find("NorGate_"), std::string::npos);
  EXPECT_NO_THROW((void)importVerilog(verilog, "sr_latch"));
}

TEST(YosysToolTest, ImportsVectorDffAsRegister)
{
  constexpr std::string_view source = R"(
    module top(input clk, input [3:0] d, output reg [3:0] q);
      always @(posedge clk) q <= d;
    endmodule
  )";

  const Circuit imported = importVerilog(source, "top");
  const auto    reg      = findComponent<Register>(imported);
  ASSERT_TRUE(reg);
  EXPECT_EQ(reg->getPropertyValue<int>("size"), 4);
  EXPECT_EQ(reg->getPropertyValue<std::string>("inputType"),
            std::optional<std::string>(Register::ParallelType));
  EXPECT_EQ(reg->getPropertyValue<std::string>("outputType"),
            std::optional<std::string>(Register::ParallelType));
}

TEST(YosysToolTest, VerilogCircuitVerilogRoundTripPreservesBehavior)
{
  constexpr std::string_view source       = R"(
    module top(input [1:0] a, output y);
      assign y = a[0] & a[1];
    endmodule
  )";
  const auto                 firstCircuit = importVerilog(source, "top");
  EXPECT_EQ(componentTypes(firstCircuit).count("WireSplitter"), 1);
  EXPECT_EQ(componentTypes(firstCircuit).count("WireMerger"), 0);
  const auto roundTrippedVerilog = SILICON::verilog::write(firstCircuit, "top");
  EXPECT_EQ(roundTrippedVerilog.find("_auto_"), std::string::npos);
  EXPECT_EQ(roundTrippedVerilog.find("$silicon"), std::string::npos);

  const auto evaluate = [](Circuit circuit, const std::uint64_t inputValue) {
    auto sharedCircuit = std::make_shared<Circuit>(std::move(circuit));
    std::shared_ptr<DummyBusInputComponent> input;
    Component_ptr                           output;
    for (const auto vertex :
         boost::make_iterator_range(boost::vertices(sharedCircuit->getGraph()))) {
      const auto& component = sharedCircuit->getGraph()[vertex].component;
      if (auto candidate = std::dynamic_pointer_cast<DummyBusInputComponent>(component))
        input = std::move(candidate);
      if (std::dynamic_pointer_cast<DummyOutputComponent>(component)
          || std::dynamic_pointer_cast<DummyBusOutputComponent>(component))
        output = component;
    }
    if (!input || !output)
      throw std::runtime_error("Round-trip circuit boundary was not preserved");
    input->setBusValue(valueFor(input->outputBuses()[0], inputValue));
    Simulator simulator(sharedCircuit);
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("Round-trip circuit simulation did not complete");
    return output->inputBuses()[0].getCurrentValue();
  };

  for (std::uint64_t value = 0; value < 4; ++value) {
    SCOPED_TRACE(std::format("input {}", value));
    EXPECT_EQ(evaluate(importVerilog(source, "top"), value),
              evaluate(importVerilog(roundTrippedVerilog, "top"), value));
  }
}

TEST(YosysToolTest, VerilogConversionNamesCircuitAfterSelectedModule)
{
  const SILICON::project::Document source{
      "code/design.v",
      "module inverter(input a, output y); assign y = ~a; endmodule\n"
      "module alu(input a, output y); inverter child(.a(a), .y(y)); endmodule\n"};
  const std::vector<SILICON::project::Document> documents{source};
  SILICON::project::ProjectContext              project;
  project.setDocuments(documents);
  SILICON::project::ProjectCircuitResolver resolver{
      project, SILICON::core::ComponentRegistry::instance()};

  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Circuit, documents,
      SILICON::core::ComponentRegistry::instance(), resolver);
  ASSERT_EQ(prepared.choices.size(), 2);

  const std::array<std::string, 1> selected{"alu"};
  const auto                       converted = prepared.execute(selected);
  EXPECT_EQ(converted.activatePath, "circuits/alu.json");
  ASSERT_EQ(converted.documents.size(), 2);
  for (const auto& document : converted.documents) {
    ASSERT_TRUE(std::holds_alternative<Circuit>(document.payload));
    const auto slug = SILICON::project::documentSlugForPath(document.path);
    ASSERT_TRUE(slug);
  }
}

TEST(YosysToolTest, CircuitConversionPreservesModulePortsAndLogic)
{
  auto    wire = std::make_shared<Wire>();
  Circuit circuit(
      Component_set{std::make_shared<DummyInputComponent>(Bus{wire}, "signal_in"),
                    std::make_shared<DummyOutputComponent>(Bus{wire}, "signal_out")},
      false);

  const SILICON::project::Document source{
      "circuits/passthrough.json",
      nlohmann::json{{"circuit", nlohmann::json::parse(circuit.serialize())}}.dump()};
  const std::vector<SILICON::project::Document> documents{source};
  SILICON::project::ProjectContext              project;
  project.setDocuments(documents);
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectCircuitResolver resolver{project, registry};

  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, SILICON::project::DocumentType::Verilog, documents, registry, resolver);
  const auto converted = prepared.execute({});

  ASSERT_EQ(converted.documents.size(), 1);
  ASSERT_TRUE(std::holds_alternative<SILICON::conversion::VerilogSource>(
      converted.documents.front().payload));
  const auto& verilog =
      std::get<SILICON::conversion::VerilogSource>(converted.documents.front().payload)
          .contents;
  EXPECT_NE(verilog.find("module passthrough(input signal_in, output signal_out);"),
            std::string::npos);
  EXPECT_NE(verilog.find("assign signal_out = signal_in;"), std::string::npos);
}
#endif

#ifdef __EMSCRIPTEN__
TEST(YosysToolTest, ExternalOperationsAreUnavailableUnderEmscripten)
{
  const auto expectUnavailable = [](const auto& operation) {
    try {
      operation();
      FAIL() << "Expected external Yosys execution to be unavailable";
    } catch (const std::runtime_error& error) {
      EXPECT_NE(std::string(error.what()).find("Emscripten"), std::string::npos);
      EXPECT_NE(std::string(error.what()).find("platform-availability phase"),
                std::string::npos);
    }
  };

  expectUnavailable([] { (void)SILICON::yosys::runScript("help"); });
  expectUnavailable([] { (void)importVerilog("module top; endmodule", "top"); });
  expectUnavailable([] {
    const Circuit circuit(Component_set{}, false);
    (void)SILICON::verilog::write(circuit, "top");
  });
}

TEST(YosysToolTest, InProcessJsonRemainsAvailableUnderEmscripten)
{
  auto component = std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>());
  Circuit    circuit(component, false);
  const auto json = SILICON::yosys::serialize(circuit, "top");
  EXPECT_NE(json.find("SILICON_HALF_ADDER"), std::string::npos);
  EXPECT_NO_THROW({
    const Circuit restored = SILICON::yosys::deserialize(json);
    EXPECT_EQ(componentTypes(restored), componentTypes(circuit));
  });
}
#endif
