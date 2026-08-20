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

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <core/circuit.hpp>
#include <core/io.hpp>
#include <core/register.hpp>
#include <core/serialization/yosys.hpp>
#include <core/wire.hpp>
#include <extraComponents/arithmetic.hpp>

using namespace SILICON::core;
using namespace SILICON::extra;

namespace {

constexpr auto CounterWidth = 4;
constexpr auto TopModule    = "counter";

[[nodiscard]] Component_ptr constant(Wire_ptr output, const State value)
{
  return std::make_shared<ConstantComponent>(std::move(output), BusValue{value});
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

      constant(increment[0], State::HIGH),
      constant(increment[1], State::LOW),
      constant(increment[2], State::LOW),
      constant(increment[3], State::LOW),
      constant(enable, State::HIGH),
      constant(clear, State::LOW),

      std::make_shared<AdderNBits>(std::array<Bus, 2>{count, increment}, nextCount,
                                   carry),
      std::make_shared<Register>(nextCount, clock, enable, clear, count),
  };

  Circuit circuit(components, false);
  circuit.setName(TopModule);
  circuit.setDescription("4-bit counter: next_count = count + 1, sampled by clk.");
  return circuit;
}

[[nodiscard]] std::size_t componentCount(const Circuit& circuit)
{
  return boost::num_vertices(circuit.getGraph());
}

}  // namespace

int main()
{
  try {
    const SILICON::yosys::ToolOptions options{
#ifdef SILICON_YOSYS_EXECUTABLE
        .executable = std::filesystem::path(SILICON_YOSYS_EXECUTABLE),
#else
        .executable = std::nullopt,
#endif
        .technologyLibraryDirectory = std::nullopt,
    };

    const Circuit     counter = makeCounterCircuit();
    const std::string verilog = SILICON::yosys::exportVerilog(counter, options);

    const auto verilogPath =
        std::filesystem::current_path() / "yosys_roundtrip_counter.v";
    const auto circuitJsonPath =
        std::filesystem::current_path() / "yosys_roundtrip_counter.json";
    writeFile(verilogPath, verilog);

    const std::string importedSource = readFile(verilogPath);
    const Circuit     restored =
        SILICON::yosys::importVerilog(importedSource, TopModule, options);
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
