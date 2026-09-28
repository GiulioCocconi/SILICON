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

  const auto  json   = nlohmann::json::parse(SILICON::yosys::serialize(circuit, "top"));
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
  const auto  json  = nlohmann::json::parse(SILICON::yosys::serialize(circuit, "top"));
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

TEST(YosysTest, ExportsUnusedAdderCarryOutput)
{
  auto adder =
      std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4), nullptr);

  const auto  exported = exportComponent(adder);
  const auto& carry    = onlyCell(exported).at("connections").at("COUT");
  ASSERT_EQ(carry.size(), 1);
  EXPECT_TRUE(carry.at(0).is_number_integer());
  EXPECT_NO_THROW((void)SILICON::yosys::deserialize(exported.dump()));
}

TEST(YosysTest, EncodesDeclaredUnconnectedInputDefault)
{
  const auto exported = exportComponent(std::make_shared<DefaultedInputYosysComponent>());
  const auto& input   = onlyCell(exported).at("connections").at("A");
  ASSERT_EQ(input.size(), 1);
  EXPECT_EQ(input.at(0), "1");
  EXPECT_NO_THROW((void)SILICON::yosys::deserialize(exported.dump()));
}

TEST(YosysTest, ExtenderLowersToPosAndRoundTripsWithItsModeAndWidths)
{
  auto extender =
      std::make_shared<Extender>(Bus(3), Bus(6), std::string(Extender::SignedMode));
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
  EXPECT_EQ(cellTypes(onlyModule(
                nlohmann::json::parse(SILICON::yosys::serialize(imported, "top")))),
            (std::multiset<std::string>{"$pos", "$pos"}));
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
  EXPECT_EQ(cellTypes(onlyModule(
                nlohmann::json::parse(SILICON::yosys::serialize(imported, "top")))),
            (std::multiset<std::string>{"$pos", "$sub"}));
}

TEST(YosysTest, ComparatorLowersToNativeComparisonCellsAndRoundTrips)
{
  // Re-importing an exported comparator needs the plugin to legalize $eq, $lt, ...
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif

  static constexpr std::array modes{
      std::pair<std::string_view, std::string_view>{"==", "$eq"},
      std::pair<std::string_view, std::string_view>{"<", "$lt"},
      std::pair<std::string_view, std::string_view>{"<=", "$le"},
      std::pair<std::string_view, std::string_view>{">", "$gt"},
      std::pair<std::string_view, std::string_view>{">=", "$ge"},
  };

  for (const auto& [mode, cellType] : modes) {
    SCOPED_TRACE(mode);
    auto comparator = std::make_shared<Comparator>(std::array<Bus, 2>{Bus(5), Bus(5)},
                                                   std::make_shared<Wire>());
    comparator->setProperty("mode", std::string(mode));
    comparator->setProperty("signed", true);

    const auto  exported = exportComponent(comparator);
    const auto& cell     = onlyCell(exported);
    EXPECT_EQ(cell.at("type"), cellType);
    EXPECT_EQ(cell.at("parameters").at("A_SIGNED"), "1");
    EXPECT_EQ(cell.at("parameters").at("B_SIGNED"), "1");
    EXPECT_EQ(cell.at("parameters").at("A_WIDTH"),
              SILICON::yosys::SerializationContext::parameter(5));
    EXPECT_EQ(cell.at("parameters").at("B_WIDTH"),
              SILICON::yosys::SerializationContext::parameter(5));
    EXPECT_EQ(cell.at("parameters").at("Y_WIDTH"),
              SILICON::yosys::SerializationContext::parameter(1));

    const Circuit imported = SILICON::yosys::deserialize(legalizeExpressions(exported.dump()));
    const auto    restored = findComponent<Comparator>(imported);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->getPropertyValue<int>("size"), 5);
    EXPECT_EQ(restored->getPropertyValue<std::string>("mode"), mode);
    EXPECT_EQ(restored->getPropertyValue<bool>("signed"), true);
  }
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
  // The shifter cases export raw $shl, $shr and $sshr cells that only the
  // plugin legalizes, so the round trip needs the plugin.
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif

  const auto roundTrip = [](const Component_ptr& component) {
    return SILICON::yosys::deserialize(legalizeExpressions(exportComponent(component).dump()));
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

  for (const auto& [mode, isSigned, cellType] :
       std::array<std::tuple<std::string_view, bool, std::string_view>, 3>{
           {{Shifter::LeftMode, false, "$shl"},
            {Shifter::RightMode, false, "$shr"},
            {Shifter::RightMode, true, "$sshr"}}}) {
    auto shifter = std::make_shared<Shifter>(Bus(5), Bus(3), Bus(5));
    shifter->setProperty("mode", std::string(mode));
    shifter->setProperty("signed", isSigned);
    const auto design = exportComponent(shifter);
    EXPECT_EQ(onlyCell(design).at("type"), cellType);
    const auto imported = findComponent<Shifter>(roundTrip(shifter));
    ASSERT_TRUE(imported);
    EXPECT_EQ(imported->getPropertyValue<std::string>("mode"), mode);
    EXPECT_EQ(imported->getPropertyValue<bool>("signed"), isSigned);
    EXPECT_EQ(imported->getPropertyValue<int>("size"), 5);
    EXPECT_EQ(imported->getPropertyValue<int>("amountSize"), 3);
  }

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

TEST(YosysTest, SharesOneSplitterAcrossIndexedBusConsumers)
{
  using SILICON::yosys::Json;

  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"a", {{"direction", "input"}, {"bits", Json::array({2, 3})}}},
            {"whole", {{"direction", "output"}, {"bits", Json::array({2, 3})}}},
            {"low", {{"direction", "output"}, {"bits", Json::array({2})}}},
            {"low_copy", {{"direction", "output"}, {"bits", Json::array({2})}}},
            {"high", {{"direction", "output"}, {"bits", Json::array({3})}}}}},
          {"cells", Json::object()},
          {"netnames", Json::object()}}}}}};

  const Circuit circuit = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(circuit).count("WireSplitter"), 1);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 0);

  const auto input    = findNamedComponent<DummyBusInputComponent>(circuit, "a");
  const auto whole    = findNamedComponent<DummyBusOutputComponent>(circuit, "whole");
  const auto low      = findNamedComponent<DummyOutputComponent>(circuit, "low");
  const auto lowCopy  = findNamedComponent<DummyOutputComponent>(circuit, "low_copy");
  const auto high     = findNamedComponent<DummyOutputComponent>(circuit, "high");
  const auto splitter = findComponent<WireSplitter>(circuit);
  ASSERT_TRUE(input && whole && low && lowCopy && high && splitter);
  ASSERT_EQ(splitter->outputBuses().size(), 2);
  EXPECT_EQ(splitter->getPropertyValue<int>("size"), 2);
  EXPECT_EQ(splitter->inputBuses()[0], input->outputBuses()[0]);
  EXPECT_EQ(whole->inputBuses()[0], input->outputBuses()[0]);
  EXPECT_EQ(low->inputBuses()[0], splitter->outputBuses()[0]);
  EXPECT_EQ(lowCopy->inputBuses()[0], splitter->outputBuses()[0]);
  EXPECT_EQ(high->inputBuses()[0], splitter->outputBuses()[1]);
}

TEST(YosysTest, SharesOneMergerAcrossIdenticalAssembledBuses)
{
  using SILICON::yosys::Json;

  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"x", {{"direction", "input"}, {"bits", Json::array({2})}}},
            {"y", {{"direction", "input"}, {"bits", Json::array({3})}}},
            {"q", {{"direction", "output"}, {"bits", Json::array({2, 3})}}},
            {"q_copy", {{"direction", "output"}, {"bits", Json::array({2, 3})}}}}},
          {"cells", Json::object()},
          {"netnames", Json::object()}}}}}};

  const Circuit circuit = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(circuit).count("WireSplitter"), 0);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 1);

  const auto x      = findNamedComponent<DummyInputComponent>(circuit, "x");
  const auto y      = findNamedComponent<DummyInputComponent>(circuit, "y");
  const auto q      = findNamedComponent<DummyBusOutputComponent>(circuit, "q");
  const auto qCopy  = findNamedComponent<DummyBusOutputComponent>(circuit, "q_copy");
  const auto merger = findComponent<WireMerger>(circuit);
  ASSERT_TRUE(x && y && q && qCopy && merger);
  ASSERT_EQ(merger->inputBuses().size(), 2);
  EXPECT_EQ(merger->getPropertyValue<int>("size"), 2);
  EXPECT_EQ(merger->inputBuses()[0], x->outputBuses()[0]);
  EXPECT_EQ(merger->inputBuses()[1], y->outputBuses()[0]);
  EXPECT_EQ(q->inputBuses()[0], merger->outputBuses()[0]);
  EXPECT_EQ(qCopy->inputBuses()[0], merger->outputBuses()[0]);
}

TEST(YosysTest, NormalizesReorderedPartSelectThroughOneSplitterAndMerger)
{
  using SILICON::yosys::Json;

  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"a", {{"direction", "input"}, {"bits", Json::array({2, 3, 4, 5})}}},
            {"q", {{"direction", "output"}, {"bits", Json::array({5, 2})}}}}},
          {"cells", Json::object()},
          {"netnames", Json::object()}}}}}};

  auto circuit = std::make_shared<Circuit>(SILICON::yosys::deserialize(design.dump()));
  EXPECT_EQ(componentTypes(*circuit).count("WireSplitter"), 1);
  EXPECT_EQ(componentTypes(*circuit).count("WireMerger"), 1);

  const auto input    = findNamedComponent<DummyBusInputComponent>(*circuit, "a");
  const auto output   = findNamedComponent<DummyBusOutputComponent>(*circuit, "q");
  const auto splitter = findComponent<WireSplitter>(*circuit);
  const auto merger   = findComponent<WireMerger>(*circuit);
  ASSERT_TRUE(input && output && splitter && merger);
  ASSERT_EQ(splitter->outputBuses().size(), 4);
  ASSERT_EQ(merger->inputBuses().size(), 2);
  EXPECT_EQ(merger->inputBuses()[0], splitter->outputBuses()[3]);
  EXPECT_EQ(merger->inputBuses()[1], splitter->outputBuses()[0]);
  EXPECT_EQ(output->inputBuses()[0], merger->outputBuses()[0]);

  input->setBusValue(valueFor(input->outputBuses()[0], 8));
  Simulator simulator(circuit);
  ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(output->inputBuses()[0].getCurrentValue(),
            valueFor(output->inputBuses()[0], 1));
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

  Circuit imported = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(imported),
            (std::multiset<std::string>{"AndGate", "ConstantComponent",
                                        "DummyBusInputComponent",
                                        "DummyBusOutputComponent", "NotGate"}));
  auto importedConstant = findComponent<ConstantComponent>(imported);
  ASSERT_TRUE(importedConstant);
  EXPECT_EQ(importedConstant->getPropertyValue<int>("size"), 2);
  EXPECT_EQ(importedConstant->getPropertyValue<BusValue>("value"),
            busValueFromBits("01"));
  const auto importedNot = findComponent<NotGate>(imported);
  ASSERT_TRUE(importedNot);
  EXPECT_EQ(importedNot->getPropertyValue<int>("size"), 2);

  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  const Circuit restored = Circuit::deserialize(imported.serialize(), registry);
  EXPECT_EQ(componentTypes(restored), componentTypes(imported));
  ASSERT_TRUE(findComponent<NotGate>(restored));
  EXPECT_EQ(findComponent<NotGate>(restored)->getPropertyValue<int>("size"), 2);
  EXPECT_NO_THROW({
    const auto reparsed =
        nlohmann::json::parse(SILICON::yosys::serialize(restored, "top"));
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
  inputComponent->setBusValue(valueFor(inputComponent->outputBuses()[0], 3));
  Simulator simulator(simulated);
  ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(outputComponent->inputBuses()[0].getCurrentValue(),
            valueFor(outputComponent->inputBuses()[0], 2));
}
