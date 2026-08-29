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
#include "subcircuitFixtures.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/circuit.hpp>
#include <core/elaboration.hpp>
#include <core/gates.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/simulationSession.hpp>
#include <core/simulator.hpp>
#include <core/subcircuit.hpp>
#include <core/subcircuitDefinition.hpp>
#include <core/projectDocument.hpp>

#include <nlohmann/json.hpp>

using namespace SILICON::core;
using namespace SILICON::project;
using namespace SILICON::simulation;
using namespace SILICON::waveform;

namespace {

std::string graphicalAndSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "graphical_and_subcircuit",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "DummyInputComponent",
          "properties": {"name": "a", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[1]]
        },
        {
          "id": 1,
          "type": "DummyInputComponent",
          "properties": {"name": "b", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[2]]
        },
        {
          "id": 2,
          "type": "AndGate",
          "properties": {"delay": 0, "bitwise": false, "size": 1},
          "inputs": [[1], [2]],
          "outputs": [[3]]
        },
        {
          "id": 3,
          "type": "DummyOutputComponent",
          "properties": {"name": "q", "portOrientation": "DOWN"},
          "inputs": [[3]],
          "outputs": []
        }
      ]
    },
    "visual": {
      "components": [
        {"type": "Input", "vertexId": 0, "position": {"x": 0, "y": 0}},
        {"type": "Input", "vertexId": 1, "position": {"x": 0, "y": 80}},
        {"type": "AndGate", "vertexId": 2, "position": {"x": 120, "y": 40}},
        {"type": "Output", "vertexId": 3, "position": {"x": 240, "y": 40}}
      ],
      "wires": []
    },
    "graphicalComponent": {
      "shape": {"type": "rectangle", "width": 8, "height": 8},
      "inputs": [],
      "outputs": []
    }
  })";
}

std::string graphicalBusSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "graphical_bus_subcircuit",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "DummyBusInputComponent",
          "properties": {"name": "data", "size": 8, "portOrientation": "DOWN", "startValue": "00000000"},
          "inputs": [],
          "outputs": [[1, 2, 3, 4, 5, 6, 7, 8]]
        },
        {
          "id": 1,
          "type": "NotGate",
          "properties": {"delay": 0, "bitwise": true, "size": 8},
          "inputs": [[1, 2, 3, 4, 5, 6, 7, 8]],
          "outputs": [[9, 10, 11, 12, 13, 14, 15, 16]]
        },
        {
          "id": 2,
          "type": "DummyBusOutputComponent",
          "properties": {"name": "result", "size": 8, "portOrientation": "DOWN"},
          "inputs": [[9, 10, 11, 12, 13, 14, 15, 16]],
          "outputs": []
        }
      ]
    },
    "visual": {"components": [], "wires": []},
    "graphicalComponent": {
      "shape": {"type": "rectangle", "width": 8, "height": 8},
      "inputs": [],
      "outputs": []
    }
  })";
}

std::string busNotCoreDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "bus_not_core",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "NotGate",
          "properties": {"delay": 0, "bitwise": true, "size": 8},
          "inputs": [[1, 2, 3, 4, 5, 6, 7, 8]],
          "outputs": [[9, 10, 11, 12, 13, 14, 15, 16]]
        }
      ]
    }
})";
}

std::string delayedNotSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "delayed_not",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "NotGate",
          "properties": {"delay": 2},
          "inputs": [[1]],
          "outputs": [[2]]
        }
      ]
    }
})";
}

std::string doubleNotSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "double_not",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "NotGate",
          "properties": {"delay": 0},
          "inputs": [[1]],
          "outputs": [[2]]
        },
        {
          "id": 1,
          "type": "NotGate",
          "properties": {"delay": 0},
          "inputs": [[2]],
          "outputs": [[3]]
        }
      ]
    }
  })";
}

std::string nestedAndSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "nested_and",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "Subcircuit",
          "properties": {"slug": "and_gate"},
          "inputs": [[1], [2]],
          "outputs": [[3]]
        }
      ]
    }
  })";
}

std::string recursiveSubcircuitDocument(std::string_view slug)
{
  return std::format(R"({{
    "circuit": {{
      "version": "0.1.0",
      "name": "recursive",
      "description": "",
      "components": [
        {{
          "id": 0,
          "type": "Subcircuit",
          "properties": {{"slug": "{}"}},
          "inputs": [[1]],
          "outputs": [[2]]
        }}
      ]
    }}
  }})",
                     slug);
}

std::string feedbackLatchCoreDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "feedback_latch",
      "description": "",
      "components": [
        {
          "id": 0,
          "type": "NorGate",
          "properties": {"delay": 0, "bitwise": false, "size": 1},
          "inputs": [[778], [776]],
          "outputs": [[777]]
        },
        {
          "id": 1,
          "type": "NorGate",
          "properties": {"delay": 0, "bitwise": false, "size": 1},
          "inputs": [[777], [779]],
          "outputs": [[776]]
        },
        {
          "id": 2,
          "type": "DummyInputComponent",
          "properties": {"name": "set", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[778]]
        },
        {
          "id": 3,
          "type": "DummyInputComponent",
          "properties": {"name": "reset", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[779]]
        },
        {
          "id": 4,
          "type": "DummyOutputComponent",
          "properties": {"name": "q", "portOrientation": "DOWN"},
          "inputs": [[777]],
          "outputs": []
        },
        {
          "id": 5,
          "type": "DummyOutputComponent",
          "properties": {"name": "not_q", "portOrientation": "DOWN"},
          "inputs": [[776]],
          "outputs": []
        }
      ]
    }
  })";
}

class SubcircuitTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    SILICON::project::DocumentStore::active().clear();
    if (!ComponentRegistry::instance().hasType(SubcircuitComponent::Type))
      registerAllComponents(ComponentRegistry::instance());
  }

  void TearDown() override { SILICON::project::DocumentStore::active().clear(); }
};

SILICON::project::Document subcircuitDocument(
    std::string slug, std::string sceneJson,
    std::optional<std::string> coreJson = std::nullopt)
{
  return {SILICON::project::subcircuitPathForSlug(slug), std::move(sceneJson),
          std::move(coreJson)};
}

}  // namespace

TEST_F(SubcircuitTest, DeserializesSubcircuitSlugProperty)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
    "description": "",
    "components": [
      {
        "id": 0,
        "type": "Subcircuit",
        "properties": {"slug": "and_gate"},
        "inputs": [[10], [11]],
        "outputs": [[12]]
      }
    ]
  })";

  auto circuit   = Circuit::deserialize(json, ComponentRegistry::instance());
  auto component = circuit.getComponentByVertexId(0);
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->typeName(), SubcircuitComponent::Type);
  EXPECT_EQ(component->getPropertyValue<std::string>("slug"), "and_gate");
  EXPECT_EQ(component->getInputs().size(), 2);
  EXPECT_EQ(component->getOutputs().size(), 1);
}

TEST_F(SubcircuitTest, DeserializesSubcircuitWithMissingSavedOutputs)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument(
          "graphical_and", graphicalAndSubcircuitDocument(),
          SILICON::core::extractCoreCircuitJson(andSubcircuitDocument())));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
    "description": "",
    "components": [
      {
        "id": 0,
        "type": "Subcircuit",
        "properties": {"slug": "graphical_and"},
        "inputs": [[10], [11]],
        "outputs": []
      }
    ]
  })";

  auto circuit   = Circuit::deserialize(json, ComponentRegistry::instance());
  auto component = circuit.getComponentByVertexId(0);

  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->getInputs().size(), 2);
  ASSERT_EQ(component->getOutputs().size(), 1);
  EXPECT_EQ(component->getOutputs()[0].size(), 1);

  const auto serialized = nlohmann::json::parse(circuit.serialize());
  ASSERT_EQ(serialized["components"].size(), 1);
  EXPECT_EQ(serialized["components"][0]["outputs"].size(), 1);
}

TEST_F(SubcircuitTest, SimulatesCombinationalSubcircuit)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("and_gate"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{inputA}, valueFor(Bus{inputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);

  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::LOW);
}

TEST_F(SubcircuitTest, ElaboratesPrimitiveCircuitWithoutHierarchy)
{
  auto input  = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  auto gate   = std::make_shared<NotGate>(input, output);
  Circuit source(gate, false);

  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());
  auto runtime = elaborator.elaborate(source);

  ASSERT_EQ(source.getComponentToVertex().size(), 1);
  ASSERT_EQ(runtime->getComponentToVertex().size(), 1);
  const auto runtimeComponent = runtime->getComponentToVertex().begin()->first;
  EXPECT_EQ(runtimeComponent->typeName(), NotGate::Type);
  EXPECT_NE(runtimeComponent, gate.get());
  EXPECT_EQ(runtimeComponent->getInputs()[0][0], input);
  EXPECT_EQ(runtimeComponent->getOutputs()[0][0], output);
}

TEST_F(SubcircuitTest, RuntimeElaborationLeavesSavedCircuitUnchanged)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
    "description": "",
    "components": [
      {
        "id": 0,
        "type": "Subcircuit",
        "properties": {"slug": "and_gate"},
        "inputs": [[10], [11]],
        "outputs": [[12]]
      }
    ]
  })";

  auto circuit    = Circuit::deserialize(json, ComponentRegistry::instance());
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());
  auto elaborated = elaborator.elaborate(circuit);

  const auto savedJson = nlohmann::json::parse(circuit.serialize());
  ASSERT_EQ(savedJson["components"].size(), 1);
  EXPECT_EQ(savedJson["components"][0]["type"], SubcircuitComponent::Type);

  const auto runtimeJson = nlohmann::json::parse(elaborated->serialize());
  ASSERT_EQ(runtimeJson["components"].size(), 1);
  EXPECT_EQ(runtimeJson["components"][0]["type"], AndGate::Type);
}

TEST_F(SubcircuitTest, RemapsTwoInstancesOfSameSubcircuitIndependently)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("not_gate", delayedNotSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
    "description": "",
    "components": [
      {
        "id": 0,
        "type": "Subcircuit",
        "properties": {"slug": "not_gate"},
        "inputs": [[10]],
        "outputs": [[11]]
      },
      {
        "id": 1,
        "type": "Subcircuit",
        "properties": {"slug": "not_gate"},
        "inputs": [[20]],
        "outputs": [[21]]
      }
    ]
  })";

  auto circuit = std::make_shared<Circuit>(
      Circuit::deserialize(json, ComponentRegistry::instance()));
  auto firstInput   = circuit->getComponentByVertexId(0)->getInputs()[0];
  auto firstOutput  = circuit->getComponentByVertexId(0)->getOutputs()[0][0];
  auto secondInput  = circuit->getComponentByVertexId(1)->getInputs()[0];
  auto secondOutput = circuit->getComponentByVertexId(1)->getOutputs()[0][0];

  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(simulator.setBus(firstInput, valueFor(firstInput, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(secondInput, valueFor(secondInput, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.runUntilIdle(), Simulator::RunResult::Completed);

  EXPECT_EQ(firstOutput->getCurrentState(), State::HIGH);
  EXPECT_EQ(secondOutput->getCurrentState(), State::LOW);
}

TEST_F(SubcircuitTest, ClonesInternalWiresForEverySubcircuitInstance)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("double_not", doubleNotSubcircuitDocument()));

  auto first  = std::make_shared<SubcircuitComponent>();
  auto second = std::make_shared<SubcircuitComponent>();
  first->setPropertyValue("slug", std::string("double_not"));
  second->setPropertyValue("slug", std::string("double_not"));

  auto firstInput   = std::make_shared<Wire>();
  auto firstOutput  = std::make_shared<Wire>();
  auto secondInput  = std::make_shared<Wire>();
  auto secondOutput = std::make_shared<Wire>();
  first->setInput(0, Bus{firstInput});
  first->setOutput(0, Bus{firstOutput});
  second->setInput(0, Bus{secondInput});
  second->setOutput(0, Bus{secondOutput});

  Circuit source(Component_set{first, second}, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());
  auto    runtime    = elaborator.elaborate(source);

  const std::set<const Wire*> interfaceWires{firstInput.get(), firstOutput.get(),
                                             secondInput.get(), secondOutput.get()};
  std::set<const Wire*>       internalWires;
  for (const auto& [component, _vertex] : runtime->getComponentToVertex()) {
    for (const auto& bus : component->inputBuses()) {
      for (const auto& wire : bus) {
        if (wire && !interfaceWires.contains(wire.get()))
          internalWires.insert(wire.get());
      }
    }
    for (const auto& bus : component->outputBuses()) {
      for (const auto& wire : bus) {
        if (wire && !interfaceWires.contains(wire.get()))
          internalWires.insert(wire.get());
      }
    }
  }

  EXPECT_EQ(internalWires.size(), 2);
}

TEST_F(SubcircuitTest, ElaboratesNestedSubcircuitsIntoParentSimulation)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("nested_and", nestedAndSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("nested_and"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{inputA}, valueFor(Bus{inputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, DelayedGateInsideSubcircuitUsesParentEventQueue)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("delayed_not", delayedNotSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("delayed_not"));

  auto input  = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{input});
  component->setOutput(0, Bus{output});

  auto circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{input}, valueFor(Bus{input}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_NE(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.run(1), Simulator::RunResult::Completed);
  EXPECT_NE(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.run(1), Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.getCurrentTime(), 2);
}

TEST_F(SubcircuitTest, RejectsModuleInputBusCountMismatch)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("and_gate"));
  std::vector<Bus> mismatchedInputs{Bus{std::make_shared<Wire>()}};
  component->setInputs(mismatchedInputs);

  Circuit source(component, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());

  try {
    static_cast<void>(elaborator.elaborate(source));
    FAIL() << "Expected an interface bus-count mismatch";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(
        std::string(error.what()).find("subcircuit:and_gate' input bus count mismatch"),
        std::string::npos);
  }
}

TEST_F(SubcircuitTest, RejectsModuleBusWidthMismatch)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("bus_not", busNotCoreDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("bus_not"));
  component->setInput(0, Bus(4));

  Circuit source(component, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());

  try {
    static_cast<void>(elaborator.elaborate(source));
    FAIL() << "Expected an interface bus-width mismatch";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(
        std::string(error.what()).find("subcircuit:bus_not' input bus 0 width mismatch"),
        std::string::npos);
  }
}

TEST_F(SubcircuitTest, DirectSimulatorRejectsUnprocessedPlaceholder)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("and_gate"));
  auto source = std::make_shared<Circuit>(component, false);

  try {
    Simulator simulator(source);
    FAIL() << "Expected direct placeholder simulation to fail";
  } catch (const std::logic_error& error) {
    EXPECT_NE(std::string(error.what()).find("preprocess the circuit"),
              std::string::npos);
  }

  // A constructor failure must not leave a topology callback referring to the
  // partially constructed Simulator.
  source->notifyTopologyListeners();
}

TEST_F(SubcircuitTest, ExplicitSessionRebuildResetsRuntimeAndRestoresTrace)
{
  auto input  = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  auto gate   = std::make_shared<NotGate>(input, output);
  gate->setPropertyValue("delay", 5);

  auto                                   source = std::make_shared<Circuit>(gate, false);
  SILICON::simulation::Session session(source);

  std::vector<std::pair<std::uint64_t, std::vector<BusValue>>> snapshots;
  session.setTraceBuses({{"output", Bus{output}}});
  session.setTraceSink(
      [&snapshots](const std::uint64_t time, const std::vector<BusValue>& values) {
        snapshots.emplace_back(time, values);
      });

  EXPECT_EQ(session.setBus(Bus{input}, valueFor(Bus{input}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(session.run(2), Simulator::RunResult::Completed);
  EXPECT_EQ(session.getCurrentTime(), 2);
  EXPECT_NE(output->getCurrentState(), State::HIGH);

  // The old runtime has a HIGH transition queued for t=5. Replace the source topology
  // with an identity AND gate; rebuilding must discard the old runtime and its event.
  input->forceSetCurrentState(State::HIGH);
  source->removeComponent(gate);
  auto replacementGate =
      std::make_shared<AndGate>(std::vector<Wire_ptr>{input, input}, output);
  replacementGate->setPropertyValue("delay", 0);
  source->addComponent(replacementGate);
  session.rebuild();

  EXPECT_EQ(session.getCurrentTime(), 0);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
  ASSERT_FALSE(snapshots.empty());
  EXPECT_EQ(snapshots.back().first, 0);
  ASSERT_EQ(snapshots.back().second.size(), 1);
  EXPECT_EQ(snapshots.back().second[0], busValueFromBits("1"));

  EXPECT_EQ(session.run(6), Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, FailedSessionRebuildKeepsPreviousRuntimeUsable)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("and_gate"));
  auto source = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session session(source);

  EXPECT_EQ(session.run(1), Simulator::RunResult::Completed);
  std::vector<Bus> mismatchedInputs{Bus{std::make_shared<Wire>()}};
  component->setInputs(mismatchedInputs);
  EXPECT_THROW(session.rebuild(), std::runtime_error);

  EXPECT_EQ(session.getCurrentTime(), 1);
  EXPECT_EQ(session.run(1), Simulator::RunResult::Completed);
  EXPECT_EQ(session.getCurrentTime(), 2);
}

TEST_F(SubcircuitTest, RejectsRecursiveSubcircuitDependenciesDuringElaboration)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("a", recursiveSubcircuitDocument("b")));
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("b", recursiveSubcircuitDocument("a")));

  auto component = std::make_shared<SubcircuitComponent>();
  try {
    component->setPropertyValue("slug", std::string("a"));
    FAIL() << "Expected recursive subcircuits to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what())
                  .find("Recursive subcircuit dependency detected: a -> b -> a"),
              std::string::npos);
  }
}

TEST_F(SubcircuitTest, ExtractCoreCircuitJsonUnwrapsCircuitObject)
{
  const auto coreJson =
      SILICON::core::extractCoreCircuitJson(andSubcircuitDocument());

  auto circuit = Circuit::deserialize(coreJson, ComponentRegistry::instance());

  ASSERT_EQ(circuit.getInputs().size(), 2);
  ASSERT_EQ(circuit.getOutputs().size(), 1);
  EXPECT_EQ(circuit.getInputs()[0].size(), 1);
  EXPECT_EQ(circuit.getInputs()[1].size(), 1);
  EXPECT_EQ(circuit.getOutputs()[0].size(), 1);
  ASSERT_NE(circuit.getComponentByVertexId(0), nullptr);
  EXPECT_EQ(circuit.getComponentByVertexId(0)->typeName(), AndGate::Type);
}

TEST_F(SubcircuitTest, UsesPreparedCoreCircuitJsonForGraphicalSubcircuitDocument)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument(
          "graphical_bus", graphicalBusSubcircuitDocument(),
          SILICON::core::extractCoreCircuitJson(busNotCoreDocument())));

  const auto definition = SILICON::core::loadSubcircuitDefinition(
      "graphical_bus", ComponentRegistry::instance());
  ASSERT_EQ(definition.inputs.size(), 1);
  ASSERT_EQ(definition.outputs.size(), 1);
  EXPECT_EQ(definition.inputs[0].name, "data");
  EXPECT_EQ(definition.outputs[0].name, "result");

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("graphical_bus"));

  ASSERT_EQ(component->getInputs().size(), 1);
  ASSERT_EQ(component->getOutputs().size(), 1);
  EXPECT_EQ(component->getInputs()[0].size(), 8);
  EXPECT_EQ(component->getOutputs()[0].size(), 8);
}

TEST_F(SubcircuitTest, ElaboratesMultipleGraphicalSubcircuits)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument(
          "graphical_and", graphicalAndSubcircuitDocument(),
          SILICON::core::extractCoreCircuitJson(andSubcircuitDocument())));
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument(
          "graphical_not", delayedNotSubcircuitDocument(),
          SILICON::core::extractCoreCircuitJson(delayedNotSubcircuitDocument())));

  auto graphical = std::make_shared<SubcircuitComponent>();
  auto inverter  = std::make_shared<SubcircuitComponent>();
  graphical->setPropertyValue("slug", std::string("graphical_and"));
  inverter->setPropertyValue("slug", std::string("graphical_not"));

  auto graphicalInputA = std::make_shared<Wire>();
  auto graphicalInputB = std::make_shared<Wire>();
  auto graphicalOutput = std::make_shared<Wire>();
  auto inverterInput    = std::make_shared<Wire>();
  auto inverterOutput   = std::make_shared<Wire>();
  graphical->setInput(0, Bus{graphicalInputA});
  graphical->setInput(1, Bus{graphicalInputB});
  graphical->setOutput(0, Bus{graphicalOutput});
  inverter->setInput(0, Bus{inverterInput});
  inverter->setOutput(0, Bus{inverterOutput});

  auto source = std::make_shared<Circuit>(Component_set{graphical, inverter}, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance());
  auto runtime = elaborator.elaborate(*source);

  std::size_t andCount = 0;
  std::size_t notCount = 0;
  for (const auto& [component, _vertex] : runtime->getComponentToVertex()) {
    EXPECT_NE(component->typeName(), SubcircuitComponent::Type);
    andCount += component->typeName() == AndGate::Type;
    notCount += component->typeName() == NotGate::Type;
  }
  EXPECT_EQ(andCount, 1);
  EXPECT_EQ(notCount, 1);

  SILICON::simulation::Session session(source);
  EXPECT_EQ(session.setBus(Bus{graphicalInputA}, valueFor(Bus{graphicalInputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(session.setBus(Bus{graphicalInputB}, valueFor(Bus{graphicalInputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(session.setBus(Bus{inverterInput}, valueFor(Bus{inverterInput}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(session.runUntilIdle(), Simulator::RunResult::Completed);
  EXPECT_EQ(graphicalOutput->getCurrentState(), State::HIGH);
  EXPECT_EQ(inverterOutput->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, CircuitUsesPortRoleDeclaredInterface)
{
  const auto circuit = Circuit::deserialize(
      SILICON::core::extractCoreCircuitJson(graphicalAndSubcircuitDocument()),
      ComponentRegistry::instance());

  const auto inputs  = circuit.getInputPorts();
  const auto outputs = circuit.getOutputPorts();
  ASSERT_EQ(inputs.size(), 2);
  ASSERT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0].name, "a");
  EXPECT_EQ(inputs[1].name, "b");
  EXPECT_EQ(outputs[0].name, "q");
}

TEST_F(SubcircuitTest, CircuitUsesDeterministicTopologyFallbackPortNames)
{
  const auto circuit = Circuit::deserialize(
      SILICON::core::extractCoreCircuitJson(andSubcircuitDocument()),
      ComponentRegistry::instance());
  const auto inputs  = circuit.getInputPorts();
  const auto outputs = circuit.getOutputPorts();
  ASSERT_EQ(inputs.size(), 2);
  ASSERT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0].name, "input_0");
  EXPECT_EQ(inputs[1].name, "input_1");
  EXPECT_EQ(outputs[0].name, "output_0");
}

TEST_F(SubcircuitTest, CircuitPreservesDuplicateAndEmptyBoundaryNames)
{
  auto document = nlohmann::json::parse(graphicalAndSubcircuitDocument());
  document["circuit"]["components"][0]["properties"]["name"] = "data";
  document["circuit"]["components"][1]["properties"]["name"] = "data";
  document["circuit"]["components"][3]["properties"]["name"] = "";
  const auto circuit = Circuit::deserialize(document["circuit"].dump(),
                                             ComponentRegistry::instance());

  const auto inputs  = circuit.getInputPorts();
  const auto outputs = circuit.getOutputPorts();
  ASSERT_EQ(inputs.size(), 2);
  ASSERT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0].name, "data");
  EXPECT_EQ(inputs[1].name, "data");
  EXPECT_TRUE(outputs[0].name.empty());
}

TEST_F(SubcircuitTest, UsesBoundaryComponentsForFeedbackOutputs)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("feedback_latch", feedbackLatchCoreDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("feedback_latch"));

  EXPECT_EQ(component->getInputs().size(), 2);
  ASSERT_EQ(component->getOutputs().size(), 2);
  EXPECT_EQ(component->getOutputs()[0].size(), 1);
  EXPECT_EQ(component->getOutputs()[1].size(), 1);
}

TEST_F(SubcircuitTest, SimulatesFeedbackSubcircuitWithBoundaryOutputs)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument("feedback_latch", feedbackLatchCoreDocument()));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("feedback_latch"));

  auto set   = std::make_shared<Wire>(State::LOW);
  auto reset = std::make_shared<Wire>(State::HIGH);
  auto q     = std::make_shared<Wire>(State::UNKNOWN);
  auto notQ  = std::make_shared<Wire>(State::UNKNOWN);
  component->setInput(0, Bus{set});
  component->setInput(1, Bus{reset});
  component->setOutput(0, Bus{q});
  component->setOutput(1, Bus{notQ});

  auto circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(q->getCurrentState(), State::HIGH);
  EXPECT_EQ(notQ->getCurrentState(), State::LOW);

  EXPECT_EQ(simulator.setBus(Bus{reset}, valueFor(Bus{reset}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{set}, valueFor(Bus{set}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(q->getCurrentState(), State::LOW);
  EXPECT_EQ(notQ->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, SimulatesGraphicalSubcircuitDocument)
{
  SILICON::project::DocumentStore::active().upsertDocument(
      subcircuitDocument(
          "graphical_and", graphicalAndSubcircuitDocument(),
          SILICON::core::extractCoreCircuitJson(andSubcircuitDocument())));

  auto component = std::make_shared<SubcircuitComponent>();
  component->setPropertyValue("slug", std::string("graphical_and"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit);

  EXPECT_EQ(simulator.setBus(Bus{inputA}, valueFor(Bus{inputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, RejectsUnknownSlug)
{
  auto component = std::make_shared<SubcircuitComponent>();
  try {
    component->setPropertyValue("slug", std::string("missing"));
    FAIL() << "Expected an unknown slug to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("Unknown subcircuit slug 'missing'"),
              std::string::npos);
  }
}

TEST_F(SubcircuitTest, DocumentStoreNotifiesSpecificAndGlobalChanges)
{
  auto&                    registry = SILICON::project::DocumentStore::active();
  std::vector<SILICON::project::DocumentChange> notifications;
  const auto listenerId = registry.addListener(
      [&notifications](const SILICON::project::DocumentChange& change) {
        notifications.push_back(change);
      });

  registry.upsertDocument(subcircuitDocument("adder", andSubcircuitDocument()));
  registry.removeDocument("subcircuits/adder.json");
  registry.setDocuments({});
  registry.removeListener(listenerId);
  registry.clear();

  ASSERT_EQ(notifications.size(), 3);
  EXPECT_EQ(notifications[0].kind, SILICON::project::DocumentChangeKind::Added);
  EXPECT_EQ(notifications[0].path, "subcircuits/adder.json");
  EXPECT_EQ(notifications[1].kind, SILICON::project::DocumentChangeKind::Removed);
  EXPECT_EQ(notifications[1].path, "subcircuits/adder.json");
  EXPECT_EQ(notifications[2].kind, SILICON::project::DocumentChangeKind::Reset);
  EXPECT_FALSE(notifications[2].path);
}
