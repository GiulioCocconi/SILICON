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
#include <core/simulator.hpp>

TEST(LogicTest, And)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);  // Process initial zero-delay and gate evaluations

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, ERROR) = " << to_str(o->getCurrentState());

  // Using simulateBus to evaluate the upstream cone for non-standard states
  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "AND(ERROR, LOW) = " << to_str(o->getCurrentState());

  // Using setBus to reactively propagate inputs through the forward subgraph
  sim.setBus(Bus{a}, 0);  // LOW
  sim.setBus(Bus{b}, 0);  // LOW
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "AND(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);  // HIGH
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "AND(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);  // HIGH
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "AND(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(LogicTest, Or)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<OrGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, ERROR) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "OR(ERROR, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 0);
  sim.setBus(Bus{b}, 0);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "OR(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "OR(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "OR(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(LogicTest, Xor)
{
  auto a = std::make_shared<Wire>(State::ERROR);
  auto b = std::make_shared<Wire>(State::ERROR);
  auto o = std::make_shared<Wire>();

  auto g = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{g});
  Simulator sim(circ);
  sim.run(20);

  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, ERROR) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::HIGH);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, HIGH) = " << to_str(o->getCurrentState());

  b->forceSetCurrentState(State::LOW);
  sim.simulateBus(Bus{o});
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::ERROR)
      << "XOR(ERROR, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 0);
  sim.setBus(Bus{b}, 0);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "XOR(LOW, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{a}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::HIGH)
      << "XOR(HIGH, LOW) = " << to_str(o->getCurrentState());

  sim.setBus(Bus{b}, 1);
  sim.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW)
      << "XOR(HIGH, HIGH) = " << to_str(o->getCurrentState());
}

TEST(LogicTest, CircuitEditing1)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::LOW);
  auto o = std::make_shared<Wire>();

  {
    auto g = std::make_shared<XorGate>(std::array<Wire_ptr, 2>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::HIGH);
  }

  {
    auto g = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::LOW);
  }

  {
    b->forceSetCurrentState(State::HIGH);
    auto g = std::make_shared<OrGate>(std::vector<Wire_ptr>{a, b}, o);
    auto      c = std::make_shared<Circuit>(Component_set{g});
    Simulator sim(c);
    sim.run(20);
    EXPECT_EQ(o->getCurrentState(), State::HIGH);
  }
}

TEST(LogicTest, CircuitEditing2)
{
  auto a = std::make_shared<Wire>(State::HIGH);
  auto b = std::make_shared<Wire>(State::HIGH);
  auto c = std::make_shared<Wire>(State::HIGH);

  auto o = std::make_shared<Wire>();

  auto ag = std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, o);
  auto      circ = std::make_shared<Circuit>(Component_set{ag});
  Simulator sim(circ);
  sim.run(20);

  std::vector<Bus> newInputs = {{c}, {b}};
  ag->setInputs(newInputs);

  // Dynamic topology alters require instantiating a fresh graph configuration
  auto      circ2 = std::make_shared<Circuit>(Component_set{ag});
  Simulator sim2(circ2);
  sim2.run(20);

  a->forceSetCurrentState(State::ERROR);  // Should not influence the gate anymore

  EXPECT_EQ(o->getCurrentState(), State::HIGH);

  sim2.setBus(Bus{c}, 0);
  sim2.run(20);
  EXPECT_EQ(o->getCurrentState(), State::LOW);
}

TEST(LogicTest, BusSettingReading)
{
  auto a = Bus(4);
  for (int i = 0; i <= 0b1111; i++) {
    a.forceSetCurrentValue(i);
    EXPECT_EQ(a.getCurrentValue(), i);
  }
}

// --- Component Property Tests --------------------------------------------------------

TEST(ComponentTest, SetAndGetIntProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("value");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<int>(*prop), 10);

  c->setProperty("value", 20);
  prop = c->getProperty("value");
  EXPECT_EQ(std::get<int>(*prop), 20);
}

TEST(ComponentTest, SetAndGetIntPropertyTemplated)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getPropertyValue<int>("value");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(*prop, 10);

  c->setPropertyValue<int>("value", 20);
  prop = c->getPropertyValue<int>("value");
  EXPECT_EQ(*prop, 20);
}

TEST(ComponentTest, SetAndGetBoolProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("enabled", false); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("enabled");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<bool>(*prop), false);

  c->setProperty("enabled", true);
  prop = c->getProperty("enabled");
  EXPECT_EQ(std::get<bool>(*prop), true);
}

TEST(ComponentTest, SetAndGetStringProperty)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {})
    {
      defineProperty("name", std::string("default"));
    }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("name");
  ASSERT_TRUE(prop.has_value());
  EXPECT_EQ(std::get<std::string>(*prop), "default");

  c->setProperty("name", std::string("custom"));
  prop = c->getProperty("name");
  EXPECT_EQ(std::get<std::string>(*prop), "custom");
}

TEST(ComponentTest, GetPropertyNotFound)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  auto prop = c->getProperty("nonexistent");
  EXPECT_FALSE(prop.has_value());
}

TEST(ComponentTest, SetPropertyTypeMismatch)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) { defineProperty("value", 10); }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  EXPECT_THROW(c->setProperty("value", std::string("wrong type")), std::invalid_argument);
}

TEST(ComponentTest, SetPropertyUndefined)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {}) {}
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c = std::make_shared<TestComponent>();

  EXPECT_THROW(c->setProperty("undefined", 10), std::invalid_argument);
}

TEST(ComponentTest, GetProperties)
{
  struct TestComponent : public Component {
    TestComponent() : Component({}, {})
    {
      defineProperty("intVal", 5);
      defineProperty("boolVal", true);
      defineProperty("strVal", std::string("hello"));
    }
    std::string_view typeName() const override { return "TestComponent"; }
    void             simulate(Simulator& sim) override {}
  };

  auto c     = std::make_shared<TestComponent>();
  auto props = c->getProperties();

  EXPECT_EQ(props.size(), 3);
  EXPECT_EQ(std::get<int>(props.at("intVal")), 5);
  EXPECT_EQ(std::get<bool>(props.at("boolVal")), true);
  EXPECT_EQ(std::get<std::string>(props.at("strVal")), "hello");
}
