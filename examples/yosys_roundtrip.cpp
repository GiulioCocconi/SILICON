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

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <core/circuit.hpp>
#include <core/io.hpp>
#include <core/register.hpp>
#include <core/serialization/yosys.hpp>
#include <core/wire.hpp>
#include <extraComponents/arithmetic.hpp>

namespace {

constexpr auto CounterWidth = 4;
constexpr auto TopModule    = "counter";

constexpr std::string_view PlainVerilogTechmap = R"(
(* techmap_celltype = "SILICON_ADDER" *)
module _silicon_plain_adder #(
  parameter WIDTH = 1,
  parameter A_SIGNED = 0,
  parameter B_SIGNED = 0
) (
  input [WIDTH-1:0] A,
  input [WIDTH-1:0] B,
  output [WIDTH-1:0] SUM,
  output COUT
);
  wire _TECHMAP_FAIL_ = A_SIGNED || B_SIGNED;
  assign {COUT, SUM} = {1'b0, A} + {1'b0, B};
endmodule

(* techmap_celltype = "SILICON_REGISTER" *)
module _silicon_plain_register #(
  parameter WIDTH = 2,
  parameter INPUT_PARALLEL = 1,
  parameter OUTPUT_PARALLEL = 1,
  parameter CLK_POLARITY = 1,
  parameter EN_POLARITY = 1,
  parameter CLR_POLARITY = 1,
  parameter LOAD_POLARITY = 1
) (
  input [(INPUT_PARALLEL ? WIDTH : 1)-1:0] DATA,
  input CLK,
  input EN,
  input CLR,
  input LOAD,
  output [(OUTPUT_PARALLEL ? WIDTH : 1)-1:0] OUT
);
  wire _TECHMAP_FAIL_ =
    !(INPUT_PARALLEL && OUTPUT_PARALLEL && CLK_POLARITY && EN_POLARITY && CLR_POLARITY);
  reg [WIDTH-1:0] state;
  assign OUT = state;
  always @(posedge CLK) begin
    if (CLR) state <= {WIDTH{1'b0}};
    else if (EN) state <= DATA;
  end
endmodule
)";

[[nodiscard]] Component_ptr constant(Wire_ptr output, std::string value)
{
  return std::make_shared<ConstantComponent>(std::move(output), std::move(value));
}

[[nodiscard]] std::string quotePath(const std::filesystem::path& path)
{
  std::string quoted = "\"";
  for (const char c : path.string()) {
    if (c == '\\' || c == '"')
      quoted += '\\';
    quoted += c;
  }
  quoted += '"';
  return quoted;
}

void writeFile(const std::filesystem::path& path, const std::string_view contents)
{
  std::ofstream output(path);
  if (!output)
    throw std::runtime_error("Could not open output file: " + path.string());
  output << contents;
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Could not read input file: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::filesystem::path temporaryPath(const std::string_view filename)
{
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path()
         / ("silicon_yosys_roundtrip_" + std::to_string(timestamp) + "_"
            + std::string(filename));
}

[[nodiscard]] Circuit makeCounterCircuit()
{
  Bus count(CounterWidth);
  Bus increment(CounterWidth);
  Bus nextCount(CounterWidth);

  auto clock  = std::make_shared<Wire>();
  auto enable = std::make_shared<Wire>();
  auto clear  = std::make_shared<Wire>();
  auto carry  = std::make_shared<Wire>();

  Component_set components{
      std::make_shared<DummyInputComponent>(Bus{clock}, "clk"),
      std::make_shared<DummyBusOutputComponent>(count, "count"),

      constant(increment[0], "1"),
      constant(increment[1], "0"),
      constant(increment[2], "0"),
      constant(increment[3], "0"),
      constant(enable, "1"),
      constant(clear, "0"),

      std::make_shared<AdderNBits>(std::array<Bus, 2>{count, increment}, nextCount,
                                   carry),
      std::make_shared<Register>(nextCount, clock, enable, clear, count),
  };

  Circuit circuit(components, false);
  circuit.setName(TopModule);
  circuit.setDescription("4-bit counter: next_count = count + 1, sampled by clk.");
  return circuit;
}

[[nodiscard]] std::string makePlainVerilog(const Circuit&                     circuit,
                                           const silicon::yosys::ToolOptions& options)
{
  const auto structuralPath = temporaryPath("structural.v");
  const auto techmapPath    = temporaryPath("plain_map.v");
  const auto plainPath      = temporaryPath("plain.v");

  writeFile(structuralPath, silicon::yosys::exportVerilog(circuit, options));
  writeFile(techmapPath, PlainVerilogTechmap);

  const auto blackBoxLibrary =
      std::filesystem::path(SILICON_YOSYS_RESOURCE_DIR) / "silicon_cells_bb.v";
  (void)silicon::yosys::runScript(
      "read_verilog -lib " + quotePath(blackBoxLibrary) + "\n" + "read_verilog "
          + quotePath(structuralPath) + "\n" + "hierarchy -check -top "
          + std::string(TopModule) + "\n" + "opt_expr t:$pos\n" + "opt_clean -purge\n"
          + "rename -unescape\n" + "techmap -autoproc -map " + quotePath(techmapPath)
          + "\n" + "opt\n" + "rename -hide w:*_TECHMAP_FAIL_\n" + "opt_clean -purge\n"
          + "rename -unescape\n" + "write_verilog -noattr -norename -decimal "
          + quotePath(plainPath) + "\n",
      options);

  const auto plainVerilog = readFile(plainPath);
  std::filesystem::remove(structuralPath);
  std::filesystem::remove(techmapPath);
  std::filesystem::remove(plainPath);
  return plainVerilog;
}

[[nodiscard]] std::size_t componentCount(const Circuit& circuit)
{
  return boost::num_vertices(circuit.getGraph());
}

}  // namespace

int main()
{
  try {
    const silicon::yosys::ToolOptions options{
#ifdef SILICON_YOSYS_EXECUTABLE
        .executable = std::filesystem::path(SILICON_YOSYS_EXECUTABLE),
#else
        .executable = std::nullopt,
#endif
        .technologyLibraryDirectory = std::nullopt,
    };

    const Circuit     counter = makeCounterCircuit();
    const std::string verilog = makePlainVerilog(counter, options);

    const auto verilogPath =
        std::filesystem::current_path() / "yosys_roundtrip_counter.v";
    const auto circuitJsonPath =
        std::filesystem::current_path() / "yosys_roundtrip_counter.json";
    writeFile(verilogPath, verilog);

    const std::string importedSource = readFile(verilogPath);
    const Circuit     restored =
        silicon::yosys::importVerilog(importedSource, TopModule, options);
    writeFile(circuitJsonPath, restored.serialize());

    std::cout << "Exported SILICON counter to " << verilogPath << '\n';
    std::cout << "Exported round-tripped SILICON circuit JSON to " << circuitJsonPath
              << '\n';
    std::cout << "Original circuit: " << componentCount(counter) << " components\n";
    std::cout << "Restored circuit: " << componentCount(restored) << " components\n";
    std::cout << "Roundtrip completed for top module '" << restored.getName() << "'\n";
  } catch (const std::exception& error) {
    std::cerr << "yosys_roundtrip failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
