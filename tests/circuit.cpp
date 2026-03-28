/*
  Copyright (c) 2025. Giulio Cocconi

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

  auto g = std::make_shared<Component>(std::vector<Bus>{inputBus},
                                       std::vector<Bus>{outputBus}, "Test");

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
