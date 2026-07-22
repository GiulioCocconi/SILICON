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
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <ranges>
#include <set>
#include <string>

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
#include <nlohmann/json.hpp>

namespace {

[[nodiscard]] nlohmann::json exportComponent(const Component_ptr& component)
{
  Circuit circuit(component, false);
  circuit.setName("component_test");
  return nlohmann::json::parse(circuit.getYosysJson());
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

  const std::string command = std::format(
      "\"{}\" -q -p \"read_verilog -lib resources/yosys/silicon_cells_bb.v; "
      "read_json {}; hierarchy -check -top top; check -assert\"",
      SILICON_TEST_YOSYS_EXECUTABLE, path.string());
  const int result = std::system(command.c_str());
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

TEST(YosysTest, LowersSequentialComponents)
{
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
    return silicon::yosys::deserialize(exportComponent(component).dump());
  };

  auto dff = std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), nullptr, nullptr,
      std::make_shared<Wire>(), std::make_shared<Wire>());
  dff->setProperty("triggerEdge", std::string("NET"));
  const auto dffDesign = exportComponent(dff);
  const auto& dffCell  = onlyCell(dffDesign);
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
  const auto adderDesign = exportComponent(adder);
  const auto& adderCell  = onlyCell(adderDesign);
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
    EXPECT_THROW((void)silicon::yosys::deserialize(design.dump()), std::runtime_error);
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

  auto  duplicateDriver = exportComponent(component);
  auto& duplicateConnections =
      onlyModule(duplicateDriver)["cells"].begin().value()["connections"];
  duplicateConnections["QN"] = duplicateConnections["Q"];
  reject(duplicateDriver);

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(3), Bus(3)}, Bus(3),
                                            std::make_shared<Wire>());
  auto malformedAdder = exportComponent(adder);
  onlyModule(malformedAdder)["cells"].begin().value()["parameters"]["WIDTH"] =
      silicon::yosys::SerializationContext::parameter(4);
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
  const auto parameter = silicon::yosys::SerializationContext::parameter(1, 65);
  EXPECT_EQ(parameter.size(), 65);
  EXPECT_EQ(parameter.back(), '1');
}

TEST(YosysTest, ImportsGeneralCombinationalNetlistWithConstants)
{
  using silicon::yosys::Json;
  using silicon::yosys::SerializationContext;

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
      (std::multiset<std::string>{"AndGate", "ConstantComponent", "ConstantComponent",
                                  "DummyBusInputComponent", "DummyBusOutputComponent",
                                  "NotGate", "NotGate"}));

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

  auto simulated = std::make_shared<Circuit>(silicon::yosys::deserialize(design.dump()));
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

TEST(YosysTest, ConnectionReaderEnforcesRolesWidthsAndDriverOwnership)
{
  using silicon::yosys::Json;
  using silicon::yosys::SerializationContext;

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
  EXPECT_NO_THROW((void)silicon::yosys::deserialize(constantConsumer.dump()));

  const Json constantDriver =
      designWithCells({{"not", notCell(Json::array({2}), Json::array({"0"}))}});
  EXPECT_THROW((void)silicon::yosys::deserialize(constantDriver.dump()),
               std::runtime_error);

  Json incorrectWidth = constantConsumer;
  incorrectWidth["modules"]["top"]["cells"]["not"]["parameters"]["A_WIDTH"] =
      SerializationContext::parameter(2);
  EXPECT_THROW((void)silicon::yosys::deserialize(incorrectWidth.dump()),
               std::runtime_error);

  const Json duplicateDriver =
      designWithCells({{"first", notCell(Json::array({"0"}), Json::array({2}))},
                       {"second", notCell(Json::array({"1"}), Json::array({2}))}});
  EXPECT_THROW((void)silicon::yosys::deserialize(duplicateDriver.dump()),
               std::runtime_error);
}

TEST(YosysTest, ImportsEveryCellShapeEmittedBySilicon)
{
  std::vector<Component_ptr> components;
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
      const auto imported = silicon::yosys::deserialize(exported);
      const auto reparsed = nlohmann::json::parse(imported.getYosysJson());
      EXPECT_TRUE(reparsed.is_object());
    });
  }
}

TEST(YosysTest, SelectsExplicitOrUniqueTopModule)
{
  using silicon::yosys::Json;
  using silicon::yosys::SerializationContext;

  const Json emptyModule{{"attributes", Json::object()},
                         {"ports", Json::object()},
                         {"cells", Json::object()},
                         {"netnames", Json::object()}};
  Json       topModule           = emptyModule;
  topModule["attributes"]["top"] = SerializationContext::parameter(1, 1);
  const Json design{{"modules", {{"helper", emptyModule}, {"selected", topModule}}}};

  EXPECT_EQ(silicon::yosys::deserialize(design.dump()).getName(), "selected");
  EXPECT_EQ(silicon::yosys::deserialize(design.dump(), "helper").getName(), "helper");

  Json ambiguous                                 = design;
  ambiguous["modules"]["selected"]["attributes"] = Json::object();
  EXPECT_THROW((void)silicon::yosys::deserialize(ambiguous.dump()), std::runtime_error);

  Json twoTops = design;
  twoTops["modules"]["helper"]["attributes"]["top"] =
      SerializationContext::parameter(1, 1);
  EXPECT_THROW((void)silicon::yosys::deserialize(twoTops.dump()), std::runtime_error);
  EXPECT_THROW((void)silicon::yosys::deserialize(design.dump(), "missing"),
               std::runtime_error);
}

TEST(YosysTest, RejectsUnsupportedOrLossyYosysConstructs)
{
  using silicon::yosys::Json;
  using silicon::yosys::SerializationContext;

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
  EXPECT_THROW((void)silicon::yosys::deserialize(highImpedance.dump()),
               std::runtime_error);

  const Json hierarchy = designWithCell(Json{{"type", "child"},
                                             {"parameters", Json::object()},
                                             {"connections", Json::object()}});
  EXPECT_THROW((void)silicon::yosys::deserialize(hierarchy.dump()), std::runtime_error);

  Json memoryDesign{{"modules",
                     {{"top",
                       {{"attributes", Json::object()},
                        {"ports", Json::object()},
                        {"cells", Json::object()},
                        {"memories", {{"memory", {{"width", 1}, {"size", 1}}}}}}}}}};
  EXPECT_THROW((void)silicon::yosys::deserialize(memoryDesign.dump()),
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
    EXPECT_NO_THROW((void)silicon::yosys::deserialize(circuit.getYosysJson()));
  }
#endif
}

TEST(YosysTest, ExportsSubcircuitsAsHierarchicalModules)
{
  if (!ComponentRegistry::instance().hasType(AndGate::Type))
    registerAllComponents(ComponentRegistry::instance());
  auto& registry = silicon::project::DocumentStore::active();
  registry.clear();
  registry.upsertDocument(
      {silicon::project::subcircuitPathForSlug("and_child"),
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
