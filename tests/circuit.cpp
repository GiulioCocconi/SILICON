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

#include <core/circuit.hpp>
#include <core/gates.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/simulator.hpp>
#include <extraComponents/arithmetic.hpp>
#include <nlohmann/json.hpp>

namespace {

class StringListTestComponent : public Component {
public:
  static constexpr std::string_view Type = "StringListTestComponent";

  StringListTestComponent() : Component({}, {})
  {
    defineStringListProperty("orientation", "DOWN", {"UP", "DOWN", "LEFT", "RIGHT"});
  }

  std::string_view typeName() const override { return Type; }
  void             simulate(Simulator& sim) override {}
};

std::vector<Component_ptr> componentsIn(const Circuit& circuit)
{
  std::vector<Component_ptr> components;
  const auto&                graph = circuit.getGraph();
  components.reserve(boost::num_vertices(graph));

  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    if (auto component = graph[vertex].component)
      components.push_back(component);
  }

  return components;
}

}  // namespace

TEST(CircuitTest, EmptyCircuit)
{
  Circuit c;
  EXPECT_TRUE(c.getInputs().empty());
  EXPECT_TRUE(c.getOutputs().empty());
  EXPECT_TRUE(c.getComponentsForBus(Bus(1)).empty());
}

TEST(CircuitTest, SingleGateInputsOutputs)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(g, false);

  auto inputs  = c.getInputs();
  auto outputs = c.getOutputs();

  EXPECT_EQ(inputs.size(), 2);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0], Bus({a}));
  EXPECT_EQ(inputs[1], Bus({b}));
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, TwoGatesInternalWire)
{
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto inputs  = c.getInputs();
  auto outputs = c.getOutputs();

  EXPECT_EQ(inputs.size(), 2);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0], Bus({a}));
  EXPECT_EQ(inputs[1], Bus({b}));
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, GetComponentsForBus)
{
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  auto busA   = Bus({a});
  auto busB   = Bus({b});
  auto busMid = Bus({mid});

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto midComponents = c.getComponentsForBus(busMid);
  EXPECT_EQ(midComponents.size(), 2);

  auto aComponents = c.getComponentsForBus(busA);
  EXPECT_EQ(aComponents.size(), 1);
}

TEST(CircuitTest, NotGateSingleInputOutput)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);

  Circuit c(g, false);

  auto inputs  = c.getInputs();
  auto outputs = c.getOutputs();

  EXPECT_EQ(inputs.size(), 1);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0], Bus({a}));
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, ChangeInputsAfterConstruction)
{
  auto a1 = std::make_shared<Wire>();
  auto a2 = std::make_shared<Wire>();
  auto o  = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a1, a2}, o);

  Circuit c(g, false);

  EXPECT_EQ(c.getInputs().size(), 2);
  EXPECT_EQ(c.getInputs()[0], Bus({a1}));
  EXPECT_EQ(c.getInputs()[1], Bus({a2}));

  auto b1 = std::make_shared<Wire>();
  auto b2 = std::make_shared<Wire>();

  g->setInput(0, Bus({b1}));
  g->setInput(1, Bus({b2}));

  auto inputs = c.getInputs();
  EXPECT_EQ(inputs.size(), 2);
  EXPECT_EQ(inputs[0], Bus({b1}));
  EXPECT_EQ(inputs[1], Bus({b2}));
}

TEST(CircuitTest, ChangeOutputAfterConstruction)
{
  auto a  = std::make_shared<Wire>();
  auto b  = std::make_shared<Wire>();
  auto o1 = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o1);

  Circuit c(g, false);

  EXPECT_EQ(c.getOutputs().size(), 1);
  EXPECT_EQ(c.getOutputs()[0], Bus({o1}));

  auto o2 = std::make_shared<Wire>();
  g->setOutput(0, Bus({o2}));

  auto outputs = c.getOutputs();
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({o2}));
}

TEST(CircuitTest, AddComponentToCircuit)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);

  Component_set comps;
  comps.insert(g);

  Circuit c(comps, false);

  EXPECT_EQ(c.getInputs().size(), 1);
  EXPECT_EQ(c.getOutputs().size(), 1);

  auto c2 = std::make_shared<Wire>();
  auto g2 = std::make_shared<NotGate>(o, c2);

  c.addComponent(g2);

  auto inputs  = c.getInputs();
  auto outputs = c.getOutputs();

  EXPECT_EQ(inputs.size(), 1);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0], Bus({a}));
  EXPECT_EQ(outputs[0], Bus({c2}));
}

TEST(CircuitTest, MultiWireBus)
{
  auto a  = std::make_shared<Wire>();
  auto b  = std::make_shared<Wire>();
  auto o1 = std::make_shared<Wire>();
  auto o2 = std::make_shared<Wire>();

  auto inputBus  = Bus({a, b});
  auto outputBus = Bus({o1, o2});

  struct TestComponent : public Component {
    TestComponent(std::vector<Bus> inputs, std::vector<Bus> outputs)
      : Component(std::move(inputs), std::move(outputs))
    {
    }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto g = std::make_shared<TestComponent>(std::vector<Bus>{inputBus},
                                           std::vector<Bus>{outputBus});

  Circuit c(g, false);

  auto inputs  = c.getInputs();
  auto outputs = c.getOutputs();

  EXPECT_EQ(inputs.size(), 1);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0], inputBus);
  EXPECT_EQ(outputs[0], outputBus);
  EXPECT_EQ(inputs[0].size(), 2);
  EXPECT_EQ(outputs[0].size(), 2);
}

TEST(CircuitTest, GetSubgraphFanIn)
{
  // a,b → AND(mid) → NOT(o)
  // getSubgraph(mid) should return only the AND gate (fan-in to mid).
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto subCircuit = c.getBackwardsSubgraph(Bus({mid}));

  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(inputs.size(), 2);
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({mid}));
}

TEST(CircuitTest, GetSubgraphFanInThreeLevels)
{
  // a → NOT(x) → AND(x, b) → AND(_, c) → o
  // getSubgraph(Bus({o})) should return all three gates.
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto c = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto y = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto not1 = std::make_shared<NotGate>(a, x);
  auto and1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, y);
  auto and2 = std::make_shared<AndGate>(std::vector<Wire_ptr>{y, c}, o);

  Component_set comps;
  comps.insert(not1);
  comps.insert(and1);
  comps.insert(and2);

  Circuit circuit(comps, false);

  auto subCircuit = circuit.getBackwardsSubgraph(Bus({o}));

  // All inputs should be reachable (a, b, c are all needed to compute o)
  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({o}));

  // 3 components → 3 input buses: a, b, c
  EXPECT_EQ(inputs.size(), 3);
}

TEST(CircuitTest, GetSubgraphPartialFanIn)
{
  // a → NOT(x) → AND(x, b) → AND(_, c) → o
  // getSubgraph(Bus({y})) should return NOT and first AND, but not second AND.
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto c = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto y = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto not1 = std::make_shared<NotGate>(a, x);
  auto and1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, y);
  auto and2 = std::make_shared<AndGate>(std::vector<Wire_ptr>{y, c}, o);

  Component_set comps;
  comps.insert(not1);
  comps.insert(and1);
  comps.insert(and2);

  Circuit circuit(comps, false);

  auto subCircuit = circuit.getBackwardsSubgraph(Bus({y}));

  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({y}));
  EXPECT_EQ(inputs.size(), 2);  // a, b
}

TEST(CircuitTest, GetForwardSubgraphFanOut)
{
  // a → NOT(x) → AND(x, b) → o
  // getForwardSubgraph(a) should return both NOT and AND gates.
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto not1 = std::make_shared<NotGate>(a, x);
  auto and1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, o);

  Component_set comps;
  comps.insert(not1);
  comps.insert(and1);

  Circuit c(comps, false);

  auto subCircuit = c.getForwardSubgraph(Bus({a}));

  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(inputs.size(), 2);  // a, b
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, GetForwardSubgraphPartial)
{
  // a → NOT(x) → AND(x, b) → o
  // getForwardSubgraph(x) should return only AND (downstream of x).
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto not1 = std::make_shared<NotGate>(a, x);
  auto and1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, o);

  Component_set comps;
  comps.insert(not1);
  comps.insert(and1);

  Circuit c(comps, false);

  auto subCircuit = c.getForwardSubgraph(Bus({x}));

  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(inputs.size(), 2);  // x, b
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, GetGraphNotEmpty)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);

  Circuit c(g, false);

  EXPECT_GT(boost::num_vertices(c.getGraph()), 0);
}

TEST(CircuitTest, SplitDagAndNonDagPureDag)
{
  // a,b → AND(mid) → NOT(o)  — no cycles, entire circuit is DAG.
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto blocks = c.splitCyclic();

  // All components should be in a single DAG block.
  ASSERT_EQ(blocks.size(), 1);
  EXPECT_FALSE(blocks[0].isCyclic);
  EXPECT_EQ(blocks[0].circuit.getInputs().size(), 2);
  EXPECT_EQ(blocks[0].circuit.getOutputs().size(), 1);
  EXPECT_EQ(blocks[0].circuit.getOutputs()[0], Bus({o}));
}

TEST(CircuitTest, SplitDagAndNonDagWithCycle)
{
  // Create a cycle: g1(w1) → g2(w2) where w2 feeds back into g1 as input.
  // g1: AND(w1, ext) → w2
  // g2: NOT(w2) → w1   (this creates the cycle w1 → w2 → w1)
  auto ext = std::make_shared<Wire>();
  auto w1  = std::make_shared<Wire>();
  auto w2  = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{w1, ext}, w2);
  auto g2 = std::make_shared<NotGate>(w2, w1);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto blocks = c.splitCyclic();

  // Both gates are in the cycle, so there should be a single cyclic block.
  ASSERT_EQ(blocks.size(), 1);
  EXPECT_TRUE(blocks[0].isCyclic);
  EXPECT_EQ(boost::num_vertices(blocks[0].circuit.getGraph()), 2);
}

TEST(CircuitTest, SplitDagAndNonDagMixed)
{
  // DAG part:  a → NOT(g1) → x
  // Cycle part: y → NOT(g3) → z → NOT(g4) → y  (cycle between g3, g4)
  // g2: AND(x, b) → y  connects DAG to cycle
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto y = std::make_shared<Wire>();
  auto z = std::make_shared<Wire>();

  auto g1 = std::make_shared<NotGate>(a, x);
  auto g2 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, y);
  auto g3 = std::make_shared<NotGate>(y, z);
  auto g4 = std::make_shared<NotGate>(z, y);  // feeds back to y → cycle

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);
  comps.insert(g3);
  comps.insert(g4);

  Circuit c(comps, false);

  auto blocks = c.splitCyclic();

  // Should have a DAG block first (g1, g2), then a cyclic block (g3, g4).
  ASSERT_EQ(blocks.size(), 2);

  // First block should be DAG with g1 and g2.
  EXPECT_FALSE(blocks[0].isCyclic);
  EXPECT_EQ(boost::num_vertices(blocks[0].circuit.getGraph()), 2);

  // Second block should be cyclic with g3 and g4.
  EXPECT_TRUE(blocks[1].isCyclic);
  EXPECT_EQ(boost::num_vertices(blocks[1].circuit.getGraph()), 2);
}

TEST(CircuitTest, SplitDagAndNonDagEmpty)
{
  Circuit c;
  auto    blocks = c.splitCyclic();

  EXPECT_TRUE(blocks.empty());
}

TEST(CircuitTest, SerializeEmptyCircuit)
{
  Circuit c;
  c.setName("main");
  c.setDescription("Main circuit");
  auto    serialized = c.serialize();
  EXPECT_FALSE(serialized.empty());

  auto json = nlohmann::json::parse(serialized);
  EXPECT_EQ(json["components"].size(), 0);
  EXPECT_EQ(json["version"], SILICON_VERSION);
  EXPECT_EQ(json["name"], "main");
  EXPECT_EQ(json["description"], "Main circuit");
}

TEST(CircuitTest, SerializeSingleGate)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(g, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 1);
  EXPECT_EQ(json["components"][0]["type"], "AndGate");
  EXPECT_EQ(json["components"][0]["inputs"].size(), 2);
  EXPECT_EQ(json["components"][0]["outputs"].size(), 1);
}

TEST(CircuitTest, SerializeAllGateTypes)
{
  auto in1  = std::make_shared<Wire>();
  auto in2  = std::make_shared<Wire>();
  auto out1 = std::make_shared<Wire>();
  auto out2 = std::make_shared<Wire>();
  auto out3 = std::make_shared<Wire>();
  auto out4 = std::make_shared<Wire>();
  auto out5 = std::make_shared<Wire>();
  auto out6 = std::make_shared<Wire>();

  auto andg  = std::make_shared<AndGate>(std::vector<Wire_ptr>{in1, in2}, out1);
  auto org   = std::make_shared<OrGate>(std::vector<Wire_ptr>{in1, in2}, out2);
  auto notg  = std::make_shared<NotGate>(in1, out3);
  auto nandg = std::make_shared<NandGate>(std::vector<Wire_ptr>{in1, in2}, out4);
  auto norg  = std::make_shared<NorGate>(std::vector<Wire_ptr>{in1, in2}, out5);
  auto xorg  = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{in1, in2}, out6);

  Component_set comps;
  comps.insert(andg);
  comps.insert(org);
  comps.insert(notg);
  comps.insert(nandg);
  comps.insert(norg);
  comps.insert(xorg);

  Circuit c(comps, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 6);

  std::set<std::string> types;
  for (const auto& comp : json["components"]) {
    types.insert(comp["type"]);
  }
  EXPECT_TRUE(types.contains("AndGate"));
  EXPECT_TRUE(types.contains("OrGate"));
  EXPECT_TRUE(types.contains("NotGate"));
  EXPECT_TRUE(types.contains("NandGate"));
  EXPECT_TRUE(types.contains("NorGate"));
  EXPECT_TRUE(types.contains("XorGate"));
}

TEST(CircuitTest, SerializeMultiWireBus)
{
  auto a  = std::make_shared<Wire>();
  auto b  = std::make_shared<Wire>();
  auto o1 = std::make_shared<Wire>();
  auto o2 = std::make_shared<Wire>();

  auto inputBus  = Bus({a, b});
  auto outputBus = Bus({o1, o2});

  struct TestComponent : public Component {
    TestComponent(std::vector<Bus> inputs, std::vector<Bus> outputs)
      : Component(std::move(inputs), std::move(outputs))
    {
    }
    std::string_view typeName() const override { return "MultiBusComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto g = std::make_shared<TestComponent>(std::vector<Bus>{inputBus},
                                           std::vector<Bus>{outputBus});

  Circuit c(g, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 1);
  EXPECT_EQ(json["components"][0]["type"], "MultiBusComponent");

  EXPECT_EQ(json["components"][0]["inputs"].size(), 1);
  EXPECT_EQ(json["components"][0]["inputs"][0].size(), 2);

  EXPECT_EQ(json["components"][0]["outputs"].size(), 1);
  EXPECT_EQ(json["components"][0]["outputs"][0].size(), 2);
}

TEST(CircuitTest, SerializeWireIdsAreValid)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(g, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  for (const auto& comp : json["components"]) {
    for (const auto& inputBus : comp["inputs"]) {
      for (const auto& wireId : inputBus) {
        if (!wireId.is_null()) {
          EXPECT_GE(wireId.get<uint64_t>(), 0);
        }
      }
    }
    for (const auto& outputBus : comp["outputs"]) {
      for (const auto& wireId : outputBus) {
        if (!wireId.is_null()) {
          EXPECT_GE(wireId.get<uint64_t>(), 0);
        }
      }
    }
  }
}

TEST(CircuitTest, SerializeComplexCircuit)
{
  auto i1  = std::make_shared<Wire>();
  auto i2  = std::make_shared<Wire>();
  auto i3  = std::make_shared<Wire>();
  auto w1  = std::make_shared<Wire>();
  auto w2  = std::make_shared<Wire>();
  auto w3  = std::make_shared<Wire>();
  auto w4  = std::make_shared<Wire>();
  auto out = std::make_shared<Wire>();

  auto n1 = std::make_shared<NotGate>(i1, w1);
  auto n2 = std::make_shared<NotGate>(i2, w2);
  auto a1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{w1, w2}, w3);
  auto n3 = std::make_shared<NotGate>(w3, w4);
  auto o1 = std::make_shared<OrGate>(std::vector<Wire_ptr>{w4, i3}, out);

  Component_set comps;
  comps.insert(n1);
  comps.insert(n2);
  comps.insert(a1);
  comps.insert(n3);
  comps.insert(o1);

  Circuit c(comps, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 5);
  EXPECT_TRUE(json.contains("version"));
  EXPECT_TRUE(json.contains("name"));
}

TEST(CircuitTest, DeserializeEmptyCircuit)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  std::string json = R"({"version": ")" + std::string(SILICON_VERSION)
                     + R"(", "components": [], "name": "test", "description": "demo"})";

  auto c = Circuit::deserialize(json, registry);

  EXPECT_EQ(boost::num_vertices(c.getGraph()), 0);
  EXPECT_EQ(c.getName(), "test");
  EXPECT_EQ(c.getDescription(), "demo");
}

TEST(CircuitTest, DeserializeDefaultsMissingDescription)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  std::string json = R"({"version": ")" + std::string(SILICON_VERSION)
                     + R"(", "components": [], "name": "test"})";

  auto c = Circuit::deserialize(json, registry);

  EXPECT_EQ(c.getName(), "test");
  EXPECT_TRUE(c.getDescription().empty());
}

TEST(CircuitTest, DeserializeSingleGate)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit original(g, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto numVertices = boost::num_vertices(deserialized.getGraph());
  EXPECT_EQ(numVertices, 1);
}

TEST(CircuitTest, DeserializePreservesWireIds)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit original(g, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  EXPECT_EQ(boost::num_vertices(deserialized.getGraph()), 1);
}

TEST(CircuitTest, DeserializeComplexCircuit)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto i1  = std::make_shared<Wire>();
  auto i2  = std::make_shared<Wire>();
  auto i3  = std::make_shared<Wire>();
  auto w1  = std::make_shared<Wire>();
  auto w2  = std::make_shared<Wire>();
  auto w3  = std::make_shared<Wire>();
  auto w4  = std::make_shared<Wire>();
  auto out = std::make_shared<Wire>();

  auto n1 = std::make_shared<NotGate>(i1, w1);
  auto n2 = std::make_shared<NotGate>(i2, w2);
  auto a1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{w1, w2}, w3);
  auto n3 = std::make_shared<NotGate>(w3, w4);
  auto o1 = std::make_shared<OrGate>(std::vector<Wire_ptr>{w4, i3}, out);

  Component_set comps;
  comps.insert(n1);
  comps.insert(n2);
  comps.insert(a1);
  comps.insert(n3);
  comps.insert(o1);

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto numVertices = boost::num_vertices(deserialized.getGraph());
  EXPECT_EQ(numVertices, 5);
}

TEST(CircuitTest, DeserializeAllGateTypes)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto in1  = std::make_shared<Wire>();
  auto in2  = std::make_shared<Wire>();
  auto out1 = std::make_shared<Wire>();
  auto out2 = std::make_shared<Wire>();
  auto out3 = std::make_shared<Wire>();
  auto out4 = std::make_shared<Wire>();
  auto out5 = std::make_shared<Wire>();
  auto out6 = std::make_shared<Wire>();

  auto andg  = std::make_shared<AndGate>(std::vector<Wire_ptr>{in1, in2}, out1);
  auto org   = std::make_shared<OrGate>(std::vector<Wire_ptr>{in1, in2}, out2);
  auto notg  = std::make_shared<NotGate>(in1, out3);
  auto nandg = std::make_shared<NandGate>(std::vector<Wire_ptr>{in1, in2}, out4);
  auto norg  = std::make_shared<NorGate>(std::vector<Wire_ptr>{in1, in2}, out5);
  auto xorg  = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{in1, in2}, out6);

  Component_set comps;
  comps.insert(andg);
  comps.insert(org);
  comps.insert(notg);
  comps.insert(nandg);
  comps.insert(norg);
  comps.insert(xorg);

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  EXPECT_EQ(boost::num_vertices(deserialized.getGraph()), 6);
}

TEST(CircuitTest, DeserializeMultiWireBus)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto in1  = std::make_shared<Wire>();
  auto in2  = std::make_shared<Wire>();
  auto out1 = std::make_shared<Wire>();
  auto out2 = std::make_shared<Wire>();

  auto andg1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{in1, in2}, out1);
  auto andg2 = std::make_shared<AndGate>(std::vector<Wire_ptr>{in1, in2}, out2);

  Component_set comps;
  comps.insert(andg1);
  comps.insert(andg2);

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto numVertices = boost::num_vertices(deserialized.getGraph());
  EXPECT_EQ(numVertices, 2);
}

TEST(CircuitTest, DeserializeInvalidVersionThrows)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  std::string json =
      R"({"version": "invalid_version", "components": [], "name": "test"})";

  EXPECT_THROW((void)Circuit::deserialize(json, registry), std::runtime_error);
}

TEST(CircuitTest, DeserializeHalfAdder)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto a    = std::make_shared<Wire>();
  auto b    = std::make_shared<Wire>();
  auto sum  = std::make_shared<Wire>();
  auto cout = std::make_shared<Wire>();

  auto ha = std::make_shared<HalfAdder>(std::array<Wire_ptr, 2>{a, b}, sum, cout);

  Circuit original(ha, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  EXPECT_EQ(boost::num_vertices(deserialized.getGraph()), 1);
}

TEST(CircuitTest, DeserializeAndNotGates)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto out = std::make_shared<Wire>();

  auto andg = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto notg = std::make_shared<NotGate>(mid, out);

  Component_set comps;
  comps.insert(andg);
  comps.insert(notg);

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  EXPECT_EQ(boost::num_vertices(deserialized.getGraph()), 2);
}

TEST(CircuitTest, SerializeIncludesProperties)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);
  g->setProperty("delay", 2);

  Circuit c(g, false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 1);
  EXPECT_TRUE(json["components"][0].contains("properties"));
  EXPECT_TRUE(json["components"][0]["properties"].contains("delay"));
  EXPECT_EQ(json["components"][0]["properties"]["delay"], 2);
}

TEST(CircuitTest, DeserializeRestoresStringListProperty)
{
  ComponentRegistry registry;
  registry.registerType(
      std::string(StringListTestComponent::Type),
      [] { return std::make_shared<StringListTestComponent>(); });

  auto component = std::make_shared<StringListTestComponent>();
  component->setProperty("orientation", std::string("LEFT"));

  Circuit original(component, false);
  auto    serialized = original.serialize();
  auto    restored   = Circuit::deserialize(serialized, registry);

  const auto components = componentsIn(restored);
  ASSERT_EQ(components.size(), 1);

  const auto restoredComponent = components.front();
  ASSERT_TRUE(restoredComponent);
  EXPECT_EQ(restoredComponent->getPropertyValue<std::string>("orientation").value(),
            "LEFT");
}

TEST(CircuitTest, DeserializeRestoresBitwiseGatePropertiesAndBusWidths)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto gate = std::make_shared<XorGate>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>());
  gate->setProperty("size", 4);
  gate->setProperty("bitwise", true);

  Circuit original(gate, false);
  auto    serialized = original.serialize();
  auto    restored   = Circuit::deserialize(serialized, registry);

  const auto components = componentsIn(restored);
  ASSERT_EQ(components.size(), 1);

  const auto component = components.front();
  ASSERT_TRUE(component);
  EXPECT_EQ(component->typeName(), "XorGate");
  EXPECT_EQ(component->getPropertyValue<bool>("bitwise"), true);
  EXPECT_EQ(component->getPropertyValue<int>("size"), 4);
  ASSERT_EQ(component->getInputs().size(), 2);
  ASSERT_EQ(component->getOutputs().size(), 1);
  EXPECT_EQ(component->getInputs()[0].size(), 4);
  EXPECT_EQ(component->getInputs()[1].size(), 4);
  EXPECT_EQ(component->getOutputs()[0].size(), 4);
}

TEST(CircuitTest, DeserializeRestoresAdderNBitsSizeAndBusWidths)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)}, Bus(4),
                                            std::make_shared<Wire>());
  adder->setProperty("size", 8);

  Circuit original(adder, false);
  auto    serialized = original.serialize();
  auto    restored   = Circuit::deserialize(serialized, registry);

  const auto components = componentsIn(restored);
  ASSERT_EQ(components.size(), 1);

  const auto component = components.front();
  ASSERT_TRUE(component);
  EXPECT_EQ(component->typeName(), "AdderNBits");
  EXPECT_EQ(component->getPropertyValue<int>("size"), 8);
  ASSERT_EQ(component->getInputs().size(), 2);
  ASSERT_EQ(component->getOutputs().size(), 2);
  EXPECT_EQ(component->getInputs()[0].size(), 8);
  EXPECT_EQ(component->getInputs()[1].size(), 8);
  EXPECT_EQ(component->getOutputs()[0].size(), 8);
  EXPECT_EQ(component->getOutputs()[1].size(), 1);
}

TEST(CircuitTest, RemoveComponent)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(g, false);

  EXPECT_EQ(c.getComponentsForBus(Bus({a})).size(), 1);
  EXPECT_EQ(c.getComponentsForBus(Bus({b})).size(), 1);
  EXPECT_EQ(c.getComponentsForBus(Bus({o})).size(), 1);

  c.removeComponent(g);

  EXPECT_TRUE(c.getComponentsForBus(Bus({a})).empty());
  EXPECT_TRUE(c.getComponentsForBus(Bus({b})).empty());
  EXPECT_TRUE(c.getComponentsForBus(Bus({o})).empty());
}

TEST(CircuitTest, RemoveComponentFromLargerCircuit)
{
  auto a      = std::make_shared<Wire>();
  auto b      = std::make_shared<Wire>();
  auto c1_out = std::make_shared<Wire>();
  auto c2_out = std::make_shared<Wire>();

  auto andg = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, c1_out);
  auto notg = std::make_shared<NotGate>(c1_out, c2_out);

  Component_set comps{andg, notg};
  Circuit       circuit(comps, false);

  EXPECT_EQ(circuit.getComponentsForBus(Bus({a})).size(), 1);
  EXPECT_EQ(circuit.getComponentsForBus(Bus({b})).size(), 1);
  EXPECT_EQ(circuit.getComponentsForBus(Bus({c1_out})).size(), 2);
  EXPECT_EQ(circuit.getComponentsForBus(Bus({c2_out})).size(), 1);

  circuit.removeComponent(andg);

  EXPECT_TRUE(circuit.getComponentsForBus(Bus({a})).empty());
  EXPECT_TRUE(circuit.getComponentsForBus(Bus({b})).empty());
  EXPECT_EQ(circuit.getComponentsForBus(Bus({c1_out})).size(), 1);
  EXPECT_EQ(circuit.getComponentsForBus(Bus({c2_out})).size(), 1);
}

TEST(CircuitTest, RemoveNonExistentComponentDoesNotCrash)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();
  auto g = std::make_shared<NotGate>(a, o);

  Circuit c(g, false);

  c.removeComponent(g);

  c.removeComponent(g);
}

TEST(CircuitTest, RemoveNullComponentDoesNotCrash)
{
  Circuit c;

  c.removeComponent(nullptr);
}

TEST(CircuitTest, RemoveMiddleComponent)
{
  auto a       = std::make_shared<Wire>();
  auto b       = std::make_shared<Wire>();
  auto c       = std::make_shared<Wire>();
  auto d       = std::make_shared<Wire>();
  auto and_out = std::make_shared<Wire>();
  auto xor_out = std::make_shared<Wire>();
  auto or_out  = std::make_shared<Wire>();

  auto andg = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, and_out);
  auto xorg = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{and_out, c}, xor_out);
  auto org  = std::make_shared<OrGate>(std::vector<Wire_ptr>{xor_out, d}, or_out);

  Component_set comps{andg, xorg, org};
  Circuit       circuit(comps, false);

  auto beforeInputs  = circuit.getInputs();
  auto beforeOutputs = circuit.getOutputs();

  EXPECT_EQ(beforeInputs.size(), 4);
  EXPECT_EQ(beforeOutputs.size(), 1);

  circuit.removeComponent(xorg);

  auto afterInputs  = circuit.getInputs();
  auto afterOutputs = circuit.getOutputs();

  EXPECT_EQ(afterInputs.size(), 4);
  EXPECT_EQ(afterOutputs.size(), 2);
}

TEST(CircuitTest, GetLevelMapEmpty)
{
  Circuit c;
  auto    levelMap = c.getLevelMap();
  EXPECT_TRUE(levelMap.empty());
}

TEST(CircuitTest, GetLevelMapSingleGate)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(g, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 1);
  EXPECT_EQ(levelMap[g], 0);
}

TEST(CircuitTest, GetLevelMapTwoGatesSeries)
{
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 2);
  EXPECT_EQ(levelMap[g1], 0);
  EXPECT_EQ(levelMap[g2], 1);
}

TEST(CircuitTest, GetLevelMapThreeGatesSeries)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto y = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, x);
  auto g2 = std::make_shared<NotGate>(x, y);
  auto g3 = std::make_shared<NotGate>(y, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);
  comps.insert(g3);

  Circuit c(comps, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 3);
  EXPECT_EQ(levelMap[g1], 0);
  EXPECT_EQ(levelMap[g2], 1);
  EXPECT_EQ(levelMap[g3], 2);
}

TEST(CircuitTest, GetLevelMapIndependentGates)
{
  auto a1 = std::make_shared<Wire>();
  auto b1 = std::make_shared<Wire>();
  auto o1 = std::make_shared<Wire>();
  auto a2 = std::make_shared<Wire>();
  auto o2 = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a1, b1}, o1);
  auto g2 = std::make_shared<NotGate>(a2, o2);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 2);
  EXPECT_EQ(levelMap[g1], 0);
  EXPECT_EQ(levelMap[g2], 0);
}

TEST(CircuitTest, GetLevelMapDiamond)
{
  // a → NOT(x) → AND(x, b) → o
  // Both NOT and b are at level 0, AND is at level 1 (max of NOT level+1 and b level+1 = 1).
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto x = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g1 = std::make_shared<NotGate>(a, x);
  auto g2 = std::make_shared<AndGate>(std::vector<Wire_ptr>{x, b}, o);

  Component_set comps;
  comps.insert(g1);
  comps.insert(g2);

  Circuit c(comps, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 2);
  EXPECT_EQ(levelMap[g1], 0);
  EXPECT_EQ(levelMap[g2], 1);
}

TEST(CircuitTest, GetLevelMapMultiplePathsDifferentDepths)
{
  // a → NOT(w1) ─┐
  //               AND(w3) → NOT(o)
  // b → NOT(w2) ─┘
  // Both NOTs are level 0, AND is level 1, final NOT is level 2.
  auto a  = std::make_shared<Wire>();
  auto b  = std::make_shared<Wire>();
  auto w1 = std::make_shared<Wire>();
  auto w2 = std::make_shared<Wire>();
  auto w3 = std::make_shared<Wire>();
  auto o  = std::make_shared<Wire>();

  auto n1 = std::make_shared<NotGate>(a, w1);
  auto n2 = std::make_shared<NotGate>(b, w2);
  auto a1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{w1, w2}, w3);
  auto n3 = std::make_shared<NotGate>(w3, o);

  Component_set comps;
  comps.insert(n1);
  comps.insert(n2);
  comps.insert(a1);
  comps.insert(n3);

  Circuit c(comps, false);

  auto levelMap = c.getLevelMap();
  ASSERT_EQ(levelMap.size(), 4);
  EXPECT_EQ(levelMap[n1], 0);
  EXPECT_EQ(levelMap[n2], 0);
  EXPECT_EQ(levelMap[a1], 1);
  EXPECT_EQ(levelMap[n3], 2);
}
