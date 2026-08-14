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
#include "circuitTestHelpers.hpp"
#include "subcircuitFixtures.hpp"

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
#include <vector>

#include <core/circuit.hpp>
#include <core/flipflops.hpp>
#include <core/io.hpp>
#include <core/register.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/yosys.hpp>
#include <core/simulator.hpp>
#include <core/subcircuit.hpp>
#include <core/projectDocument.hpp>
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

[[nodiscard]] nlohmann::json exportComponent(const Component_ptr& component)
{
  Circuit circuit(component, false);
  circuit.setName("component_test");
  return nlohmann::json::parse(circuit.getYosysJson());
}

[[nodiscard]] nlohmann::json signalBits(int& nextSignal, const std::size_t width)
{
  auto bits = nlohmann::json::array();
  for (std::size_t bit = 0; bit < width; ++bit)
    bits.push_back(nextSignal++);
  return bits;
}

[[nodiscard]] nlohmann::json subtractionDesign(const std::size_t aWidth,
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

[[nodiscard]] unsigned int evaluateBinaryCircuit(const std::shared_ptr<Circuit>& circuit,
                                                 const unsigned int              a,
                                                 const unsigned int              b)
{
  std::map<std::string, std::shared_ptr<DummyBusInputComponent>> inputs;
  std::shared_ptr<DummyBusOutputComponent>                       output;
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(circuit->getGraph()))) {
    const auto& component = circuit->getGraph()[vertex].component;
    if (auto input = std::dynamic_pointer_cast<DummyBusInputComponent>(component))
      inputs.emplace(input->getPropertyValue<std::string>("name").value_or(""), input);
    if (auto candidate = std::dynamic_pointer_cast<DummyBusOutputComponent>(component))
      output = std::move(candidate);
  }

  if (!inputs.contains("a") || !inputs.contains("b") || !output)
    throw std::runtime_error("Expected named binary circuit boundary components");
  inputs.at("a")->setState(a);
  inputs.at("b")->setState(b);
  Simulator simulator(circuit);
  if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
    throw std::runtime_error("Binary circuit simulation did not complete");
  return output->inputBuses()[0].getCurrentValue();
}

[[nodiscard]] std::multiset<std::string> cellTypes(const nlohmann::json& module)
{
  std::multiset<std::string> result;
  for (const auto& cell : module.at("cells"))
    result.insert(cell.at("type").get<std::string>());
  return result;
}

[[nodiscard]] const nlohmann::json& onlyModule(const nlohmann::json& design)
{
  return design.at("modules").begin().value();
}

[[nodiscard]] nlohmann::json& onlyModule(nlohmann::json& design)
{
  return design.at("modules").begin().value();
}

[[nodiscard]] const nlohmann::json& onlyCell(const nlohmann::json& design)
{
  return onlyModule(design).at("cells").begin().value();
}

template <typename ComponentType>
[[nodiscard]] std::shared_ptr<ComponentType> findComponent(const Circuit& circuit)
{
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(circuit.getGraph()))) {
    if (auto component = std::dynamic_pointer_cast<ComponentType>(
            circuit.getGraph()[vertex].component))
      return component;
  }
  return nullptr;
}

[[nodiscard]] Circuit circuitWithBoundaryPorts(const Component_ptr& component)
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
  circuit.setName("top");
  return circuit;
}

[[nodiscard]] std::shared_ptr<Register>
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
[[nodiscard]] int validateWithYosys(const Circuit& circuit, const std::string_view tag)
{
  const auto path =
      std::filesystem::temp_directory_path() / std::format("silicon_yosys_{}.json", tag);
  {
    std::ofstream output(path);
    if (!output.good())
      return -1;
    output << circuit.getYosysJson();
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
#endif

}  // namespace

TEST(YosysTest, ExportsNamedPortsAndNativeGate)
{
  auto          a = std::make_shared<Wire>();
  auto          b = std::make_shared<Wire>();
  auto          y = std::make_shared<Wire>();
  Component_set components{
      std::make_shared<DummyInputComponent>(Bus{a}, "a"),
      std::make_shared<DummyInputComponent>(Bus{b}, "b"),
      std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, y),
      std::make_shared<DummyOutputComponent>(Bus{y}, "y"),
  };
  Circuit circuit(components, false);
  circuit.setName("top");

  const auto  json   = nlohmann::json::parse(circuit.getYosysJson());
  const auto& module = json.at("modules").at("top");
  EXPECT_EQ(module.at("ports").at("a").at("direction"), "input");
  EXPECT_EQ(module.at("ports").at("b").at("direction"), "input");
  EXPECT_EQ(module.at("ports").at("y").at("direction"), "output");
  EXPECT_EQ(cellTypes(module), std::multiset<std::string>{"$and"});
}

TEST(YosysTest, MakesDuplicateBoundaryNamesUnique)
{
  auto    a = std::make_shared<Wire>();
  auto    b = std::make_shared<Wire>();
  Circuit circuit(Component_set{std::make_shared<DummyInputComponent>(Bus{a}, "signal"),
                                std::make_shared<DummyOutputComponent>(Bus{b}, "signal")},
                  false);
  const auto  json  = nlohmann::json::parse(circuit.getYosysJson());
  const auto& ports = onlyModule(json).at("ports");
  EXPECT_TRUE(ports.contains("signal"));
  EXPECT_TRUE(ports.contains("signal_2"));
}

TEST(YosysTest, LowersCombinationalComponents)
{
  auto a     = std::make_shared<Wire>();
  auto b     = std::make_shared<Wire>();
  auto c     = std::make_shared<Wire>();
  auto y     = std::make_shared<Wire>();
  auto carry = std::make_shared<Wire>();

  EXPECT_EQ(cellTypes(onlyModule(exportComponent(
                std::make_shared<NandGate>(std::vector<Wire_ptr>{a, b, c}, y)))),
            (std::multiset<std::string>{"$and", "$and", "$not"}));
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(std::make_shared<FullAdder>(
                std::array<Wire_ptr, 2>{a, b}, c, y, carry)))),
            std::multiset<std::string>{"SILICON_FULL_ADDER"});

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4),
                                            std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(adder))),
            std::multiset<std::string>{"SILICON_ADDER"});

  auto extender = std::make_shared<Extender>(Bus(3), Bus(5));
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(extender))),
            std::multiset<std::string>{"$pos"});

  auto mux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(mux))),
            std::multiset<std::string>{"$bmux"});

  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4));
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(demux))),
            std::multiset<std::string>{"$demux"});

  auto decoder = std::make_shared<Decoder>(Bus(1), Bus(2), Bus(4));
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(decoder))),
            std::multiset<std::string>{"$demux"});

  auto splitter =
      std::make_shared<WireSplitter>(Bus(2), std::vector<Bus>{Bus(1), Bus(1)});
  auto merger = std::make_shared<WireMerger>(std::vector<Bus>{Bus(1), Bus(1)}, Bus(2));
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(splitter))),
            std::multiset<std::string>{"$pos"});
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(merger))),
            std::multiset<std::string>{"$pos"});
}

TEST(YosysTest, ExtenderLowersToPosAndRoundTripsWithItsModeAndWidths)
{
  auto extender = std::make_shared<Extender>(Bus(3), Bus(6),
                                              std::string(Extender::SignedMode));
  auto exported = exportComponent(extender);
  EXPECT_EQ(cellTypes(onlyModule(exported)), std::multiset<std::string>{"$pos"});

  const auto& cell = onlyCell(exported);
  EXPECT_EQ(cell.at("parameters").at("A_SIGNED"),
            SILICON::yosys::SerializationContext::parameter(1, 1));
  EXPECT_EQ(cell.at("parameters").at("A_WIDTH"),
            SILICON::yosys::SerializationContext::parameter(3));
  EXPECT_EQ(cell.at("parameters").at("Y_WIDTH"),
            SILICON::yosys::SerializationContext::parameter(6));

  const Circuit imported = SILICON::yosys::deserialize(exported.dump());
  const auto    restored = findComponent<Extender>(imported);
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->getPropertyValue<int>("inSize"), 3);
  EXPECT_EQ(restored->getPropertyValue<int>("outSize"), 6);
  EXPECT_EQ(restored->getPropertyValue<std::string>("mode"),
            std::string(Extender::SignedMode));
  EXPECT_EQ(cellTypes(onlyModule(nlohmann::json::parse(imported.getYosysJson()))),
            std::multiset<std::string>{"$pos"});
}

TEST(YosysTest, ComplementerLowersToSubAndRoundTripsWithoutAnAdder)
{
  auto complementer = std::make_shared<Complementer>(Bus(5), Bus(5));
  auto exported     = exportComponent(complementer);
  EXPECT_EQ(cellTypes(onlyModule(exported)), std::multiset<std::string>{"$sub"});

  const auto& cell = onlyCell(exported);
  EXPECT_EQ(cell.at("parameters").at("A_WIDTH"),
            SILICON::yosys::SerializationContext::parameter(5));
  EXPECT_EQ(cell.at("parameters").at("B_WIDTH"),
            SILICON::yosys::SerializationContext::parameter(5));
  EXPECT_EQ(cell.at("parameters").at("Y_WIDTH"),
            SILICON::yosys::SerializationContext::parameter(5));
  EXPECT_TRUE(std::ranges::all_of(cell.at("connections").at("A"),
                                  [](const auto& bit) { return bit == "0"; }));

  const Circuit imported = SILICON::yosys::deserialize(exported.dump());
  const auto    restored = findComponent<Complementer>(imported);
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->getPropertyValue<int>("size"), 5);
  EXPECT_FALSE(findComponent<AdderNBits>(imported));
  EXPECT_EQ(cellTypes(onlyModule(nlohmann::json::parse(imported.getYosysJson()))),
            std::multiset<std::string>{"$sub"});
}

TEST(YosysTest, LowersSequentialComponents)
{
  auto latch =
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(latch))),
            std::multiset<std::string>{"SILICON_DLATCH"});

  auto dff = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), nullptr, nullptr,
      std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(dff))),
            std::multiset<std::string>{"SILICON_DFF"});

  auto effe = std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      nullptr, nullptr, std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(effe))),
            std::multiset<std::string>{"SILICON_DFFE"});

  auto jk = std::make_shared<JKFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      nullptr, nullptr, std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(cellTypes(onlyModule(exportComponent(jk))),
            std::multiset<std::string>{"SILICON_JKFF"});

  EXPECT_EQ(cellTypes(onlyModule(exportComponent(registerWithMode(true, false)))),
            std::multiset<std::string>{"SILICON_PISO"});
}

TEST(YosysTest, CustomTechnologyCellsRoundTripToNativeComponents)
{
  const auto roundTrip = [](const Component_ptr& component) {
    return SILICON::yosys::deserialize(exportComponent(component).dump());
  };

  auto latch =
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>());
  const auto  latchDesign = exportComponent(latch);
  const auto& latchCell   = onlyCell(latchDesign);
  EXPECT_EQ(latchCell.at("type"), "SILICON_DLATCH");
  EXPECT_EQ(latchCell.at("parameters").at("EN_POLARITY"), "1");
  EXPECT_TRUE(findComponent<DLatch>(roundTrip(latch)));

  auto dff = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), nullptr, nullptr,
      std::make_shared<Wire>(), std::make_shared<Wire>());
  dff->setProperty("triggerEdge", std::string("NET"));
  const auto  dffDesign = exportComponent(dff);
  const auto& dffCell   = onlyCell(dffDesign);
  EXPECT_EQ(dffCell.at("type"), "SILICON_DFF");
  EXPECT_EQ(dffCell.at("connections").size(), 4);
  EXPECT_EQ(dffCell.at("parameters").at("CLK_POLARITY"), "0");
  const auto importedDff = roundTrip(dff);
  ASSERT_TRUE(findComponent<DFlipFlop>(importedDff));
  EXPECT_EQ(
      findComponent<DFlipFlop>(importedDff)->getPropertyValue<std::string>("triggerEdge"),
      std::optional<std::string>("NET"));

  auto dffsr = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(dffsr)).at("type"), "SILICON_DFFSR");
  EXPECT_TRUE(findComponent<DFlipFlop>(roundTrip(dffsr)));

  auto dffe = std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      nullptr, nullptr, std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(dffe)).at("type"), "SILICON_DFFE");
  EXPECT_TRUE(findComponent<EFlipFlop>(roundTrip(dffe)));

  auto dffsre = std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(dffsre)).at("type"), "SILICON_DFFSRE");
  EXPECT_TRUE(findComponent<EFlipFlop>(roundTrip(dffsre)));

  auto jkff = std::make_shared<JKFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      nullptr, nullptr, std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(jkff)).at("type"), "SILICON_JKFF");
  EXPECT_TRUE(findComponent<JKFlipFlop>(roundTrip(jkff)));

  auto halfAdder = std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(halfAdder)).at("type"), "SILICON_HALF_ADDER");
  EXPECT_TRUE(findComponent<HalfAdder>(roundTrip(halfAdder)));

  auto fullAdder = std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>());
  EXPECT_EQ(onlyCell(exportComponent(fullAdder)).at("type"), "SILICON_FULL_ADDER");
  EXPECT_TRUE(findComponent<FullAdder>(roundTrip(fullAdder)));

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(5), Bus(5)}, Bus(5),
                                            std::make_shared<Wire>());
  const auto  adderDesign = exportComponent(adder);
  const auto& adderCell   = onlyCell(adderDesign);
  EXPECT_EQ(adderCell.at("type"), "SILICON_ADDER");
  EXPECT_EQ(adderCell.at("connections").at("A").size(), 5);
  const auto importedAdder = roundTrip(adder);
  ASSERT_TRUE(findComponent<AdderNBits>(importedAdder));
  EXPECT_EQ(findComponent<AdderNBits>(importedAdder)->getPropertyValue<int>("size"), 5);

  struct RegisterMode {
    bool             parallelInput;
    bool             parallelOutput;
    std::string_view cellType;
  };
  static constexpr std::array registerModes{
                                               RegisterMode{true, true, "SILICON_PIPO"},
      RegisterMode{true, false, "SILICON_PISO"},
      RegisterMode{false, true, "SILICON_SIPO"},
      RegisterMode{false, false, "SILICON_SISO"},
  };
                                               for (const auto& mode : registerModes) {
    SCOPED_TRACE(mode.cellType);
    const auto  reg = registerWithMode(mode.parallelInput, mode.parallelOutput);
  const auto  registerDesign = exportComponent(reg);
  const auto& registerCell   = onlyCell(registerDesign);
  EXPECT_EQ(registerCell.at("type"), mode.cellType);
    EXPECT_EQ(registerCell.at("connections").at("DATA").size(),
              mode.parallelInput ? 4 : 1);
    EXPECT_EQ(registerCell.at("connections").at("OUT").size(),
              mode.parallelOutput ? 4 : 1);
    EXPECT_EQ(registerCell.at("connections").contains("LOAD"),
              mode.parallelInput && !mode.parallelOutput);
    EXPECT_EQ(registerCell.at("parameters").contains("LOAD_POLARITY"),
              mode.parallelInput && !mode.parallelOutput);
    EXPECT_FALSE(registerCell.at("parameters").contains("INPUT_PARALLEL"));
    EXPECT_FALSE(registerCell.at("parameters").contains("OUTPUT_PARALLEL"));

    const auto importedCircuit  = roundTrip(reg);
    const auto importedRegister = findComponent<Register>(importedCircuit);
    ASSERT_TRUE(importedRegister);
    EXPECT_EQ(importedRegister->getPropertyValue<int>("size"), 4);
    EXPECT_EQ(importedRegister->getPropertyValue<std::string>("inputType"),
              std::optional<std::string>(mode.parallelInput ? Register::ParallelType
                                                            : Register::SerialType));
    EXPECT_EQ(importedRegister->getPropertyValue<std::string>("outputType"),
              std::optional<std::string>(mode.parallelOutput ? Register::ParallelType
                                                             : Register::SerialType));
  }
}

TEST(YosysTest, RejectsMalformedCustomTechnologyCells)
{
  auto component = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>());
  const auto reject = [](const nlohmann::json& design) {
    EXPECT_THROW((void)SILICON::yosys::deserialize(design.dump()), std::runtime_error);
  };

  auto missingPort = exportComponent(component);
  onlyModule(missingPort)["cells"].begin().value()["connections"].erase("D");
  reject(missingPort);

  auto unexpectedPort = exportComponent(component);
  onlyModule(unexpectedPort)["cells"].begin().value()["connections"]["EXTRA"] =
      nlohmann::json::array({2});
  reject(unexpectedPort);

  auto incorrectWidth = exportComponent(component);
  onlyModule(incorrectWidth)["cells"].begin().value()["connections"]["D"].push_back(3);
  reject(incorrectWidth);

  auto invalidBoolean = exportComponent(component);
  onlyModule(invalidBoolean)["cells"].begin().value()["parameters"]["CLK_POLARITY"] =
      "10";
  reject(invalidBoolean);

  auto unsupportedPolarity = exportComponent(component);
  onlyModule(unsupportedPolarity)["cells"].begin().value()["parameters"]["SET_POLARITY"] =
      "0";
  reject(unsupportedPolarity);

  auto latch =
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>());
  auto unsupportedLatchPolarity = exportComponent(latch);
  onlyModule(unsupportedLatchPolarity)["cells"].begin().value()["parameters"]
                                                               ["EN_POLARITY"] = "0";
  reject(unsupportedLatchPolarity);

  auto  duplicateDriver = exportComponent(component);
  auto& duplicateConnections =
      onlyModule(duplicateDriver)["cells"].begin().value()["connections"];
  duplicateConnections["QN"] = duplicateConnections["Q"];
  reject(duplicateDriver);

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(3), Bus(3)}, Bus(3),
                                            std::make_shared<Wire>());
  auto malformedAdder = exportComponent(adder);
  onlyModule(malformedAdder)["cells"].begin().value()["parameters"]["WIDTH"] =
      SILICON::yosys::SerializationContext::parameter(4);
  reject(malformedAdder);

  auto missingRegisterLoad = exportComponent(registerWithMode(true, false));
  onlyModule(missingRegisterLoad)["cells"].begin().value()["connections"].erase("LOAD");
  reject(missingRegisterLoad);

  auto unsupportedRegisterPolarity = exportComponent(registerWithMode(false, true));
  onlyModule(unsupportedRegisterPolarity)["cells"].begin().value()["parameters"]
                                                                  ["EN_POLARITY"] = "0";
  reject(unsupportedRegisterPolarity);

  auto unexpectedRegisterLoad = exportComponent(registerWithMode(true, true));
  onlyModule(unexpectedRegisterLoad)["cells"].begin().value()["connections"]["LOAD"] =
      nlohmann::json::array({"0"});
  reject(unexpectedRegisterLoad);

  EXPECT_THROW((void)exportComponent(std::make_shared<HalfAdder>()), std::runtime_error);
}

TEST(YosysTest, EncodesWideParametersWithoutTruncatingTheirWidth)
{
  const auto parameter = SILICON::yosys::SerializationContext::parameter(1, 65);
  EXPECT_EQ(parameter.size(), 65);
  EXPECT_EQ(parameter.back(), '1');
}

TEST(YosysTest, ImportsGeneralCombinationalNetlistWithConstants)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json binaryParameters{
      {"A_SIGNED", SerializationContext::parameter(0, 1)},
      {"B_SIGNED", SerializationContext::parameter(0, 1)},
      {"A_WIDTH", SerializationContext::parameter(2)},
      {"B_WIDTH", SerializationContext::parameter(2)},
      {"Y_WIDTH", SerializationContext::parameter(2)},
  };
  const Json unaryParameters{
      {"A_SIGNED", SerializationContext::parameter(0, 1)},
      {"A_WIDTH", SerializationContext::parameter(2)},
      {"Y_WIDTH", SerializationContext::parameter(2)},
  };
  const Json design{
      {"creator", "test"},
      {"modules",
       {{"logic_top",
         {{"attributes", Json::object()},
          {"ports",
           {{"a", {{"direction", "input"}, {"bits", Json::array({2, 3})}}},
            {"y", {{"direction", "output"}, {"bits", Json::array({6, 7})}}}}},
          {"cells",
           {{"generic_and",
             {{"type", "$and"},
              {"parameters", binaryParameters},
              {"connections",
               {{"A", Json::array({2, 3})},
                {"B", Json::array({"1", "0"})},
                {"Y", Json::array({4, 5})}}}}},
            {"generic_not",
             {{"type", "$not"},
              {"parameters", unaryParameters},
              {"connections",
               {{"A", Json::array({4, 5})}, {"Y", Json::array({6, 7})}}}}}}},
          {"netnames", Json::object()}}}}}};

  Circuit imported = Circuit::deserializeYosys(design.dump());
  EXPECT_EQ(imported.getName(), "logic_top");
  EXPECT_EQ(
      componentTypes(imported),
      (std::multiset<std::string>{"AndGate", "ConstantComponent",
                                  "DummyBusInputComponent", "DummyBusOutputComponent",
                                  "NotGate", "NotGate"}));
  auto importedConstant = findComponent<ConstantComponent>(imported);
  ASSERT_TRUE(importedConstant);
  EXPECT_EQ(importedConstant->getPropertyValue<int>("size"), 2);
  EXPECT_EQ(importedConstant->getPropertyValue<std::string>("value"), "01");

  imported.setName("top");
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  const Circuit restored = Circuit::deserialize(imported.serialize(), registry);
  EXPECT_EQ(componentTypes(restored), componentTypes(imported));
  EXPECT_NO_THROW({
    const auto reparsed = nlohmann::json::parse(restored.getYosysJson());
    EXPECT_TRUE(reparsed.is_object());
  });

#ifdef SILICON_TEST_YOSYS_EXECUTABLE
  EXPECT_EQ(validateWithYosys(restored, "import_constants"), 0);
#endif

  auto simulated = std::make_shared<Circuit>(SILICON::yosys::deserialize(design.dump()));
  std::shared_ptr<DummyBusInputComponent>  inputComponent;
  std::shared_ptr<DummyBusOutputComponent> outputComponent;
  for (const auto vertex :
       boost::make_iterator_range(boost::vertices(simulated->getGraph()))) {
    const auto& component = simulated->getGraph()[vertex].component;
    if (auto input = std::dynamic_pointer_cast<DummyBusInputComponent>(component))
      inputComponent = std::move(input);
    if (auto output = std::dynamic_pointer_cast<DummyBusOutputComponent>(component))
      outputComponent = std::move(output);
  }
  ASSERT_TRUE(inputComponent);
  ASSERT_TRUE(outputComponent);
  inputComponent->setState(3);
  Simulator simulator(simulated);
  ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(outputComponent->inputBuses()[0].getCurrentValue(), 2U);
}

TEST(YosysTest, ImportsSubWithYosysWidthAndSignednessSemantics)
{
  const auto import = [](const std::size_t aWidth, const std::size_t bWidth,
                         const std::size_t yWidth, const bool aSigned,
                         const bool bSigned) {
    return std::make_shared<Circuit>(SILICON::yosys::deserialize(
        subtractionDesign(aWidth, bWidth, yWidth, aSigned, bSigned).dump()));
  };

  auto unsignedCircuit = import(3, 5, 4, false, false);
  auto complementer    = findComponent<Complementer>(*unsignedCircuit);
  auto adder           = findComponent<AdderNBits>(*unsignedCircuit);
  ASSERT_TRUE(complementer);
  ASSERT_TRUE(adder);
  auto extender = findComponent<Extender>(*unsignedCircuit);
  ASSERT_TRUE(extender);
  EXPECT_EQ(complementer->getPropertyValue<int>("size"), 5);
  EXPECT_EQ(adder->getPropertyValue<int>("size"), 5);
  EXPECT_EQ(extender->getPropertyValue<int>("inSize"), 3);
  EXPECT_EQ(extender->getPropertyValue<int>("outSize"), 5);
  EXPECT_EQ(extender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UnsignedMode));
  EXPECT_EQ(evaluateBinaryCircuit(unsignedCircuit, 2, 5), 13U);

  // Both signed flags cause sign extension: 3'b110 (-2) - 5'b00011 (3) = -5.
  auto signedCircuit = import(3, 5, 6, true, true);
  auto signedExtender = findComponent<Extender>(*signedCircuit);
  ASSERT_TRUE(signedExtender);
  EXPECT_EQ(signedExtender->getPropertyValue<std::string>("mode"),
            std::string(Extender::SignedMode));
  EXPECT_EQ(evaluateBinaryCircuit(signedCircuit, 6, 3), 59U);

  // A mixed signedness operation is unsigned in Yosys: 6 - 3 = 3.
  auto mixedCircuit = import(3, 5, 6, true, false);
  auto mixedExtender = findComponent<Extender>(*mixedCircuit);
  ASSERT_TRUE(mixedExtender);
  EXPECT_EQ(mixedExtender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UnsignedMode));
  EXPECT_EQ(evaluateBinaryCircuit(mixedCircuit, 6, 3), 3U);

  // Arithmetic is evaluated at the widest width and narrowed to the low Y bits.
  auto narrowedCircuit = import(5, 4, 3, false, false);
  EXPECT_EQ(evaluateBinaryCircuit(narrowedCircuit, 1, 3), 6U);

  auto addDesign = subtractionDesign(3, 5, 4, false, false);
  onlyModule(addDesign)["cells"].begin().value()["type"] = "$add";
  auto addCircuit = std::make_shared<Circuit>(SILICON::yosys::deserialize(addDesign.dump()));
  auto addExtender = findComponent<Extender>(*addCircuit);
  ASSERT_TRUE(addExtender);
  EXPECT_EQ(addExtender->getPropertyValue<int>("inSize"), 3);
  EXPECT_EQ(addExtender->getPropertyValue<int>("outSize"), 5);
  EXPECT_EQ(evaluateBinaryCircuit(addCircuit, 2, 5), 7U);
}

TEST(YosysTest, RaisesSharedConstantEqualityComparisonsToDecoder)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json eqParameters{
      {"A_SIGNED", SerializationContext::parameter(0, 1)},
      {"B_SIGNED", SerializationContext::parameter(0, 1)},
      {"A_WIDTH", SerializationContext::parameter(3)},
      {"B_WIDTH", SerializationContext::parameter(3)},
      {"Y_WIDTH", SerializationContext::parameter(1)},
  };
  const auto eqCell = [&eqParameters](Json constant, const int output) {
    return Json{{"type", "$eq"},
                {"parameters", eqParameters},
                {"connections",
                 {{"A", Json::array({2, 3, 4})},
                  {"B", std::move(constant)},
                  {"Y", Json::array({output})}}}};
  };
  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"select", {{"direction", "input"}, {"bits", Json::array({2, 3, 4})}}},
            {"matches", {{"direction", "output"}, {"bits", Json::array({5, 6})}}}}},
          {"cells",
           {{"match_one", eqCell(Json::array({"1", "0", "0"}), 5)},
            {"match_six", eqCell(Json::array({"0", "1", "1"}), 6)}}},
          {"netnames", Json::object()}}}}}};

  auto circuit = std::make_shared<Circuit>(SILICON::yosys::deserialize(design.dump()));
  auto decoder = findComponent<Decoder>(*circuit);
  ASSERT_TRUE(decoder);
  EXPECT_EQ(decoder->getPropertyValue<int>("selectionSize"), 3);
  EXPECT_EQ(componentTypes(*circuit).count("Decoder"), 1);

  Simulator simulator(circuit);
  ASSERT_EQ(simulator.setBus(decoder->inputBuses()[1], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::HIGH);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::LOW);

  ASSERT_EQ(simulator.setBus(decoder->inputBuses()[1], 6),
            Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::LOW);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::HIGH);

  ASSERT_EQ(simulator.setBus(decoder->inputBuses()[1], 3),
            Simulator::RunResult::Completed);
  EXPECT_EQ(decoder->outputBuses()[0][1]->getCurrentState(), State::LOW);
  EXPECT_EQ(decoder->outputBuses()[0][6]->getCurrentState(), State::LOW);
}

TEST(YosysTest, ConnectionReaderEnforcesRolesWidthsAndDriverOwnership)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json parameters{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                        {"A_WIDTH", SerializationContext::parameter(1)},
                        {"Y_WIDTH", SerializationContext::parameter(1)}};
  const auto designWithCells = [](Json cells) {
    return Json{{"modules",
                 {{"top",
                   {{"attributes", Json::object()},
                    {"ports", Json::object()},
                    {"cells", std::move(cells)},
                    {"netnames", Json::object()}}}}}};
  };
  const auto notCell = [&parameters](Json input, Json output) {
    return Json{{"type", "$not"},
                {"parameters", parameters},
                {"connections", {{"A", std::move(input)}, {"Y", std::move(output)}}}};
  };

  const Json constantConsumer =
      designWithCells({{"not", notCell(Json::array({"1"}), Json::array({2}))}});
  EXPECT_NO_THROW((void)SILICON::yosys::deserialize(constantConsumer.dump()));

  const Json constantDriver =
      designWithCells({{"not", notCell(Json::array({2}), Json::array({"0"}))}});
  EXPECT_THROW((void)SILICON::yosys::deserialize(constantDriver.dump()),
               std::runtime_error);

  Json incorrectWidth = constantConsumer;
  incorrectWidth["modules"]["top"]["cells"]["not"]["parameters"]["A_WIDTH"] =
      SerializationContext::parameter(2);
  EXPECT_THROW((void)SILICON::yosys::deserialize(incorrectWidth.dump()),
               std::runtime_error);

  const Json duplicateDriver =
      designWithCells({{"first", notCell(Json::array({"0"}), Json::array({2}))},
                       {"second", notCell(Json::array({"1"}), Json::array({2}))}});
  EXPECT_THROW((void)SILICON::yosys::deserialize(duplicateDriver.dump()),
               std::runtime_error);
}

TEST(YosysTest, ImportsEveryCellShapeEmittedBySilicon)
{
  std::vector<Component_ptr> components;
  components.push_back(std::make_shared<Extender>(Bus(3), Bus(5)));
  components.push_back(std::make_shared<Complementer>(Bus(4), Bus(4)));
  components.push_back(std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>()));

  auto mux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  mux->setProperty("busSize", 2);
  components.push_back(std::move(mux));

  auto demux = std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4));
  demux->setProperty("busSize", 2);
  components.push_back(std::move(demux));

  components.push_back(std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), nullptr, nullptr,
      std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      nullptr, nullptr, std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(4)));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(1)));
  components.push_back(
      std::make_shared<WireMerger>(std::vector<Bus>{Bus(1), Bus(1)}, Bus(2)));

  for (std::size_t index = 0; index < components.size(); ++index) {
    SCOPED_TRACE(std::format("component {} ({})", index, components[index]->typeName()));
    const auto exported = exportComponent(components[index]).dump();
    EXPECT_NO_THROW({
      const auto imported = SILICON::yosys::deserialize(exported);
      const auto reparsed = nlohmann::json::parse(imported.getYosysJson());
      EXPECT_TRUE(reparsed.is_object());
    });
  }
}

TEST(YosysTest, SelectsExplicitOrUniqueTopModule)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json emptyModule{{"attributes", Json::object()},
                         {"ports", Json::object()},
                         {"cells", Json::object()},
                         {"netnames", Json::object()}};
  Json       topModule           = emptyModule;
  topModule["attributes"]["top"] = SerializationContext::parameter(1, 1);
  const Json design{{"modules", {{"helper", emptyModule}, {"selected", topModule}}}};

  EXPECT_EQ(SILICON::yosys::deserialize(design.dump()).getName(), "selected");
  EXPECT_EQ(SILICON::yosys::deserialize(design.dump(), "helper").getName(), "helper");

  Json ambiguous                                 = design;
  ambiguous["modules"]["selected"]["attributes"] = Json::object();
  EXPECT_THROW((void)SILICON::yosys::deserialize(ambiguous.dump()), std::runtime_error);

  Json twoTops = design;
  twoTops["modules"]["helper"]["attributes"]["top"] =
      SerializationContext::parameter(1, 1);
  EXPECT_THROW((void)SILICON::yosys::deserialize(twoTops.dump()), std::runtime_error);
  EXPECT_THROW((void)SILICON::yosys::deserialize(design.dump(), "missing"),
               std::runtime_error);
}

TEST(YosysTest, RejectsUnsupportedOrLossyYosysConstructs)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json notParameters{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                           {"A_WIDTH", SerializationContext::parameter(1)},
                           {"Y_WIDTH", SerializationContext::parameter(1)}};
  const auto designWithCell = [](Json cell) {
    return Json{{"modules",
                 {{"top",
                   {{"attributes", Json::object()},
                    {"ports", Json::object()},
                    {"cells", {{"cell", std::move(cell)}}},
                    {"netnames", Json::object()}}}}}};
  };

  const Json highImpedance = designWithCell(
      Json{{"type", "$not"},
           {"parameters", notParameters},
           {"connections", {{"A", Json::array({"z"})}, {"Y", Json::array({2})}}}});
  EXPECT_THROW((void)SILICON::yosys::deserialize(highImpedance.dump()),
               std::runtime_error);

  const Json hierarchy = designWithCell(Json{{"type", "child"},
                                             {"parameters", Json::object()},
                                             {"connections", Json::object()}});
  EXPECT_THROW((void)SILICON::yosys::deserialize(hierarchy.dump()), std::runtime_error);

  Json memoryDesign{{"modules",
                     {{"top",
                       {{"attributes", Json::object()},
                        {"ports", Json::object()},
                        {"cells", Json::object()},
                        {"memories", {{"memory", {{"width", 1}, {"size", 1}}}}}}}}}};
  EXPECT_THROW((void)SILICON::yosys::deserialize(memoryDesign.dump()),
               std::runtime_error);
}

TEST(YosysTest, RejectsUnsupportedThirdPartyComponent)
{
  class Unsupported final : public Component {
  public:
    std::string_view typeName() const override { return "Unsupported"; }
    void             simulate(Simulator&) override {}
  };

  Circuit circuit(std::make_shared<Unsupported>(), false);
  EXPECT_THROW((void)circuit.getYosysJson(), std::runtime_error);
}

TEST(YosysTest, YosysAcceptsEveryBuiltInLowering)
{
#ifndef SILICON_TEST_YOSYS_EXECUTABLE
  GTEST_SKIP() << "Yosys executable was not found when tests were configured";
#else
  std::vector<Component_ptr> components;
  components.push_back(std::make_shared<AndGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>(),
                            std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<OrGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<NotGate>(std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<NandGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<NorGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<XorGate>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<Extender>(Bus(3), Bus(5)));
  components.push_back(std::make_shared<Complementer>(Bus(4), Bus(4)));
  components.push_back(std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)},
                                                    Bus(4), std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>()));
  auto busMux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  busMux->setProperty("busSize", 2);
  components.push_back(busMux);
  components.push_back(std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4)));
  auto busDemux = std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4));
  busDemux->setProperty("busSize", 2);
  components.push_back(busDemux);
  components.push_back(std::make_shared<Decoder>(Bus(1), Bus(2), Bus(4)));
  components.push_back(
      std::make_shared<WireSplitter>(Bus(2), std::vector<Bus>{Bus(1), Bus(1)}));
  components.push_back(
      std::make_shared<WireMerger>(std::vector<Bus>{Bus(1), Bus(1)}, Bus(2)));
  components.push_back(std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<JKFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(4)));
  components.push_back(std::make_shared<Register>(Bus(1), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(4)));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(1)));
  auto serialRegister = std::make_shared<Register>(Bus(2), std::make_shared<Wire>(),
                                                   std::make_shared<Wire>(),
                                                   std::make_shared<Wire>(), Bus(2));
  serialRegister->setProperty("inputType", std::string(Register::SerialType));
  serialRegister->setProperty("outputType", std::string(Register::SerialType));
  components.push_back(serialRegister);

  for (std::size_t index = 0; index < components.size(); ++index) {
    SCOPED_TRACE(std::format("component {} ({})", index, components[index]->typeName()));
    const auto circuit = circuitWithBoundaryPorts(components[index]);
    EXPECT_EQ(validateWithYosys(circuit, std::format("component_{}", index)), 0);
    EXPECT_NO_THROW((void)SILICON::yosys::deserialize(circuit.getYosysJson()));
    const auto verilog = SILICON::yosys::exportVerilog(circuit);
    EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
    EXPECT_NO_THROW((void)SILICON::yosys::importVerilog(verilog, "top"));
  }
#endif
}

TEST(YosysTest, ExportsSubcircuitsAsHierarchicalModules)
{
  if (!ComponentRegistry::instance().hasType(AndGate::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto& registry = SILICON::project::DocumentStore::active();
  registry.clear();
  registry.upsertDocument(
      {SILICON::project::subcircuitPathForSlug("and_child"),
       andSubcircuitDocument()});

  {
    auto instance = std::make_shared<SubcircuitComponent>();
    instance->setProperty("slug", std::string("and_child"));
    auto circuit = circuitWithBoundaryPorts(instance);

    const auto json = nlohmann::json::parse(circuit.getYosysJson());
    ASSERT_TRUE(json.at("modules").contains("top"));
    ASSERT_TRUE(json.at("modules").contains("and_child"));
    EXPECT_TRUE(std::ranges::any_of(
        json.at("modules").at("top").at("cells"),
        [](const auto& cell) { return cell.at("type") == "and_child"; }));
#ifdef SILICON_TEST_YOSYS_EXECUTABLE
    EXPECT_EQ(validateWithYosys(circuit, "hierarchy"), 0);
    const auto verilog = SILICON::yosys::exportVerilog(circuit);
    EXPECT_NE(verilog.find("module and_child"), std::string::npos);
    EXPECT_NO_THROW((void)SILICON::yosys::importVerilog(verilog, "top"));
#endif
  }

  registry.clear();
}

TEST(YosysTest, YosysReadJsonAcceptsExport)
{
#ifndef SILICON_TEST_YOSYS_EXECUTABLE
  GTEST_SKIP() << "Yosys executable was not found when tests were configured";
#else
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
  circuit.setName("top");

  EXPECT_EQ(validateWithYosys(circuit, "simple"), 0);
#endif
}

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
  const auto discovered = SILICON::yosys::runScript("help echo");
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

TEST(YosysToolTest, ImportsCombinationalVerilogAndFlattensHelpers)
{
  constexpr std::string_view source = R"(
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

  const Circuit circuit = SILICON::yosys::importVerilog(source, "selected");
  EXPECT_EQ(circuit.getName(), "selected");
  EXPECT_EQ(componentTypes(circuit),
            (std::multiset<std::string>{"AndGate", "DummyInputComponent",
                                        "DummyInputComponent", "DummyOutputComponent"}));
  EXPECT_THROW((void)SILICON::yosys::importVerilog(source, "selected; delete selected"),
               std::invalid_argument);
  EXPECT_THROW((void)SILICON::yosys::importVerilog(source, "missing"),
               std::runtime_error);
}

TEST(YosysToolTest, ImportsZeroExtendedOutputAsUnsignedExtender)
{
  constexpr std::string_view source = R"(
    module a(output [7:0] bus_out, input in);
      assign bus_out = { 7'h00, in };
    endmodule
  )";

  const Circuit circuit  = SILICON::yosys::importVerilog(source, "a");
  const auto    extender = findComponent<Extender>(circuit);
  ASSERT_TRUE(extender);
  EXPECT_EQ(extender->getPropertyValue<int>("inSize"), 1);
  EXPECT_EQ(extender->getPropertyValue<int>("outSize"), 8);
  EXPECT_EQ(extender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UnsignedMode));
  EXPECT_FALSE(findComponent<ConstantComponent>(circuit));
}

TEST(YosysToolTest, LowersPriorityMuxCellsBeforeImport)
{
  constexpr std::string_view source = R"(
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

  const Circuit circuit = SILICON::yosys::importVerilog(source, "top");
  EXPECT_GT(componentTypes(circuit).count("Multiplexer"), 0);
}

TEST(YosysToolTest, ImportsSequentialVerilog)
{
  constexpr std::string_view source  = R"(
    module storage(input d, input clk, output reg q);
      always @(posedge clk)
        q <= d;
    endmodule
  )";
  const Circuit              circuit = SILICON::yosys::importVerilog(source, "storage");
  EXPECT_TRUE(componentTypes(circuit).contains("DFlipFlop"));
}

TEST(YosysToolTest, RaisesCaseDecoderAndImportsItsMuxTree)
{
  constexpr std::string_view source = R"(
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

  const Circuit circuit = SILICON::yosys::importVerilog(source, "top");
  EXPECT_EQ(componentTypes(circuit).count("Decoder"), 1);
  EXPECT_GT(componentTypes(circuit).count("Multiplexer"), 0);
}

TEST(YosysToolTest, FoldsExhaustiveCaseIntoOneWideMultiplexer)
{
  constexpr std::string_view source = R"(
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

  const Circuit circuit = SILICON::yosys::importVerilog(source, "mux");
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  EXPECT_EQ(componentTypes(circuit).count("Decoder"), 0);
  EXPECT_EQ(componentTypes(circuit).count("OrGate"), 0);

  const auto mux = findComponent<Multiplexer>(circuit);
  ASSERT_TRUE(mux);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 2);
  EXPECT_EQ(mux->getPropertyValue<int>("busSize"), 4);
  EXPECT_EQ(mux->inputBuses().size(), 5);
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

TEST(YosysToolTest, MapsVerilogToSiliconTechnologyCells)
{
  const auto mappedTypes = [](const std::string_view source, const std::string_view top) {
    return cellTypes(onlyModule(nlohmann::json::parse(
        SILICON::yosys::importVerilog(source, top).getYosysJson())));
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
            std::multiset<std::string>{"SILICON_ADDER"});
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
  auto                       latchCircuit =
      std::make_shared<Circuit>(SILICON::yosys::importVerilog(latchSource, "top"));
  auto latch = findComponent<DLatch>(*latchCircuit);
  ASSERT_TRUE(latch);
  Simulator latchSimulator(latchCircuit);
  EXPECT_EQ(latchSimulator.setBus(latch->inputBuses()[0], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(latch->outputBuses()[0][0]->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(latchSimulator.setBus(latch->inputBuses()[1], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(latch->outputBuses()[0][0]->getCurrentState(), State::HIGH);
  EXPECT_EQ(latch->outputBuses()[1][0]->getCurrentState(), State::LOW);
  EXPECT_EQ(latchSimulator.setBus(latch->inputBuses()[1], 0),
            Simulator::RunResult::Completed);
  EXPECT_EQ(latchSimulator.setBus(latch->inputBuses()[0], 0),
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
    auto circuit =
        std::make_shared<Circuit>(SILICON::yosys::importVerilog(source, "top"));
    auto dff = findComponent<DFlipFlop>(*circuit);
    if (!dff)
      throw std::runtime_error("Mapped D flip-flop was not reconstructed");
    Simulator  simulator(circuit);
    const bool positive = edge == "posedge";
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[1], positive ? 0 : 1),
              Simulator::RunResult::Completed);
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[0], 1), Simulator::RunResult::Completed);
    EXPECT_EQ(simulator.setBus(dff->inputBuses()[1], positive ? 1 : 0),
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
  auto                       enabledCircuit =
      std::make_shared<Circuit>(SILICON::yosys::importVerilog(enabledSource, "top"));
  auto dffe = findComponent<EFlipFlop>(*enabledCircuit);
  ASSERT_TRUE(dffe);
  Simulator enabledSimulator(enabledCircuit);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[2], 0),
            Simulator::RunResult::Completed);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[0], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[1], 0),
            Simulator::RunResult::Completed);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[2], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(dffe->outputBuses()[0][0]->getCurrentState(), State::UNKNOWN);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[2], 0),
            Simulator::RunResult::Completed);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[1], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(enabledSimulator.setBus(dffe->inputBuses()[2], 1),
            Simulator::RunResult::Completed);
  EXPECT_EQ(dffe->outputBuses()[0][0]->getCurrentState(), State::HIGH);

  constexpr std::string_view adderSource = R"(
    module top(input [3:0] a, input [3:0] b, output [4:0] y);
      assign y = a + b;
    endmodule
  )";
  auto                       adderCircuit =
      std::make_shared<Circuit>(SILICON::yosys::importVerilog(adderSource, "top"));
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
  inputs.at("a")->setState(15);
  inputs.at("b")->setState(1);
  Simulator adderSimulator(adderCircuit);
  ASSERT_EQ(adderSimulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(), 16U);
}

TEST(YosysToolTest, ExportsAdderAsBehavioralExpression)
{
  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4),
                                            std::make_shared<Wire>());
  const auto verilog = SILICON::yosys::exportVerilog(circuitWithBoundaryPorts(adder));
  EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(verilog.find(" + "), std::string::npos);

  const Circuit restored      = SILICON::yosys::importVerilog(verilog, "top");
  const auto    restoredAdder = findComponent<AdderNBits>(restored);
  ASSERT_TRUE(restoredAdder);
  EXPECT_EQ(restoredAdder->getPropertyValue<int>("size"), 4);
}

TEST(YosysToolTest, TechnologyCellsExportAsBehavioralVerilog)
{
  auto component = std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>());
  const Circuit circuit = circuitWithBoundaryPorts(component);
  const auto    verilog = SILICON::yosys::exportVerilog(circuit);
  EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(verilog.find("assign output_0"), std::string::npos);
  EXPECT_NE(verilog.find("assign output_1"), std::string::npos);
  EXPECT_EQ(verilog.find("_auto_"), std::string::npos);
  EXPECT_EQ(verilog.find("$silicon"), std::string::npos);

  const Circuit restored = SILICON::yosys::importVerilog(verilog, "top");
  EXPECT_TRUE(componentTypes(restored).contains("FullAdder"));
  EXPECT_EQ(cellTypes(onlyModule(nlohmann::json::parse(restored.getYosysJson()))),
            std::multiset<std::string>{"SILICON_FULL_ADDER"});

  for (unsigned value = 0; value < 8; ++value) {
    auto simulated =
        std::make_shared<Circuit>(SILICON::yosys::importVerilog(verilog, "top"));
    auto adder = findComponent<FullAdder>(*simulated);
    ASSERT_TRUE(adder);
    Simulator simulator(simulated);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[0], value & 1U),
              Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[1], (value >> 1U) & 1U),
              Simulator::RunResult::Completed);
    ASSERT_EQ(simulator.setBus(adder->inputBuses()[2], (value >> 2U) & 1U),
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
  dffBoundary.setName("top");
  const auto dffVerilog = SILICON::yosys::exportVerilog(dffBoundary);
  EXPECT_EQ(dffVerilog.find("SILICON_"), std::string::npos);
  EXPECT_NE(dffVerilog.find("always @"), std::string::npos);
  auto       dffCircuit =
      std::make_shared<Circuit>(SILICON::yosys::importVerilog(dffVerilog, "top"));
  auto restoredDff = findComponent<DFlipFlop>(*dffCircuit);
  ASSERT_TRUE(restoredDff);
  EXPECT_EQ(restoredDff->getPropertyValue<std::string>("triggerEdge"),
            std::optional<std::string>("NET"));
  Simulator dffSimulator(dffCircuit);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[1], 1),
            Simulator::RunResult::Completed);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[0], 1),
            Simulator::RunResult::Completed);
  ASSERT_EQ(dffSimulator.setBus(restoredDff->inputBuses()[1], 0),
            Simulator::RunResult::Completed);
  EXPECT_EQ(restoredDff->outputBuses()[0][0]->getCurrentState(), State::HIGH);
  EXPECT_EQ(restoredDff->outputBuses()[1][0]->getCurrentState(), State::LOW);
}

TEST(YosysToolTest, ReportsMissingTechnologyLibrary)
{
  const auto missing = std::filesystem::temp_directory_path()
                       / "silicon_yosys_library_that_does_not_exist";
  try {
    (void)SILICON::yosys::importVerilog(
        "module top; endmodule", "top",
        {.executable = std::filesystem::path(SILICON_TEST_YOSYS_EXECUTABLE),
         .technologyLibraryDirectory = missing});
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
  circuit.setName("top");

  const auto verilog = SILICON::yosys::exportVerilog(circuit);
  EXPECT_NE(verilog.find("module top(input a, input b, output y);"),
            std::string::npos);
  EXPECT_EQ(verilog.find("\n  input a;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  wire a;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  output y;"), std::string::npos);
  EXPECT_EQ(verilog.find("\n  wire y;"), std::string::npos);
  EXPECT_EQ(verilog.find("_auto_"), std::string::npos);
  EXPECT_EQ(verilog.find("$silicon"), std::string::npos);
  EXPECT_NO_THROW({
    const Circuit reparsed = SILICON::yosys::importVerilog(verilog, "top");
    EXPECT_TRUE(componentTypes(reparsed).contains("AndGate"));
  });
}

TEST(YosysToolTest, ExportsWideMuxAsCaseStatement)
{
  auto mux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  mux->setProperty("busSize", 4);
  mux->setProperty("selectionSize", 2);
  Circuit circuit = circuitWithBoundaryPorts(mux);

  const auto verilog = SILICON::yosys::exportVerilog(circuit);
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
  EXPECT_NO_THROW((void)SILICON::yosys::importVerilog(verilog, "top"));
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
  circuit.setName("sr_latch");

  const auto verilog = SILICON::yosys::exportVerilog(circuit);
  EXPECT_NE(verilog.find("assign nq = ~("), std::string::npos);
  EXPECT_NE(verilog.find("assign q = ~("), std::string::npos);
  EXPECT_EQ(verilog.find("NorGate_"), std::string::npos);
  EXPECT_NO_THROW((void)SILICON::yosys::importVerilog(verilog, "sr_latch"));
}

TEST(YosysToolTest, ImportsVectorDffAsRegister)
{
  constexpr std::string_view source = R"(
    module top(input clk, input [3:0] d, output reg [3:0] q);
      always @(posedge clk) q <= d;
    endmodule
  )";

  const Circuit imported = SILICON::yosys::importVerilog(source, "top");
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
  const auto                 firstCircuit = SILICON::yosys::importVerilog(source, "top");
  const auto roundTrippedVerilog          = SILICON::yosys::exportVerilog(firstCircuit);
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
    input->setState(inputValue);
    Simulator simulator(sharedCircuit);
    if (simulator.runUntilIdle() != Simulator::RunResult::Completed)
      throw std::runtime_error("Round-trip circuit simulation did not complete");
    return output->inputBuses()[0].getCurrentValue();
  };

  for (std::uint64_t value = 0; value < 4; ++value) {
    SCOPED_TRACE(std::format("input {}", value));
    EXPECT_EQ(evaluate(SILICON::yosys::importVerilog(source, "top"), value),
              evaluate(SILICON::yosys::importVerilog(roundTrippedVerilog, "top"), value));
  }
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
  expectUnavailable(
      [] { (void)SILICON::yosys::importVerilog("module top; endmodule", "top"); });
  expectUnavailable([] {
    const Circuit circuit(Component_set{}, false);
    (void)SILICON::yosys::exportVerilog(circuit);
  });
}

TEST(YosysToolTest, InProcessJsonRemainsAvailableUnderEmscripten)
{
  auto component = std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>());
  Circuit    circuit(component, false);
  const auto json = circuit.getYosysJson();
  EXPECT_NE(json.find("SILICON_HALF_ADDER"), std::string::npos);
  EXPECT_NO_THROW({
    const Circuit restored = Circuit::deserializeYosys(json);
    EXPECT_EQ(componentTypes(restored), componentTypes(circuit));
  });
}
#endif
