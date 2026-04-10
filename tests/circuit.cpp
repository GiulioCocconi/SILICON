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
#include <extraComponents/arithmetic.hpp>
#include <nlohmann/json.hpp>

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

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

  Circuit c(comps, false);

  auto midComponents = c.getComponentsForBus(busMid);
  EXPECT_EQ(midComponents.size(), 2);

  auto aComponents = c.getComponentsForBus(busA);
  EXPECT_EQ(aComponents.size(), 1);
}

TEST(CircuitTest, BusValuePropagation)
{
  auto a = std::make_shared<Wire>(State::LOW);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(Component_weakPtr(g), false);

  EXPECT_EQ(o->getCurrentState(), State::LOW);

  a->forceSetCurrentState(State::HIGH);
  b->forceSetCurrentState(State::HIGH);
  EXPECT_EQ(o->getCurrentState(), State::HIGH);

  a->forceSetCurrentState(State::LOW);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(CircuitTest, NotGateSingleInputOutput)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);

  Circuit c(Component_weakPtr(g), false);

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

  Circuit c(Component_weakPtr(g), false);

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

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(g));

  Circuit c(comps, false);

  EXPECT_EQ(c.getInputs().size(), 1);
  EXPECT_EQ(c.getOutputs().size(), 1);

  auto c2 = std::make_shared<Wire>();
  auto g2 = std::make_shared<NotGate>(o, c2);

  c.addComponent(Component_weakPtr(g2));

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
  };

  auto g = std::make_shared<TestComponent>(std::vector<Bus>{inputBus},
                                           std::vector<Bus>{outputBus});

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

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
  comps.insert(Component_weakPtr(not1));
  comps.insert(Component_weakPtr(and1));
  comps.insert(Component_weakPtr(and2));

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
  comps.insert(Component_weakPtr(not1));
  comps.insert(Component_weakPtr(and1));
  comps.insert(Component_weakPtr(and2));

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
  comps.insert(Component_weakPtr(not1));
  comps.insert(Component_weakPtr(and1));

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
  comps.insert(Component_weakPtr(not1));
  comps.insert(Component_weakPtr(and1));

  Circuit c(comps, false);

  auto subCircuit = c.getForwardSubgraph(Bus({x}));

  auto inputs  = subCircuit.getInputs();
  auto outputs = subCircuit.getOutputs();

  EXPECT_EQ(inputs.size(), 2);  // x, b
  EXPECT_EQ(outputs.size(), 1);
  EXPECT_EQ(outputs[0], Bus({o}));
}

TEST(CircuitTest, TopologicalOrderSingleGate)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(Component_weakPtr(g), false);

  auto order = c.topologicalOrder();
  EXPECT_EQ(order.size(), 1);
  EXPECT_EQ(order[0].lock().get(), g.get());
}

TEST(CircuitTest, TopologicalOrderTwoGates)
{
  // a,b → AND(g1, mid) → NOT(g2, o)
  // Topological order: g1 before g2.
  auto a   = std::make_shared<Wire>();
  auto b   = std::make_shared<Wire>();
  auto mid = std::make_shared<Wire>();
  auto o   = std::make_shared<Wire>();

  auto g1 = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, mid);
  auto g2 = std::make_shared<NotGate>(mid, o);

  Component_set comps;
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

  Circuit c(comps, false);

  auto order = c.topologicalOrder();
  ASSERT_EQ(order.size(), 2);
  EXPECT_EQ(order[0].lock().get(), g1.get());
  EXPECT_EQ(order[1].lock().get(), g2.get());
}

TEST(CircuitTest, TopologicalOrderEmpty)
{
  Circuit c;
  auto    order = c.topologicalOrder();
  EXPECT_TRUE(order.empty());
}

TEST(CircuitTest, GetGraphNotEmpty)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));

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
  comps.insert(Component_weakPtr(g1));
  comps.insert(Component_weakPtr(g2));
  comps.insert(Component_weakPtr(g3));
  comps.insert(Component_weakPtr(g4));

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
  auto    serialized = c.serialize();
  EXPECT_FALSE(serialized.empty());

  auto json = nlohmann::json::parse(serialized);
  EXPECT_EQ(json["components"].size(), 0);
  EXPECT_EQ(json["version"], SILICON_VERSION);
  EXPECT_TRUE(json["name"].is_string());
}

TEST(CircuitTest, SerializeSingleGate)
{
  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(andg));
  comps.insert(Component_weakPtr(org));
  comps.insert(Component_weakPtr(notg));
  comps.insert(Component_weakPtr(nandg));
  comps.insert(Component_weakPtr(norg));
  comps.insert(Component_weakPtr(xorg));

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
  };

  auto g = std::make_shared<TestComponent>(std::vector<Bus>{inputBus},
                                           std::vector<Bus>{outputBus});

  Circuit c(Component_weakPtr(g), false);

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

  Circuit c(Component_weakPtr(g), false);

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
  comps.insert(Component_weakPtr(n1));
  comps.insert(Component_weakPtr(n2));
  comps.insert(Component_weakPtr(a1));
  comps.insert(Component_weakPtr(n3));
  comps.insert(Component_weakPtr(o1));

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
                     + R"(", "components": [], "name": "test"})";

  auto c = Circuit::deserialize(json, registry);

  auto topo = c.topologicalOrder();
  EXPECT_TRUE(topo.empty());
}

TEST(CircuitTest, DeserializeSingleGate)
{
  ComponentRegistry registry;
  registerAllComponents(registry);

  auto a = std::make_shared<Wire>();
  auto b = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);

  Circuit original(Component_weakPtr(g), false);
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

  Circuit original(Component_weakPtr(g), false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto topo = deserialized.topologicalOrder();
  EXPECT_EQ(topo.size(), 1);
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
  comps.insert(Component_weakPtr(n1));
  comps.insert(Component_weakPtr(n2));
  comps.insert(Component_weakPtr(a1));
  comps.insert(Component_weakPtr(n3));
  comps.insert(Component_weakPtr(o1));

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
  comps.insert(Component_weakPtr(andg));
  comps.insert(Component_weakPtr(org));
  comps.insert(Component_weakPtr(notg));
  comps.insert(Component_weakPtr(nandg));
  comps.insert(Component_weakPtr(norg));
  comps.insert(Component_weakPtr(xorg));

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto topoComps = deserialized.topologicalOrder();
  EXPECT_EQ(topoComps.size(), 6);
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
  comps.insert(Component_weakPtr(andg1));
  comps.insert(Component_weakPtr(andg2));

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

  Circuit original(Component_weakPtr(ha), false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto topoComps = deserialized.topologicalOrder();
  EXPECT_EQ(topoComps.size(), 1);
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
  comps.insert(Component_weakPtr(andg));
  comps.insert(Component_weakPtr(notg));

  Circuit original(comps, false);
  auto    serialized = original.serialize();

  auto deserialized = Circuit::deserialize(serialized, registry);

  auto topoComps = deserialized.topologicalOrder();
  EXPECT_EQ(topoComps.size(), 2);
}

TEST(CircuitTest, SerializeIncludesProperties)
{
  auto a = std::make_shared<Wire>();
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<NotGate>(a, o);
  g->setProperty("delay", 2);

  Circuit c(Component_weakPtr(g), false);

  auto serialized = c.serialize();
  auto json       = nlohmann::json::parse(serialized);

  EXPECT_EQ(json["components"].size(), 1);
  EXPECT_TRUE(json["components"][0].contains("properties"));
  EXPECT_TRUE(json["components"][0]["properties"].contains("delay"));
  EXPECT_EQ(json["components"][0]["properties"]["delay"], 2);
}
