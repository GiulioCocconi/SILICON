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

#pragma once

#include "circuitTestHelpers.hpp"
#include "subcircuitFixtures.hpp"
#include "tests.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <core/circuit.hpp>
#include <core/flipflops.hpp>
#include <core/io.hpp>
#include <core/memory.hpp>
#include <core/projectCircuitResolver.hpp>
#include <core/projectContext.hpp>
#include <core/projectDocument.hpp>
#include <core/register.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/document_conversion.hpp>
#include <core/serialization/verilog.hpp>
#include <core/serialization/yosys/cells.hpp>
#include <core/serialization/yosys/netlist.hpp>
#include <core/serialization/yosys/yosys_tool.hpp>
#include <core/simulator.hpp>
#include <core/subcircuit.hpp>
#include <extraComponents/arithmetic.hpp>
#include <extraComponents/multiplexer.hpp>
#include <extraComponents/utils.hpp>
#include <logging/logger.hpp>
#include <nlohmann/json.hpp>

using namespace SILICON::core;
using namespace SILICON::extra;
using namespace SILICON::logging;
using namespace SILICON::simulation;
using namespace SILICON::waveform;

namespace {

class YosysLogCapture {
public:
  YosysLogCapture()
    : handle(Logger::addCallbackSink([this](const LogMessage& message) {
        if (message.category == "yosys")
          messages.push_back(message);
      }))
  {
  }

  ~YosysLogCapture() { Logger::removeSink(handle); }

  [[nodiscard]] std::string text() const
  {
    std::string result;
    for (const auto& message : messages)
      result += message.message + '\n';
    return result;
  }

private:
  std::vector<LogMessage> messages;
  Logger::SinkHandle      handle;
};

class DefaultedInputYosysComponent : public Component {
public:
  DefaultedInputYosysComponent() : Component({Bus{Wire_ptr{}}}, {Bus(1)})
  {
    defineUnconnectedInputDefault(0, State::HIGH);
  }

  std::string_view typeName() const override { return "DefaultedInputYosys"; }
  void             simulate(Simulator&) override {}

  void serializeYosys(SILICON::yosys::SerializationContext& context) const override
  {
    using SILICON::yosys::Json;
    using SILICON::yosys::SerializationContext;
    context.addCell("default_input", "$pos",
                    Json{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                         {"A_WIDTH", SerializationContext::parameter(1)},
                         {"Y_WIDTH", SerializationContext::parameter(1)}},
                    Json{{"A", "input"}, {"Y", "output"}},
                    Json{{"A", context.inputBits(*this, 0, 1)},
                         {"Y", context.bits(outputBuses().at(0))}});
  }
};

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
class PathEnvironmentGuard {
public:
  PathEnvironmentGuard()
  {
    if (const char* value = std::getenv("PATH"))
      original = value;
    setenv("PATH", "", 1);
  }

  ~PathEnvironmentGuard()
  {
    if (original)
      setenv("PATH", original->c_str(), 1);
    else
      unsetenv("PATH");
  }

private:
  std::optional<std::string> original;
};
#endif

// Local composition of the primitive Yosys API (the convenience wrappers were
// removed from the library): parse-only read, then elaborate preserving the
// module hierarchy, then deserialize into a Silicon circuit.
[[nodiscard]] inline Circuit importVerilog(const std::string_view             source,
                                           const std::string_view             topModule,
                                           const SILICON::yosys::ToolOptions& options = {})
{
  return SILICON::yosys::deserialize(
      SILICON::yosys::elaborateHierarchy(SILICON::verilog::read(source, options)),
      topModule);
}

// Exercise the expression passes without unrelated synthesis or technology mapping.
[[nodiscard]] inline std::string legalizeExpressions(std::string_view rawJson,
                                                      std::string_view passes = "silicon_logic\nsilicon_compare\nsilicon_shift")
{
#ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  const auto directory = std::filesystem::temp_directory_path()
      / std::format("silicon_expression_{}",
                    std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(directory);
  const auto input = directory / "input.json";
  const auto output = directory / "output.json";
  try {
    { std::ofstream file(input); file << rawJson; }
    (void)SILICON::yosys::runScript(std::format(
        "plugin -i \"{}\"\nread_json \"{}\"\n{}\nwrite_json \"{}\"",
        SILICON_TEST_YOSYS_PLUGIN_PATH, input.string(), passes, output.string()));
    std::ifstream file(output);
    std::string result((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::filesystem::remove_all(directory);
    return result;
  } catch (...) {
    std::filesystem::remove_all(directory);
    throw;
  }
#else
  return std::string(rawJson);
#endif
}

[[nodiscard]] inline nlohmann::json exportComponent(const Component_ptr& component)
{
  Circuit circuit(component, false);
  return nlohmann::json::parse(SILICON::yosys::serialize(circuit, "component_test"));
}

[[nodiscard]] inline nlohmann::json signalBits(int& nextSignal, const std::size_t width)
{
  auto bits = nlohmann::json::array();
  for (std::size_t bit = 0; bit < width; ++bit)
    bits.push_back(nextSignal++);
  return bits;
}

[[nodiscard]] inline nlohmann::json subtractionDesign(const std::size_t aWidth,
                                               const std::size_t bWidth,
                                               const std::size_t yWidth,
                                               const bool aSigned, const bool bSigned)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  int        nextSignal = 2;
  const Json aBits      = signalBits(nextSignal, aWidth);
  const Json bBits      = signalBits(nextSignal, bWidth);
  const Json yBits      = signalBits(nextSignal, yWidth);
  const Json parameters{
      {"A_SIGNED", SerializationContext::parameter(aSigned, 1)},
      {"B_SIGNED", SerializationContext::parameter(bSigned, 1)},
      {"A_WIDTH", SerializationContext::parameter(aWidth)},
      {"B_WIDTH", SerializationContext::parameter(bWidth)},
      {"Y_WIDTH", SerializationContext::parameter(yWidth)},
  };

  return Json{{"creator", "test"},
              {"modules",
               {{"top",
                 {{"attributes", Json::object()},
                  {"ports",
                   {{"a", {{"direction", "input"}, {"bits", aBits}}},
                    {"b", {{"direction", "input"}, {"bits", bBits}}},
                    {"y", {{"direction", "output"}, {"bits", yBits}}}}},
                  {"cells",
                   {{"subtract",
                     {{"type", "$sub"},
                      {"parameters", parameters},
                      {"connections", {{"A", aBits}, {"B", bBits}, {"Y", yBits}}}}}}},
                  {"netnames", Json::object()}}}}}};
}

[[nodiscard]] inline nlohmann::json comparisonDesign(const std::string_view type,
                                              const std::size_t      aWidth,
                                              const std::size_t      bWidth,
                                              const std::size_t      yWidth,
                                              const bool aSigned, const bool bSigned)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  int        nextSignal = 2;
  const Json aBits      = signalBits(nextSignal, aWidth);
  const Json bBits      = signalBits(nextSignal, bWidth);
  const Json yBits      = signalBits(nextSignal, yWidth);

  return Json{{"creator", "test"},
              {"modules",
               {{"top",
                 {{"attributes", Json::object()},
                  {"ports",
                   {{"a", {{"direction", "input"}, {"bits", aBits}}},
                    {"b", {{"direction", "input"}, {"bits", bBits}}},
                    {"y", {{"direction", "output"}, {"bits", yBits}}}}},
                  {"cells",
                   {{"compare",
                     {{"type", type},
                      {"parameters",
                       {{"A_SIGNED", SerializationContext::parameter(aSigned, 1)},
                        {"B_SIGNED", SerializationContext::parameter(bSigned, 1)},
                        {"A_WIDTH", SerializationContext::parameter(aWidth)},
                        {"B_WIDTH", SerializationContext::parameter(bWidth)},
                        {"Y_WIDTH", SerializationContext::parameter(yWidth)}}},
                      {"connections", {{"A", aBits}, {"B", bBits}, {"Y", yBits}}}}}}},
                  {"netnames", Json::object()}}}}}};
}

[[nodiscard]] inline BusValue evaluateBinaryCircuit(const std::shared_ptr<Circuit>& circuit,
                                             const unsigned int a, const unsigned int b)
{
  std::map<std::string, std::shared_ptr<DummyBusInputComponent>> inputs;
  Component_ptr                                                  output;
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(circuit->getGraph()))) {
    const auto& component = circuit->getGraph()[vertex].component;
    if (auto input = std::dynamic_pointer_cast<DummyBusInputComponent>(component))
      inputs.emplace(input->getPropertyValue<std::string>("name").value_or(""), input);
    if (std::dynamic_pointer_cast<DummyOutputComponent>(component)
        || std::dynamic_pointer_cast<DummyBusOutputComponent>(component))
      output = component;
  }

  if (!inputs.contains("a") || !inputs.contains("b") || !output)
    throw std::runtime_error("Expected named binary circuit boundary components");
  inputs.at("a")->setBusValue(valueFor(inputs.at("a")->outputBuses()[0], a));
  inputs.at("b")->setBusValue(valueFor(inputs.at("b")->outputBuses()[0], b));
  Simulator simulator(circuit);
  if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
    throw std::runtime_error("Binary circuit simulation did not complete");
  return output->inputBuses()[0].getCurrentValue();
}

[[nodiscard]] inline std::multiset<std::string> cellTypes(const nlohmann::json& module)
{
  std::multiset<std::string> result;
  for (const auto& cell : module.at("cells"))
    result.insert(cell.at("type").get<std::string>());
  return result;
}

template <typename ComponentType>
[[nodiscard]] inline std::shared_ptr<ComponentType>
findNamedComponent(const Circuit& circuit, const std::string_view name)
{
  for (const auto& component : componentsIn(circuit)) {
    auto candidate = std::dynamic_pointer_cast<ComponentType>(component);
    if (candidate
        && candidate->template getPropertyValue<std::string>("name").value_or("")
               == name) {
      return candidate;
    }
  }
  return nullptr;
}

[[nodiscard]] inline const nlohmann::json& onlyModule(const nlohmann::json& design)
{
  return design.at("modules").begin().value();
}

[[nodiscard]] inline nlohmann::json& onlyModule(nlohmann::json& design)
{
  return design.at("modules").begin().value();
}

[[nodiscard]] inline const nlohmann::json& onlyCell(const nlohmann::json& design)
{
  return onlyModule(design).at("cells").begin().value();
}

template <typename ComponentType>
[[nodiscard]] inline std::shared_ptr<ComponentType> findComponent(const Circuit& circuit)
{
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(circuit.getGraph()))) {
    if (auto component = std::dynamic_pointer_cast<ComponentType>(
            circuit.getGraph()[vertex].component))
      return component;
  }
  return nullptr;
}

[[nodiscard]] inline Circuit circuitWithBoundaryPorts(const Component_ptr& component)
{
  Component_set components{component};
  for (std::size_t index = 0; index < component->inputBuses().size(); ++index) {
    components.insert(std::make_shared<DummyBusInputComponent>(
        component->inputBuses()[index], std::format("input_{}", index)));
  }
  for (std::size_t index = 0; index < component->outputBuses().size(); ++index) {
    components.insert(std::make_shared<DummyBusOutputComponent>(
        component->outputBuses()[index], std::format("output_{}", index)));
  }
  Circuit circuit(components, false);
  return circuit;
}

[[nodiscard]] inline std::shared_ptr<Register>
registerWithMode(const bool parallelInput, const bool parallelOutput, const int width = 4)
{
  auto reg = std::make_shared<Register>(
      Bus(static_cast<unsigned short>(width)), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(),
      Bus(static_cast<unsigned short>(width)));
  reg->setProperty("inputType", std::string(parallelInput ? Register::ParallelType
                                                          : Register::SerialType));
  reg->setProperty("outputType", std::string(parallelOutput ? Register::ParallelType
                                                            : Register::SerialType));
  return reg;
}

#ifdef SILICON_TEST_YOSYS_EXECUTABLE
[[nodiscard]] inline int validateWithYosys(const Circuit& circuit, const std::string_view tag)
{
  const auto path =
      std::filesystem::temp_directory_path() / std::format("silicon_yosys_{}.json", tag);
  {
    std::ofstream output(path);
    if (!output.good())
      return -1;
    output << SILICON::yosys::serialize(circuit, "top");
  }

  int result = 0;
  try {
    const SILICON::yosys::ToolOptions options{
        .executable = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
        .technologyLibraryDirectory = std::nullopt};
    (void)SILICON::yosys::runScript(
        std::format("read_verilog -lib -D SILICON_BLACKBOX \"{}/silicon_cells.v\"\n"
                    "read_json \"{}\"\n"
                    "hierarchy -check -top top\n"
                    "check -assert\n",
                    SILICON_TEST_YOSYS_RESOURCE_DIR, path.string()),
        options);
  } catch (const std::runtime_error&) {
    result = 1;
  }
  std::filesystem::remove(path);
  return result;
}

#ifdef SILICON_TEST_YOSYS_PLUGIN_PATH
inline void runPluginScript(const std::string_view source, const std::string_view commands,
                            const std::string_view tag)
{
  const auto path =
      std::filesystem::temp_directory_path()
      / std::format("silicon_yosys_{}_{}.v", tag,
                    std::chrono::steady_clock::now().time_since_epoch().count());
  {
    std::ofstream output(path);
    if (!output.good())
      throw std::runtime_error("Could not create temporary Verilog source");
    output << source;
  }

  try {
    const SILICON::yosys::ToolOptions options{
        .executable = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
        .technologyLibraryDirectory = std::nullopt};
    (void)SILICON::yosys::runScript(
        std::format("plugin -i \"{}\"\nread_verilog \"{}\"\n{}",
                    SILICON_TEST_YOSYS_PLUGIN_PATH, path.string(), commands),
        options);
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
  std::filesystem::remove(path);
}
#endif
#endif

} // namespace
