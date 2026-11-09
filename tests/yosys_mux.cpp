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

#include <core/elaboration.hpp>

namespace {
using Json = nlohmann::json;

Json muxTreeDesign()
{
  Json ports;
  for (int i = 0; i < 4; ++i)
    ports[std::string(1, 'a' + i)] = {{"direction", "input"},
                                      {"bits", Json::array({2 + 2 * i, 3 + 2 * i})}};
  ports["s"]     = {{"direction", "input"}, {"bits", Json::array({10, 11, 12})}};
  ports["y"]     = {{"direction", "output"}, {"bits", Json::array({24, 25})}};
  const auto mux = [](Json a, Json b, Json s, Json y) {
    return Json{
        {"type", "$mux"},
        {"parameters", {{"WIDTH", SILICON::yosys::SerializationContext::parameter(2)}}},
        {"connections", {{"A", a}, {"B", b}, {"S", s}, {"Y", y}}}};
  };
  const Json cells{{"left", mux({2, 3}, {4, 5}, {10}, {20, 21})},
                   {"right", mux({6, 7}, {8, 9}, {10}, {22, 23})},
                   {"root", mux({20, 21}, {22, 23}, {11}, {24, 25})}};
  return Json{{"modules", {{"top", {{"ports", ports}, {"cells", cells}}}}}};
}

Circuit importMuxStructure(const Json& design)
{
  return SILICON::yosys::deserialize(legalizeExpressions(design.dump(), "silicon_mux"));
}

std::shared_ptr<Circuit> nativeRoundTrip(const Circuit& circuit)
{
  ComponentRegistry registry;
  registerAllComponents(registry);
  const auto restored = Circuit::deserialize(circuit.serialize(), registry);
  return CircuitElaborator(registry).elaborate(restored);
}

void setMuxData(const std::shared_ptr<Circuit>& circuit)
{
  // Four distinct values make incorrect selector bit ordering visible.
  for (int i = 0; i < 4; ++i) {
    const auto input =
        findNamedComponent<DummyBusInputComponent>(*circuit, std::string(1, 'a' + i));
    ASSERT_TRUE(input);
    input->setBusValue(valueFor(input->outputBuses()[0], i));
  }
}
}  // namespace

TEST(YosysMuxTest, CompactsRepeatedChoicesAndPreservesSelectionTable)
{
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif
  auto  design = muxTreeDesign();
  auto& module = onlyModule(design);
  module["cells"].clear();
  const std::array<int, 8> choices{0, 1, 0, 2, 1, 0, 2, 3};
  Json                     data = Json::array();
  for (const auto choice : choices) {
    data.push_back(2 + 2 * choice);
    data.push_back(3 + 2 * choice);
  }
  module["cells"]["table"] = {
      {"type", "$bmux"},
      {"parameters",
       {{"WIDTH", SILICON::yosys::SerializationContext::parameter(2)},
        {"S_WIDTH", SILICON::yosys::SerializationContext::parameter(3)}}},
      {"connections",
       {{"A", data}, {"S", Json::array({10, 11, 12})}, {"Y", Json::array({24, 25})}}}};
  auto circuit = std::make_shared<Circuit>(importMuxStructure(design));
  auto mux     = findComponent<Multiplexer>(*circuit);
  ASSERT_TRUE(mux);
  ASSERT_EQ(mux->inputBuses().size(), 5);
  EXPECT_EQ(mux->selectionTableIndices(),
            (std::vector<unsigned short>{0, 1, 0, 2, 1, 0, 2, 3}));
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 3);
  circuit = nativeRoundTrip(*circuit);
  mux     = findComponent<Multiplexer>(*circuit);
  ASSERT_TRUE(mux);
  ASSERT_EQ(mux->inputBuses().size(), 5);

  // Export expands the aliases back into a standard $bmux lane table.
  const auto  exported = Json::parse(SILICON::yosys::serialize(*circuit, "top"));
  const auto& packed =
      onlyModule(exported).at("cells").begin().value().at("connections").at("A");
  ASSERT_EQ(packed.size(), 16);
  EXPECT_EQ(packed[0], packed[4]);
  EXPECT_EQ(packed[1], packed[5]);

  setMuxData(circuit);
  const auto s = findNamedComponent<DummyBusInputComponent>(*circuit, "s");
  const auto y = findNamedComponent<DummyBusOutputComponent>(*circuit, "y");
  ASSERT_TRUE(s && y);
  Simulator simulator(circuit);
  for (unsigned int selection = 0; selection < choices.size(); ++selection) {
    simulator.setBus(s->outputBuses()[0], valueFor(3, selection));
    ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
    EXPECT_EQ(y->inputBuses()[0].getCurrentValue(), valueFor(2, choices[selection]));
  }
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    simulator.setBus(s->outputBuses()[0], BusValue{State::LOW, state, State::HIGH});
    ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
    EXPECT_EQ(y->inputBuses()[0].getCurrentValue(), BusValue(2, state));
  }

#ifdef SILICON_TEST_YOSYS_EXECUTABLE
  auto restored = std::make_shared<Circuit>(
      importVerilog(SILICON::yosys::writeVerilog(exported.dump()), "top"));
  setMuxData(restored);
  const auto restoredS = findNamedComponent<DummyBusInputComponent>(*restored, "s");
  const auto restoredY = findNamedComponent<DummyBusOutputComponent>(*restored, "y");
  ASSERT_TRUE(restoredS && restoredY);
  Simulator restoredSimulator(restored);
  for (unsigned int selection = 0; selection < choices.size(); ++selection) {
    restoredSimulator.setBus(restoredS->outputBuses()[0], valueFor(3, selection));
    ASSERT_EQ(restoredSimulator.runUntilIdle(), Simulator::RunResult::Completed);
    EXPECT_EQ(restoredY->inputBuses()[0].getCurrentValue(),
              valueFor(2, choices[selection]));
  }
#endif

  // Even a table with one distinct choice must retain its selector: an
  // UNKNOWN/ERROR select has observable behavior in Silicon's mux model.
  Json identical = Json::array();
  for (unsigned int lane = 0; lane < choices.size(); ++lane) {
    identical.push_back(2);
    identical.push_back(3);
  }
  module["cells"]["table"]["connections"]["A"] = identical;
  auto       aliased    = std::make_shared<Circuit>(importMuxStructure(design));
  const auto aliasedMux = findComponent<Multiplexer>(*aliased);
  ASSERT_TRUE(aliasedMux);
  ASSERT_EQ(aliasedMux->inputBuses().size(), 2);
  const auto aliasedS = findNamedComponent<DummyBusInputComponent>(*aliased, "s");
  const auto aliasedY = findNamedComponent<DummyBusOutputComponent>(*aliased, "y");
  ASSERT_TRUE(aliasedS && aliasedY);
  setMuxData(aliased);
  Simulator aliasedSimulator(aliased);
  for (const State state : {State::UNKNOWN, State::ERROR}) {
    aliasedSimulator.setBus(aliasedS->outputBuses()[0],
                            BusValue{state, State::LOW, State::LOW});
    ASSERT_EQ(aliasedSimulator.runUntilIdle(), Simulator::RunResult::Completed);
    EXPECT_EQ(aliasedY->inputBuses()[0].getCurrentValue(), BusValue(2, state));
  }
}

TEST(YosysMuxTest, CombinesBalancedTreesWithRootSelectorPrecedence)
{
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif
  auto circuit = std::make_shared<Circuit>(importMuxStructure(muxTreeDesign()));
  EXPECT_EQ(componentTypes(*circuit).count("Multiplexer"), 1);
  const auto mux = findComponent<Multiplexer>(*circuit);
  ASSERT_TRUE(mux);
  EXPECT_EQ(mux->getPropertyValue<int>("selectionSize"), 2);
  circuit = nativeRoundTrip(*circuit);
  setMuxData(circuit);
  const auto s = findNamedComponent<DummyBusInputComponent>(*circuit, "s");
  const auto y = findNamedComponent<DummyBusOutputComponent>(*circuit, "y");
  ASSERT_TRUE(s && y);
  Simulator            simulator(circuit);
  constexpr std::array states{State::LOW, State::HIGH, State::UNKNOWN, State::ERROR};
  for (const auto root : states)
    for (const auto child : states) {
      simulator.setBus(s->outputBuses()[0], BusValue{child, root, State::LOW});
      ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
      const auto expected = root == State::UNKNOWN || root == State::ERROR
                                ? BusValue(2, root)
                            : child == State::UNKNOWN || child == State::ERROR
                                ? BusValue(2, child)
                                : valueFor(2, (root == State::HIGH ? 2 : 0)
                                                  + (child == State::HIGH ? 1 : 0));
      EXPECT_EQ(y->inputBuses()[0].getCurrentValue(), expected);
    }
}

TEST(YosysMuxTest, KeepsPriorityChainsIndependentSelectorsAndSharedOutputs)
{
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif
  auto  priority = muxTreeDesign();
  auto& module   = onlyModule(priority);
  module["cells"].erase("right");
  module["cells"]["root"]["connections"]["B"] = Json::array({8, 9});
  auto circuit = std::make_shared<Circuit>(importMuxStructure(priority));
  EXPECT_EQ(componentTypes(*circuit).count("Multiplexer"), 2);
  setMuxData(circuit);
  const auto s = findNamedComponent<DummyBusInputComponent>(*circuit, "s");
  const auto y = findNamedComponent<DummyBusOutputComponent>(*circuit, "y");
  ASSERT_TRUE(s && y);
  Simulator simulator(circuit);
  // Unknown selection on an unused branch must not contaminate the chosen lane.
  simulator.setBus(s->outputBuses()[0],
                   BusValue{State::UNKNOWN, State::HIGH, State::LOW});
  ASSERT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(y->inputBuses()[0].getCurrentValue(), valueFor(2, 3));

  auto independent                                              = muxTreeDesign();
  onlyModule(independent)["cells"]["right"]["connections"]["S"] = Json::array({12});
  EXPECT_EQ(componentTypes(importMuxStructure(independent)).count("Multiplexer"), 3);

  auto shared                        = muxTreeDesign();
  onlyModule(shared)["ports"]["tap"] = {{"direction", "output"},
                                        {"bits", Json::array({20, 21})}};
  EXPECT_EQ(componentTypes(importMuxStructure(shared)).count("Multiplexer"), 3);

  auto feedback                                             = muxTreeDesign();
  onlyModule(feedback)["cells"]["left"]["connections"]["A"] = Json::array({24, 25});
  EXPECT_EQ(componentTypes(importMuxStructure(feedback)).count("Multiplexer"), 3);
}

TEST(YosysMuxTest, PassRespectsSelectionKeepAndRepeatedRuns)
{
#ifndef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif
  auto source = muxTreeDesign();
  // Repeated leaf values exercise both folding and the compact canonical cell.
  onlyModule(source)["cells"]["right"]["connections"]["A"] = Json::array({2, 3});
  const auto first = Json::parse(legalizeExpressions(source.dump(), "silicon_mux"));
  ASSERT_EQ(onlyModule(first).at("cells").size(), 1);
  EXPECT_EQ(onlyCell(first).at("type"), "SILICON_MUX");
  EXPECT_EQ(Json::parse(legalizeExpressions(first.dump(), "silicon_mux")).at("modules"),
            first.at("modules"));
  const auto selected = Json::parse(
      legalizeExpressions(source.dump(), "select top/root\nsilicon_mux\nselect *"));
  EXPECT_EQ(onlyModule(selected).at("cells").size(), 3);

  auto kept                                               = source;
  onlyModule(kept)["cells"]["left"]["attributes"]["keep"] = "1";
  EXPECT_EQ(componentTypes(importMuxStructure(kept)).count("Multiplexer"), 3);
  kept                                       = source;
  onlyModule(kept)["netnames"]["left_alias"] = {
      {"hide_name", 0}, {"bits", Json::array({20, 21})}, {"attributes", {{"keep", "1"}}}};
  EXPECT_EQ(componentTypes(importMuxStructure(kept)).count("Multiplexer"), 3);

  for (const auto table : {"0", "11111111", "00000000", "000001x0"}) {
    auto malformed                                                = first;
    onlyModule(malformed)["cells"]["root"]["parameters"]["TABLE"] = table;
    EXPECT_THROW((void)SILICON::yosys::deserialize(malformed.dump()), std::runtime_error);
  }
  auto malformed                                             = first;
  onlyModule(malformed)["cells"]["root"]["connections"]["A"] = Json::array({2, 3});
  EXPECT_THROW((void)SILICON::yosys::deserialize(malformed.dump()), std::runtime_error);
}
