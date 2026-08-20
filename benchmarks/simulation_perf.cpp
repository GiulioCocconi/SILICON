/*
  Copyright (C) 2026 Giulio Cocconi

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

#include <core/circuit.hpp>
#include <core/flipflops.hpp>
#include <core/gates.hpp>
#include <core/simulator.hpp>
#include <logging/logger.hpp>
#include <utils/num_formatting.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace SILICON::core;
using namespace SILICON::simulation;
using namespace SILICON::waveform;
using namespace SILICON::logging;

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::string scenario   = "all";
  std::size_t width      = 128;
  std::size_t depth      = 64;
  std::size_t iterations = 100;
  bool        selfDesc   = false;
};

struct CircuitBench {
  std::shared_ptr<Circuit> circuit;
  std::vector<Bus>         inputs;
  std::vector<Bus>         outputs;
  std::size_t              components = 0;
  std::size_t              wires      = 0;
};

struct Measurement {
  CircuitBench bench;
  double       buildMs = 0.0;
};

double elapsedMs(const Clock::time_point start, const Clock::time_point end)
{
  return std::chrono::duration<double, std::milli>(end - start).count();
}

void setZeroDelay(const Component_ptr& component)
{
  component->setProperty("delay", 0);
}

Wire_ptr makeWire(const State state = State::LOW)
{
  return std::make_shared<Wire>(state);
}

void addGate(Component_set& components, const Component_ptr& gate,
             const bool zeroDelay = true)
{
  if (zeroDelay)
    setZeroDelay(gate);
  components.insert(gate);
}

Measurement makeLayeredCircuit(const std::size_t width, const std::size_t depth)
{
  const auto start = Clock::now();

  Component_set         components;
  std::vector<Wire_ptr> current;
  std::vector<Bus>      inputs;
  current.reserve(width);
  inputs.reserve(width);

  for (std::size_t i = 0; i < width; ++i) {
    auto wire = makeWire(i % 2 == 0 ? State::LOW : State::HIGH);
    inputs.push_back(Bus{wire});
    current.push_back(std::move(wire));
  }

  std::size_t wireCount = current.size();
  for (std::size_t layer = 0; layer < depth; ++layer) {
    std::vector<Wire_ptr> next;
    next.reserve(width);

    for (std::size_t bit = 0; bit < width; ++bit) {
      auto       output = makeWire(State::UNKNOWN);
      const auto left   = current[bit];
      const auto right  = current[(bit + layer + 1) % width];

      switch ((layer + bit) % 3) {
        case 0:
          addGate(components,
                  std::make_shared<AndGate>(std::vector<Wire_ptr>{left, right}, output));
          break;
        case 1:
          addGate(components,
                  std::make_shared<OrGate>(std::vector<Wire_ptr>{left, right}, output));
          break;
        default:
          addGate(components, std::make_shared<XorGate>(
                                  std::array<Wire_ptr, 2>{left, right}, output));
          break;
      }

      next.push_back(std::move(output));
    }

    wireCount += next.size();
    current = std::move(next);
  }

  std::vector<Bus> outputs;
  outputs.reserve(current.size());
  for (const auto& wire : current)
    outputs.push_back(Bus{wire});

  CircuitBench bench;
  bench.circuit    = std::make_shared<Circuit>(components, false);
  bench.inputs     = std::move(inputs);
  bench.outputs    = std::move(outputs);
  bench.components = components.size();
  bench.wires      = wireCount;

  return {std::move(bench), elapsedMs(start, Clock::now())};
}

Measurement makeFanoutCircuit(const std::size_t width, const std::size_t depth)
{
  const auto start = Clock::now();

  Component_set    components;
  const auto       root = makeWire(State::LOW);
  std::vector<Bus> inputs{Bus{root}};
  std::vector<Bus> outputs;
  std::size_t      wireCount = 1;

  outputs.reserve(width);
  for (std::size_t branch = 0; branch < width; ++branch) {
    auto current = root;
    auto salt    = makeWire(branch % 2 == 0 ? State::LOW : State::HIGH);
    ++wireCount;

    for (std::size_t stage = 0; stage < depth; ++stage) {
      auto output = makeWire(State::UNKNOWN);
      if ((stage + branch) % 2 == 0) {
        addGate(components,
                std::make_shared<AndGate>(std::vector<Wire_ptr>{current, salt}, output));
      } else {
        addGate(components,
                std::make_shared<OrGate>(std::vector<Wire_ptr>{current, salt}, output));
      }
      current = std::move(output);
      ++wireCount;
    }

    outputs.push_back(Bus{current});
  }

  CircuitBench bench;
  bench.circuit    = std::make_shared<Circuit>(components, false);
  bench.inputs     = std::move(inputs);
  bench.outputs    = std::move(outputs);
  bench.components = components.size();
  bench.wires      = wireCount;

  return {std::move(bench), elapsedMs(start, Clock::now())};
}

Measurement makeDelayedChainsCircuit(const std::size_t width, const std::size_t depth)
{
  const auto start = Clock::now();

  Component_set    components;
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;
  std::size_t      wireCount = 0;

  inputs.reserve(width);
  outputs.reserve(width);
  for (std::size_t chain = 0; chain < width; ++chain) {
    auto current = makeWire(chain % 2 == 0 ? State::LOW : State::HIGH);
    inputs.push_back(Bus{current});
    ++wireCount;

    for (std::size_t stage = 0; stage < depth; ++stage) {
      auto output = makeWire(State::UNKNOWN);
      auto gate   = std::make_shared<NotGate>(current, output);
      gate->setProperty("delay", 1);
      components.insert(gate);
      current = std::move(output);
      ++wireCount;
    }

    outputs.push_back(Bus{current});
  }

  CircuitBench bench;
  bench.circuit    = std::make_shared<Circuit>(components, false);
  bench.inputs     = std::move(inputs);
  bench.outputs    = std::move(outputs);
  bench.components = components.size();
  bench.wires      = wireCount;

  return {std::move(bench), elapsedMs(start, Clock::now())};
}

Measurement makeCyclicCombinationalCircuit(const std::size_t width,
                                           const std::size_t depth)
{
  const auto start = Clock::now();

  Component_set    components;
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;
  std::size_t      wireCount = 0;

  inputs.reserve(width);
  outputs.reserve(width);
  for (std::size_t ring = 0; ring < width; ++ring) {
    const auto forcingInput = makeWire(ring % 2 == 0 ? State::HIGH : State::LOW);
    inputs.push_back(Bus{forcingInput});
    ++wireCount;

    std::vector<Wire_ptr> ringWires;
    ringWires.reserve(depth);
    for (std::size_t stage = 0; stage < depth; ++stage) {
      ringWires.push_back(makeWire(State::UNKNOWN));
      ++wireCount;
    }

    for (std::size_t stage = 0; stage < depth; ++stage) {
      const auto input  = ringWires[stage];
      const auto output = ringWires[(stage + 1) % depth];

      if ((stage + ring) % 2 == 0) {
        addGate(components, std::make_shared<OrGate>(
                                std::vector<Wire_ptr>{input, forcingInput}, output));
      } else {
        addGate(components, std::make_shared<AndGate>(
                                std::vector<Wire_ptr>{input, forcingInput}, output));
      }
    }

    outputs.push_back(Bus{ringWires.front()});
  }

  CircuitBench bench;
  bench.circuit    = std::make_shared<Circuit>(components, false);
  bench.inputs     = std::move(inputs);
  bench.outputs    = std::move(outputs);
  bench.components = components.size();
  bench.wires      = wireCount;

  return {std::move(bench), elapsedMs(start, Clock::now())};
}

Measurement makeRegisteredCyclicCircuit(const std::size_t width, const std::size_t depth)
{
  const auto start = Clock::now();

  Component_set components;
  const auto    clock  = makeWire(State::LOW);
  const auto    clear  = makeWire(State::LOW);
  const auto    preset = makeWire(State::LOW);

  std::vector<Bus>      inputs{Bus{clock}};
  std::vector<Bus>      outputs;
  std::vector<Wire_ptr> qWires;
  std::vector<Wire_ptr> dWires;
  std::size_t           wireCount = 3;

  qWires.reserve(width);
  dWires.reserve(width);
  outputs.reserve(width);
  for (std::size_t bit = 0; bit < width; ++bit) {
    qWires.push_back(makeWire(bit % 2 == 0 ? State::LOW : State::HIGH));
    dWires.push_back(makeWire(State::UNKNOWN));
    outputs.push_back(Bus{qWires.back()});
    wireCount += 2;
  }

  for (std::size_t bit = 0; bit < width; ++bit) {
    auto current = qWires[(bit + width - 1) % width];
    for (std::size_t stage = 0; stage < depth; ++stage) {
      const auto output = stage + 1 == depth ? dWires[bit] : makeWire(State::UNKNOWN);
      addGate(components, std::make_shared<NotGate>(current, output));
      current = output;
      if (stage + 1 != depth)
        ++wireCount;
    }

    auto notQ = makeWire(State::UNKNOWN);
    auto ff =
        std::make_shared<DFlipFlop>(dWires[bit], clock, clear, preset, qWires[bit], notQ);
    ff->setProperty("propagationDelay", 0);
    components.insert(ff);
    ++wireCount;
  }

  CircuitBench bench;
  bench.circuit    = std::make_shared<Circuit>(components, false);
  bench.inputs     = std::move(inputs);
  bench.outputs    = std::move(outputs);
  bench.components = components.size();
  bench.wires      = wireCount;

  return {std::move(bench), elapsedMs(start, Clock::now())};
}

std::size_t checksum(const std::vector<Bus>& buses)
{
  std::size_t result = 0;
  for (const auto& bus : buses) {
    for (const auto state : bus.getCurrentValue())
      result = result * 131U + static_cast<std::size_t>(std::to_underlying(state));
    result = result * 17U + static_cast<std::size_t>(bus.hasUnknowns());
    result = result * 17U + static_cast<std::size_t>(bus.isInErrorState());
  }
  return result;
}

template <typename Value>
void printMetric(const std::string_view name, const Value& value,
                 const std::string_view description, const bool selfDesc)
{
  std::cout << "  " << name << ": " << value;
  if (selfDesc)
    std::cout << "  # " << description;
  std::cout << '\n';
}

void printScenarioHeader(const std::string_view name, const std::string_view description,
                         const Measurement& measurement, const bool selfDesc)
{
  std::cout << name << '\n';
  if (selfDesc)
    printMetric("description", description, "scenario topology and measured behavior",
                selfDesc);
  printMetric("components", measurement.bench.components,
              "number of simulated components in this generated circuit", selfDesc);
  printMetric("wires", measurement.bench.wires,
              "number of wires allocated by this generated circuit", selfDesc);
  printMetric("build_ms", measurement.buildMs,
              "milliseconds spent constructing the generated circuit", selfDesc);
}

void printUsage(const char* executable)
{
  std::cout
      << "Usage: " << executable
      << " [--scenario all|layered|fanout|delayed|cyclic|registered-cyclic]"
         " [--width N]"
         " [--depth N] [--iterations N] [--selfdesc]\n\n"
      << "Runs self-contained simulator performance testbenches.\n\n"
      << "Scenarios:\n"
      << "  layered             wide acyclic gate layers; measures forward "
         "propagation\n"
      << "  fanout              one input feeding many gate chains; measures fanout "
         "propagation\n"
      << "  delayed             NOT chains with unit gate delays; measures event "
         "queue draining\n"
      << "  cyclic              forced combinational rings; measures cyclic "
         "settling\n"
      << "  registered-cyclic   flip-flop feedback rings; measures clocked "
         "feedback\n\n"
      << "Options:\n"
      << "  --width N       number of parallel bits, branches, chains, or rings\n"
      << "  --depth N       number of gate stages per topology\n"
      << "  --iterations N  number of input toggles or clock cycles to measure\n"
      << "  --selfdesc      include metric descriptions in benchmark output\n";
}

std::size_t parseSize(const char* value, const std::string_view option)
{
  try {
    const auto parsed = std::stoull(value);
    if (parsed == 0)
      throw std::invalid_argument("zero");
    return parsed;
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(option) + " expects a positive integer");
  }
}

Options parseOptions(const int argc, char** argv)
{
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg          = argv[i];
    auto                   requireValue = [&](const std::string_view option) -> char* {
      if (i + 1 >= argc)
        throw std::invalid_argument(std::string(option) + " requires a value");
      return argv[++i];
    };

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else if (arg == "--scenario") {
      options.scenario = requireValue(arg);
    } else if (arg == "--width") {
      options.width = parseSize(requireValue(arg), arg);
    } else if (arg == "--depth") {
      options.depth = parseSize(requireValue(arg), arg);
    } else if (arg == "--iterations") {
      options.iterations = parseSize(requireValue(arg), arg);
    } else if (arg == "--selfdesc") {
      options.selfDesc = true;
    } else {
      throw std::invalid_argument("unknown option: " + std::string(arg));
    }
  }

  return options;
}

void runPropagationBenchmark(const std::string_view name, Measurement measurement,
                             const std::string_view description,
                             const std::size_t iterations, const bool selfDesc)
{
  const auto simulatorStart = Clock::now();
  Simulator  simulator(measurement.bench.circuit);
  const auto simulatorMs = elapsedMs(simulatorStart, Clock::now());

  const auto runStart = Clock::now();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const auto value = static_cast<unsigned int>(iteration & 1U);
    for (const auto& input : measurement.bench.inputs) {
      if (simulator.setBus(input, busValueFromInteger(value, input.size()))
          != Simulator::RunResult::Completed)
        throw std::runtime_error("setBus did not complete");
    }
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("runUntilIdle did not complete");
  }
  const auto runMs = elapsedMs(runStart, Clock::now());

  printScenarioHeader(name, description, measurement, selfDesc);
  printMetric("simulator_construct_ms", simulatorMs,
              "milliseconds spent constructing the Simulator from the circuit", selfDesc);
  printMetric("iterations", iterations,
              "number of input toggle and settle rounds measured", selfDesc);
  printMetric("propagation_ms", runMs,
              "total milliseconds spent applying inputs and settling propagation",
              selfDesc);
  printMetric("propagation_ms_per_iteration", runMs / iterations,
              "average propagation milliseconds per input toggle and settle round",
              selfDesc);
  printMetric("checksum", checksum(measurement.bench.outputs),
              "hash of final output states used to keep the benchmark work observable",
              selfDesc);
  std::cout << '\n';
}

void runDelayedBenchmark(Measurement measurement, const bool selfDesc)
{
  const auto previousMaxSteps = Simulator::getMaxSimulationSteps();
  Simulator::setMaxSimulationSteps(
      std::max<uint64_t>(previousMaxSteps, measurement.bench.components * 4U));

  const auto simulatorStart = Clock::now();
  Simulator  simulator(measurement.bench.circuit);
  const auto simulatorMs = elapsedMs(simulatorStart, Clock::now());

  const auto runStart = Clock::now();
  if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
    throw std::runtime_error("delayed benchmark did not reach idle");
  const auto runMs = elapsedMs(runStart, Clock::now());

  Simulator::setMaxSimulationSteps(previousMaxSteps);

  printScenarioHeader("delayed",
                      "parallel NOT chains with unit gate delays; drains pending "
                      "events until idle",
                      measurement, selfDesc);
  printMetric("simulator_construct_ms", simulatorMs,
              "milliseconds spent constructing the Simulator from the circuit", selfDesc);
  printMetric("event_queue_drain_ms", runMs,
              "milliseconds spent processing delayed events until the queue reached idle",
              selfDesc);
  printMetric("final_time", simulator.getCurrentTime(),
              "simulated timestamp reached after all delayed events settled", selfDesc);
  printMetric("checksum", checksum(measurement.bench.outputs),
              "hash of final output states used to keep the benchmark work observable",
              selfDesc);
  std::cout << '\n';
}

void runCyclicBenchmark(Measurement measurement, const std::size_t iterations,
                        const bool selfDesc)
{
  const auto previousMaxTransitions = Simulator::getMaxTransitionsPerDeltaCycle();
  Simulator::setMaxTransitionsPerDeltaCycle(static_cast<int>(
      std::max<std::size_t>(previousMaxTransitions, measurement.bench.components * 4U)));

  const auto simulatorStart = Clock::now();
  Simulator  simulator(measurement.bench.circuit);
  const auto simulatorMs = elapsedMs(simulatorStart, Clock::now());

  const auto runStart = Clock::now();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const auto value = static_cast<unsigned int>(iteration & 1U);
    for (const auto& input : measurement.bench.inputs) {
      if (simulator.setBus(input, busValueFromInteger(value, input.size()))
          != Simulator::RunResult::Completed)
        throw std::runtime_error("cyclic setBus did not complete");
    }
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("cyclic benchmark did not reach idle");
  }
  const auto runMs = elapsedMs(runStart, Clock::now());

  Simulator::setMaxTransitionsPerDeltaCycle(previousMaxTransitions);

  printScenarioHeader("cyclic",
                      "combinational rings forced by external inputs; measures "
                      "delta-cycle settling",
                      measurement, selfDesc);
  printMetric("simulator_construct_ms", simulatorMs,
              "milliseconds spent constructing the Simulator from the circuit", selfDesc);
  printMetric("iterations", iterations,
              "number of cyclic input toggle and settle rounds measured", selfDesc);
  printMetric("cyclic_settle_ms", runMs,
              "total milliseconds spent toggling cyclic inputs and resolving "
              "delta-cycle feedback",
              selfDesc);
  printMetric("cyclic_settle_ms_per_iteration", runMs / iterations,
              "average cyclic settling milliseconds per input toggle round", selfDesc);
  printMetric("checksum", checksum(measurement.bench.outputs),
              "hash of final output states used to keep the benchmark work observable",
              selfDesc);
  std::cout << '\n';
}

void runRegisteredCyclicBenchmark(Measurement measurement, const std::size_t iterations,
                                  const bool selfDesc)
{
  const auto simulatorStart = Clock::now();
  Simulator  simulator(measurement.bench.circuit);
  const auto simulatorMs = elapsedMs(simulatorStart, Clock::now());

  const auto runStart = Clock::now();
  const auto clock    = measurement.bench.inputs.front();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    if (simulator.setBus(clock, busValueFromInteger(1, clock.size()))
        != Simulator::RunResult::Completed)
      throw std::runtime_error("registered cyclic rising clock did not complete");
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("registered cyclic rising settle did not complete");
    if (simulator.setBus(clock, busValueFromInteger(0, clock.size()))
        != Simulator::RunResult::Completed)
      throw std::runtime_error("registered cyclic falling clock did not complete");
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("registered cyclic falling settle did not complete");
  }
  const auto runMs = elapsedMs(runStart, Clock::now());

  printScenarioHeader("registered-cyclic",
                      "D flip-flop feedback rings separated by combinational NOT "
                      "chains; measures clocked feedback",
                      measurement, selfDesc);
  printMetric("simulator_construct_ms", simulatorMs,
              "milliseconds spent constructing the Simulator from the circuit", selfDesc);
  printMetric("clock_cycles", iterations,
              "number of full rising-plus-falling clock cycles measured", selfDesc);
  printMetric("clocked_ms", runMs,
              "total milliseconds spent clocking and settling registered feedback",
              selfDesc);
  printMetric("clocked_ms_per_cycle", runMs / iterations,
              "average milliseconds per full clock cycle", selfDesc);
  printMetric("checksum", checksum(measurement.bench.outputs),
              "hash of final output states used to keep the benchmark work observable",
              selfDesc);
  std::cout << '\n';
}

bool shouldRun(const std::string& selected, const std::string_view scenario)
{
  return selected == "all" || selected == scenario;
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    Logger::setMinimumLevel(LogLevel::Warning);

    const auto options = parseOptions(argc, argv);
    if (!shouldRun(options.scenario, "layered") && !shouldRun(options.scenario, "fanout")
        && !shouldRun(options.scenario, "delayed")
        && !shouldRun(options.scenario, "cyclic")
        && !shouldRun(options.scenario, "registered-cyclic")) {
      throw std::invalid_argument(
          "scenario must be all, layered, fanout, delayed, cyclic, or "
          "registered-cyclic");
    }

    std::cout << "simulation_perf\n";
    if (options.selfDesc)
      printMetric("description", "self-contained simulator performance testbench",
                  "what this executable runs", options.selfDesc);
    printMetric("scenario", options.scenario,
                "selected topology group; all runs every scenario", options.selfDesc);
    printMetric("width", options.width,
                "parallel bits, branches, chains, or rings generated per scenario",
                options.selfDesc);
    printMetric("depth", options.depth, "gate stages generated per topology path",
                options.selfDesc);
    printMetric("iterations", options.iterations,
                "input toggles or clock cycles requested for measured scenarios",
                options.selfDesc);
    std::cout << '\n';

    if (shouldRun(options.scenario, "layered")) {
      runPropagationBenchmark("layered", makeLayeredCircuit(options.width, options.depth),
                              "wide acyclic layers of AND/OR/XOR gates; measures "
                              "forward propagation",
                              options.iterations, options.selfDesc);
    }

    if (shouldRun(options.scenario, "fanout")) {
      runPropagationBenchmark("fanout", makeFanoutCircuit(options.width, options.depth),
                              "one root input feeding many AND/OR gate chains; "
                              "measures fanout propagation",
                              options.iterations, options.selfDesc);
    }

    if (shouldRun(options.scenario, "delayed")) {
      runDelayedBenchmark(makeDelayedChainsCircuit(options.width, options.depth),
                          options.selfDesc);
    }

    if (shouldRun(options.scenario, "cyclic")) {
      runCyclicBenchmark(makeCyclicCombinationalCircuit(options.width, options.depth),
                         options.iterations, options.selfDesc);
    }

    if (shouldRun(options.scenario, "registered-cyclic")) {
      runRegisteredCyclicBenchmark(
          makeRegisteredCyclicCircuit(options.width, options.depth), options.iterations,
          options.selfDesc);
    }
  } catch (const std::exception& e) {
    std::cerr << "simulation_perf: " << e.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
