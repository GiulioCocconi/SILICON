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

TEST(YosysTest, PreservesLiteralBmuxLanesAsSizedConstants)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  // The packed A port contains two signal lanes followed by the LSB-first encodings
  // of 4'b0011 and 4'b0100. Each literal lane should become one four-bit constant.
  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"first", {{"direction", "input"}, {"bits", Json::array({2, 3, 4, 5})}}},
            {"second", {{"direction", "input"}, {"bits", Json::array({6, 7, 8, 9})}}},
            {"select", {{"direction", "input"}, {"bits", Json::array({10, 11})}}},
            {"y", {{"direction", "output"}, {"bits", Json::array({12, 13, 14, 15})}}}}},
          {"cells",
           {{"mux",
             {{"type", "$bmux"},
              {"parameters",
               {{"WIDTH", SerializationContext::parameter(4)},
                {"S_WIDTH", SerializationContext::parameter(2)}}},
              {"connections",
               {{"A", Json::array({2, 3, 4, 5, 6, 7, 8, 9, "1", "1", "0", "0", "0", "0",
                                   "1", "0"})},
                {"S", Json::array({10, 11})},
                {"Y", Json::array({12, 13, 14, 15})}}}}}}},
          {"netnames", Json::object()}}}}}};

  const Circuit circuit = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(circuit).count("ConstantComponent"), 2);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 0);
  EXPECT_EQ(componentTypes(circuit).count("WireSplitter"), 0);

  const auto mux = findComponent<Multiplexer>(circuit);
  ASSERT_TRUE(mux);
  ASSERT_EQ(mux->inputBuses().size(), 5);

  std::map<BusValue, std::shared_ptr<ConstantComponent>> constants;
  for (const auto& component : componentsIn(circuit)) {
    if (auto constant = std::dynamic_pointer_cast<ConstantComponent>(component)) {
      ASSERT_EQ(constant->getPropertyValue<int>("size"), 4);
      constants.emplace(*constant->getPropertyValue<BusValue>("value"), constant);
    }
  }
  ASSERT_TRUE(constants.contains(busValueFromBits("0011")));
  ASSERT_TRUE(constants.contains(busValueFromBits("0100")));
  EXPECT_EQ(mux->inputBuses()[2],
            constants.at(busValueFromBits("0011"))->outputBuses()[0]);
  EXPECT_EQ(mux->inputBuses()[3],
            constants.at(busValueFromBits("0100"))->outputBuses()[0]);
}

TEST(YosysTest, ImportsScalarBmuxAsSeparateLiteralLanesAndRoundTrips)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json design{
      {"modules",
       {{"top",
         {{"attributes", Json::object()},
          {"ports",
           {{"select", {{"direction", "input"}, {"bits", Json::array({2, 3})}}},
            {"y", {{"direction", "output"}, {"bits", Json::array({4})}}}}},
          {"cells",
           {{"mux",
             {{"type", "$bmux"},
              {"parameters",
               {{"WIDTH", SerializationContext::parameter(1)},
                {"S_WIDTH", SerializationContext::parameter(2)}}},
              {"connections",
               {{"A", Json::array({"0", "1", "x", "0"})},
                {"S", Json::array({2, 3})},
                {"Y", Json::array({4})}}}}}}},
          {"netnames", Json::object()}}}}}};

  const Circuit circuit = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(circuit).count("Multiplexer"), 1);
  // Identical literal lanes share one constant producer.
  EXPECT_EQ(componentTypes(circuit).count("ConstantComponent"), 3);
  EXPECT_EQ(componentTypes(circuit).count("WireMerger"), 0);
  EXPECT_EQ(componentTypes(circuit).count("WireSplitter"), 0);

  const auto mux = findComponent<Multiplexer>(circuit);
  ASSERT_TRUE(mux);
  ASSERT_EQ(mux->inputBuses().size(), 5);
  for (std::size_t lane = 0; lane < 4; ++lane)
    EXPECT_EQ(mux->inputBuses()[lane].size(), 1);
  EXPECT_EQ(mux->inputBuses().back().size(), 2);
  EXPECT_EQ(mux->outputBuses()[0].size(), 1);

  const Circuit restored =
      SILICON::yosys::deserialize(SILICON::yosys::serialize(circuit, "top"), "top");
  const auto restoredMux = findComponent<Multiplexer>(restored);
  ASSERT_TRUE(restoredMux);
  ASSERT_EQ(restoredMux->inputBuses().size(), 5);
  EXPECT_TRUE(std::ranges::all_of(restoredMux->inputBuses() | std::views::take(4),
                                  [](const Bus& lane) { return lane.size() == 1; }));
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
  EXPECT_EQ(evaluateBinaryCircuit(unsignedCircuit, 2, 5), valueFor(4, 13));

  // Both signed flags cause sign extension: 3'b110 (-2) - 5'b00011 (3) = -5.
  auto signedCircuit  = import(3, 5, 6, true, true);
  auto signedExtender = findComponent<Extender>(*signedCircuit);
  ASSERT_TRUE(signedExtender);
  EXPECT_EQ(signedExtender->getPropertyValue<std::string>("mode"),
            std::string(Extender::SignedMode));
  EXPECT_EQ(evaluateBinaryCircuit(signedCircuit, 6, 3), valueFor(6, 59));

  // A mixed signedness operation is unsigned in Yosys: 6 - 3 = 3.
  auto mixedCircuit  = import(3, 5, 6, true, false);
  auto mixedExtender = findComponent<Extender>(*mixedCircuit);
  ASSERT_TRUE(mixedExtender);
  EXPECT_EQ(mixedExtender->getPropertyValue<std::string>("mode"),
            std::string(Extender::UnsignedMode));
  EXPECT_EQ(evaluateBinaryCircuit(mixedCircuit, 6, 3), valueFor(6, 3));

  // Arithmetic is evaluated at the widest width and narrowed to the low Y bits.
  auto narrowedCircuit = import(5, 4, 3, false, false);
  EXPECT_EQ(evaluateBinaryCircuit(narrowedCircuit, 1, 3), valueFor(3, 6));

  auto addDesign = subtractionDesign(3, 5, 4, false, false);
  onlyModule(addDesign)["cells"].begin().value()["type"] = "$add";
  auto addCircuit =
      std::make_shared<Circuit>(SILICON::yosys::deserialize(addDesign.dump()));
  auto addExtender = findComponent<Extender>(*addCircuit);
  ASSERT_TRUE(addExtender);
  EXPECT_EQ(addExtender->getPropertyValue<int>("inSize"), 3);
  EXPECT_EQ(addExtender->getPropertyValue<int>("outSize"), 5);
  EXPECT_EQ(evaluateBinaryCircuit(addCircuit, 2, 5), valueFor(4, 7));
}

TEST(YosysTest, ImportsComparisonCellsWithYosysWidthAndSignednessSemantics)
{
  struct ComparisonCase {
    std::string_view type;
    std::string_view mode;
  };
  static constexpr std::array cases{
      ComparisonCase{"$eq", "=="}, ComparisonCase{"$lt", "<"},
      ComparisonCase{"$le", "<="}, ComparisonCase{"$gt", ">"},
      ComparisonCase{"$ge", ">="},
  };

  for (const auto& comparison : cases) {
    SCOPED_TRACE(comparison.type);
    auto       circuit    = std::make_shared<Circuit>(SILICON::yosys::deserialize(
        comparisonDesign(comparison.type, 3, 5, 1, false, false).dump()));
    const auto comparator = findComponent<Comparator>(*circuit);
    ASSERT_TRUE(comparator);
    EXPECT_EQ(comparator->getPropertyValue<int>("size"), 5);
    EXPECT_EQ(comparator->getPropertyValue<std::string>("mode"), comparison.mode);
    EXPECT_EQ(comparator->getPropertyValue<bool>("signed"), false);
    EXPECT_TRUE(findComponent<Extender>(*circuit));
  }

  // 3'b110 is -2 for a signed comparison, but 6 for an unsigned one.
  auto signedLess = std::make_shared<Circuit>(
      SILICON::yosys::deserialize(comparisonDesign("$lt", 3, 5, 1, true, true).dump()));
  EXPECT_EQ(evaluateBinaryCircuit(signedLess, 6, 3), valueFor(1, 1));
  ASSERT_TRUE(findComponent<Comparator>(*signedLess));
  EXPECT_EQ(findComponent<Comparator>(*signedLess)->getPropertyValue<bool>("signed"),
            true);

  auto mixedLess = std::make_shared<Circuit>(
      SILICON::yosys::deserialize(comparisonDesign("$lt", 3, 5, 1, true, false).dump()));
  EXPECT_EQ(evaluateBinaryCircuit(mixedLess, 6, 3), valueFor(1, 0));
  ASSERT_TRUE(findComponent<Comparator>(*mixedLess));
  EXPECT_EQ(findComponent<Comparator>(*mixedLess)->getPropertyValue<bool>("signed"),
            false);

  auto wideOutput = std::make_shared<Circuit>(
      SILICON::yosys::deserialize(comparisonDesign("$ge", 4, 4, 3, false, false).dump()));
  EXPECT_EQ(evaluateBinaryCircuit(wideOutput, 9, 3), valueFor(3, 1));
  EXPECT_TRUE(findComponent<Extender>(*wideOutput));
}

TEST(YosysTest, ImportsIndependentEqualityCells)
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

  const Circuit circuit = SILICON::yosys::deserialize(design.dump());
  EXPECT_EQ(componentTypes(circuit).count("Comparator"), 2);
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
  components.push_back(std::make_shared<Comparator>(std::array<Bus, 2>{Bus(4), Bus(4)},
                                                    std::make_shared<Wire>()));
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
      const auto reparsed =
          nlohmann::json::parse(SILICON::yosys::serialize(imported, "top"));
      EXPECT_TRUE(reparsed.is_object());
    });
  }
}

TEST(YosysTest, SelectsExplicitOrUniqueTopModule)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const auto moduleWithPort = [](const std::string_view portName) {
    return Json{{"attributes", Json::object()},
                {"ports",
                 {{std::string(portName),
                   {{"direction", "input"}, {"bits", Json::array({2, 3})}}}}},
                {"cells", Json::object()},
                {"netnames", Json::object()}};
  };
  Json topModule                 = moduleWithPort("selected");
  topModule["attributes"]["top"] = SerializationContext::parameter(1, 1);
  const Json design{
      {"modules", {{"helper", moduleWithPort("helper")}, {"selected", topModule}}}};

  EXPECT_TRUE(findNamedComponent<DummyBusInputComponent>(
      SILICON::yosys::deserialize(design.dump()), "selected"));
  EXPECT_TRUE(findNamedComponent<DummyBusInputComponent>(
      SILICON::yosys::deserialize(design.dump(), "helper"), "helper"));

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

TEST(YosysTest, BuildsModuleDependencyGraph)
{
  using SILICON::yosys::Json;

  const Json design{
      {"modules",
       {{"top",
         {{"cells",
           {{"second_child", {{"type", "child"}}},
            {"primitive", {{"type", "$and"}}},
            {"first_child", {{"type", "child"}}},
            {"helper_instance", {{"type", "helper"}}}}}}},
        {"helper", {{"cells", {{"leaf_instance", {{"type", "leaf"}}}}}}},
        {"child", {{"cells", Json::object()}}},
        {"leaf", Json::object()},
        {"external", {{"attributes", {{"blackbox", "1"}}}, {"cells", Json::object()}}}}}};

  const auto graph = SILICON::yosys::moduleDependencyGraph(design.dump());

  EXPECT_TRUE(graph.containsModule("top"));
  EXPECT_TRUE(graph.containsModule("child"));
  EXPECT_TRUE(graph.containsModule("helper"));
  EXPECT_TRUE(graph.containsModule("leaf"));
  EXPECT_FALSE(graph.containsModule("$and"));
  EXPECT_FALSE(graph.containsModule("primitive"));
  EXPECT_FALSE(graph.containsModule("external"));

  EXPECT_EQ(graph.modules(),
            (std::vector<std::string>{"child", "helper", "leaf", "top"}));
  EXPECT_EQ(graph.dependenciesOf("top"), (std::vector<std::string>{"child", "helper"}));
  EXPECT_EQ(graph.dependenciesOf("helper"), (std::vector<std::string>{"leaf"}));
  EXPECT_EQ(graph.dependenciesOf("child"), std::vector<std::string>{});
  EXPECT_EQ(graph.dependenciesOf("leaf"), std::vector<std::string>{});
  EXPECT_EQ(graph.dependenciesOf("missing"), std::vector<std::string>{});
}

TEST(YosysTest, OrdersSelectedModuleDependencyClosure)
{
  SILICON::yosys::ModuleDependencyGraph graph;
  for (const auto module : {"top", "other", "left", "right", "leaf", "unused"})
    graph.addModule(module);
  graph.addDependency("top", "left");
  graph.addDependency("top", "right");
  graph.addDependency("left", "leaf");
  graph.addDependency("right", "leaf");
  graph.addDependency("other", "right");

  EXPECT_EQ(graph.dependencyOrder({"top", "other"}),
            (std::vector<std::string>{"leaf", "right", "other", "left", "top"}));
  EXPECT_THROW((void)graph.dependencyOrder({"missing"}), std::invalid_argument);

  graph.addDependency("leaf", "top");
  EXPECT_THROW((void)graph.dependencyOrder({"top"}), std::runtime_error);
}

TEST(YosysTest, ImportsDeclaredModuleCellsAsSubcircuits)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;

  const Json unaryParameters{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                             {"A_WIDTH", SerializationContext::parameter(1)},
                             {"Y_WIDTH", SerializationContext::parameter(1)}};
  const Json design{
      {"modules",
       {{"leaf",
         {{"ports",
           {{"a", {{"direction", "input"}, {"bits", Json::array({2})}}},
            {"y", {{"direction", "output"}, {"bits", Json::array({3})}}}}},
          {"cells",
           {{"invert",
             {{"type", "$not"},
              {"parameters", unaryParameters},
              {"connections", {{"A", Json::array({2})}, {"Y", Json::array({3})}}}}}}}}},
        {"top",
         {{"ports",
           {{"source", {{"direction", "input"}, {"bits", Json::array({4})}}},
            {"result", {{"direction", "output"}, {"bits", Json::array({5})}}}}},
          {"cells",
           {{"child",
             {{"type", "leaf"},
              {"parameters", Json::object()},
              {"connections",
               {{"a", Json::array({4})}, {"y", Json::array({5})}}}}}}}}}}}};

  const Circuit top = SILICON::yosys::deserialize(design.dump(), "top");
  EXPECT_EQ(componentTypes(top),
            (std::multiset<std::string>{"DummyInputComponent", "DummyOutputComponent",
                                        "Subcircuit"}));
  const auto instance = findComponent<SubcircuitComponent>(top);
  ASSERT_TRUE(instance);
  EXPECT_EQ(instance->getPropertyValue<std::string>("slug"),
            std::optional<std::string>("leaf"));
  ASSERT_EQ(instance->inputBuses().size(), 1);
  ASSERT_EQ(instance->outputBuses().size(), 1);
  EXPECT_EQ(instance->inputBuses()[0].size(), 1);
  EXPECT_EQ(instance->outputBuses()[0].size(), 1);
  EXPECT_EQ(instance->importedInputNames(), (std::vector<std::string>{"a"}));
  EXPECT_EQ(instance->importedOutputNames(), (std::vector<std::string>{"y"}));

  const auto serialized = Json::parse(top.serialize());
  const auto serializedInstance =
      std::ranges::find_if(serialized.at("components"), [](const Json& component) {
        return component.value("type", std::string()) == "Subcircuit";
      });
  ASSERT_NE(serializedInstance, serialized.at("components").end());
  EXPECT_EQ(serializedInstance->at("properties").at("slug"), "leaf");
}

TEST(YosysTest, RejectsMalformedModuleDependencyGraphInput)
{
  using SILICON::yosys::Json;

  EXPECT_THROW((void)SILICON::yosys::moduleDependencyGraph("not json"),
               std::runtime_error);
  EXPECT_THROW((void)SILICON::yosys::moduleDependencyGraph(Json::object().dump()),
               std::runtime_error);

  const Json malformedCells{{"modules", {{"top", {{"cells", Json::array()}}}}}};
  EXPECT_THROW((void)SILICON::yosys::moduleDependencyGraph(malformedCells.dump()),
               std::runtime_error);

  const Json missingType{
      {"modules", {{"top", {{"cells", {{"instance", Json::object()}}}}}}}};
  EXPECT_THROW((void)SILICON::yosys::moduleDependencyGraph(missingType.dump()),
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
  EXPECT_THROW((void)SILICON::yosys::serialize(circuit, "top"), std::runtime_error);
}
