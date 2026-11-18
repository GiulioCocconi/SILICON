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

#include "subcircuitFixtures.hpp"
#include "tests.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/circuit.hpp>
#include <core/circuitDocument.hpp>
#include <core/elaboration.hpp>
#include <core/gates.hpp>
#include <core/io.hpp>
#include <core/memory.hpp>
#include <core/projectCircuitResolver.hpp>
#include <core/projectContext.hpp>
#include <core/projectDocument.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>
#include <core/serialization/yosys/netlist.hpp>
#include <core/simulationSession.hpp>
#include <core/simulator.hpp>
#include <core/subcircuit.hpp>

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

std::string romSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "rom_leaf",
      "components": [
        {
          "id": 0,
          "type": "ROM",
          "properties": {"dataWidth": 8, "binaryContents": "program"},
          "inputs": [[1, 2], [3], [4]],
          "outputs": [[5, 6, 7, 8, 9, 10, 11, 12]]
        }
      ]
    }
  })";
}

std::string nestedRomSubcircuitDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "rom_parent",
      "components": [
        {
          "id": 0,
          "type": "Subcircuit",
          "properties": {"slug": "rom_leaf"},
          "inputs": [],
          "outputs": []
        }
      ]
    }
  })";
}

std::string romSimulationLeafDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "rom_sim_leaf",
      "components": [
        {
          "id": 0,
          "type": "DummyBusInputComponent",
          "properties": {"name": "address", "size": 2, "portOrientation": "DOWN", "startValue": "00"},
          "inputs": [],
          "outputs": [[1, 2]]
        },
        {
          "id": 1,
          "type": "DummyInputComponent",
          "properties": {"name": "cs", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[3]]
        },
        {
          "id": 2,
          "type": "DummyInputComponent",
          "properties": {"name": "oe", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[4]]
        },
        {
          "id": 3,
          "type": "ROM",
          "properties": {"dataWidth": 8, "binaryContents": "program"},
          "inputs": [[1, 2], [3], [4]],
          "outputs": [[5, 6, 7, 8, 9, 10, 11, 12]]
        },
        {
          "id": 4,
          "type": "DummyBusOutputComponent",
          "properties": {"name": "data", "size": 8, "portOrientation": "DOWN"},
          "inputs": [[5, 6, 7, 8, 9, 10, 11, 12]],
          "outputs": []
        }
      ]
    }
  })";
}

std::string romSimulationParentDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "rom_sim_parent",
      "components": [
        {
          "id": 0,
          "type": "DummyBusInputComponent",
          "properties": {"name": "address", "size": 2, "portOrientation": "DOWN", "startValue": "00"},
          "inputs": [],
          "outputs": [[1, 2]]
        },
        {
          "id": 1,
          "type": "DummyInputComponent",
          "properties": {"name": "cs", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[3]]
        },
        {
          "id": 2,
          "type": "DummyInputComponent",
          "properties": {"name": "oe", "portOrientation": "DOWN", "startValue": "0"},
          "inputs": [],
          "outputs": [[4]]
        },
        {
          "id": 3,
          "type": "Subcircuit",
          "properties": {"slug": "rom_sim_leaf"},
          "inputs": [[1, 2], [3], [4]],
          "outputs": [[5, 6, 7, 8, 9, 10, 11, 12]]
        },
        {
          "id": 4,
          "type": "DummyBusOutputComponent",
          "properties": {"name": "data", "size": 8, "portOrientation": "DOWN"},
          "inputs": [[5, 6, 7, 8, 9, 10, 11, 12]],
          "outputs": []
        }
      ]
    }
  })";
}

class RecursiveCircuitResolver final : public CircuitResolver {
public:
  [[nodiscard]] SubcircuitDefinition resolve(const std::string_view slug) const override
  {
    const auto nestedSlug = slug == "a" ? "b" : "a";
    auto       nested     = SubcircuitComponent::imported(nestedSlug, {}, {}, {}, {});
    return {Circuit(nested, false), {}, {}};
  }
};

std::string feedbackLatchCoreDocument()
{
  return R"({
    "circuit": {
      "version": "0.1.0",
      "name": "feedback_latch",
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
    registerAllComponents(registry);
    if (!ComponentRegistry::instance().hasType(SubcircuitComponent::Type))
      registerAllComponents(ComponentRegistry::instance());
  }

  ComponentRegistry                        registry = ComponentRegistry::empty();
  SILICON::project::ProjectContext         project;
  SILICON::project::ProjectCircuitResolver resolver{project, registry};
};

SILICON::project::Document subcircuitDocument(std::string slug, std::string sceneJson)
{
  return {SILICON::project::documentPathForSlug(SILICON::project::DocumentType::Circuit,
                                                slug),
          std::move(sceneJson)};
}

struct ProjectFileCleanup {
  std::filesystem::path path;
  ~ProjectFileCleanup() { std::filesystem::remove(path); }
};

std::filesystem::path temporaryProjectPath()
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path()
         / std::format("silicon_subcircuit_resolver_{}.sil", stamp);
}

}  // namespace

TEST_F(SubcircuitTest, DeserializesSubcircuitSlugProperty)
{
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
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

  auto circuit   = Circuit::deserialize(json, ComponentRegistry::instance(), &resolver);
  auto component = circuit.getComponentByVertexId(0);
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->typeName(), SubcircuitComponent::Type);
  EXPECT_EQ(component->getPropertyValue<std::string>("slug"), "and_gate");
  EXPECT_EQ(component->getInputs().size(), 2);
  EXPECT_EQ(component->getOutputs().size(), 1);
}

TEST_F(SubcircuitTest, DeserializesSubcircuitWithMissingSavedOutputs)
{
  project.upsertDocument(
      subcircuitDocument("graphical_and", graphicalAndSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
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

  auto circuit   = Circuit::deserialize(json, ComponentRegistry::instance(), &resolver);
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
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("and_gate"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

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
  auto    input  = std::make_shared<Wire>();
  auto    output = std::make_shared<Wire>();
  auto    gate   = std::make_shared<NotGate>(input, output);
  Circuit source(gate, false);

  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance(),
                                                    &resolver);
  auto                                   runtime = elaborator.elaborate(source);

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
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
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

  auto circuit = Circuit::deserialize(json, ComponentRegistry::instance(), &resolver);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance(),
                                                    &resolver);
  auto                                   elaborated = elaborator.elaborate(circuit);

  const auto savedJson = nlohmann::json::parse(circuit.serialize());
  ASSERT_EQ(savedJson["components"].size(), 1);
  EXPECT_EQ(savedJson["components"][0]["type"], SubcircuitComponent::Type);

  const auto runtimeJson = nlohmann::json::parse(elaborated->serialize());
  ASSERT_EQ(runtimeJson["components"].size(), 1);
  EXPECT_EQ(runtimeJson["components"][0]["type"], AndGate::Type);
}

TEST_F(SubcircuitTest, RemapsTwoInstancesOfSameSubcircuitIndependently)
{
  project.upsertDocument(subcircuitDocument("not_gate", delayedNotSubcircuitDocument()));

  const auto json = R"({
    "version": "0.1.0",
    "name": "main",
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
      Circuit::deserialize(json, ComponentRegistry::instance(), &resolver));
  auto firstInput   = circuit->getComponentByVertexId(0)->getInputs()[0];
  auto firstOutput  = circuit->getComponentByVertexId(0)->getOutputs()[0][0];
  auto secondInput  = circuit->getComponentByVertexId(1)->getInputs()[0];
  auto secondOutput = circuit->getComponentByVertexId(1)->getOutputs()[0][0];

  SILICON::simulation::Session simulator(circuit, {}, &resolver);

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
  project.upsertDocument(subcircuitDocument("double_not", doubleNotSubcircuitDocument()));

  auto first  = std::make_shared<SubcircuitComponent>(&resolver);
  auto second = std::make_shared<SubcircuitComponent>(&resolver);
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

  Circuit                                source(Component_set{first, second}, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance(),
                                                    &resolver);
  auto                                   runtime = elaborator.elaborate(source);

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
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));
  project.upsertDocument(subcircuitDocument("nested_and", nestedAndSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("nested_and"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  EXPECT_EQ(simulator.setBus(Bus{inputA}, valueFor(Bus{inputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, DelayedGateInsideSubcircuitUsesParentEventQueue)
{
  project.upsertDocument(
      subcircuitDocument("delayed_not", delayedNotSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("delayed_not"));

  auto input  = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{input});
  component->setOutput(0, Bus{output});

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  EXPECT_EQ(simulator.setBus(Bus{input}, valueFor(Bus{input}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_NE(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.run(1), Simulator::RunResult::Completed);
  EXPECT_NE(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.run(1), Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
  EXPECT_EQ(simulator.getCurrentTime(), 2);
}

TEST_F(SubcircuitTest, ReconcilesModuleConnectionsWithoutPersistentIdentities)
{
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));
  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("and_gate"));
  const Bus connected(1);
  component->setInputs({connected});
  const auto definition = resolver.resolve("and_gate");
  const auto inputs     = component->inputsForPorts(definition.inputs);
  ASSERT_EQ(inputs.size(), 2);
  EXPECT_EQ(inputs[0], connected);
  EXPECT_EQ(inputs[1].size(), 1);
  EXPECT_NE(inputs[1][0], connected[0]);
}

TEST_F(SubcircuitTest, ReconcilesBusWidthsWithoutPersistentIdentities)
{
  project.upsertDocument(subcircuitDocument("bus_not", busNotCoreDocument()));
  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("bus_not"));
  const Bus connected(4);
  component->setInput(0, connected);
  const auto definition = resolver.resolve("bus_not");
  const auto inputs     = component->inputsForPorts(definition.inputs);
  ASSERT_EQ(inputs.size(), 1);
  ASSERT_EQ(inputs[0].size(), definition.inputs[0].bus.size());
  for (std::size_t bit = 0; bit < connected.size(); ++bit)
    EXPECT_EQ(inputs[0][bit], connected[bit]);
}

TEST_F(SubcircuitTest, DirectSimulatorRejectsUnprocessedPlaceholder)
{
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("and_gate"));
  auto source = std::make_shared<Circuit>(component, false);

  try {
    Simulator simulator(source);
    FAIL() << "Expected direct placeholder simulation to fail";
  } catch (const std::logic_error& error) {
    EXPECT_NE(std::string(error.what()).find("preprocess the circuit"),
              std::string::npos);
  }
}

TEST_F(SubcircuitTest, ReplacementSessionResetsRuntimeAndSupportsTrace)
{
  auto input  = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  auto gate   = std::make_shared<NotGate>(input, output);
  gate->setPropertyValue("delay", 5);

  auto source  = std::make_shared<Circuit>(gate, false);
  auto session = std::make_unique<SILICON::simulation::Session>(
      source, Simulator::CancellationCheck{}, &resolver);

  std::vector<std::pair<std::uint64_t, std::vector<BusValue>>> snapshots;
  const auto traceSink = [&snapshots](const std::uint64_t          time,
                                      const std::vector<BusValue>& values) {
    snapshots.emplace_back(time, values);
  };
  session->setTraceBuses({{"output", Bus{output}}});
  session->setTraceSink(traceSink);

  EXPECT_EQ(session->setBus(Bus{input}, valueFor(Bus{input}, 0)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(session->run(2), Simulator::RunResult::Completed);
  EXPECT_EQ(session->getCurrentTime(), 2);
  EXPECT_NE(output->getCurrentState(), State::HIGH);

  // The old runtime has a HIGH transition queued for t=5. Replace the source topology
  // with an identity AND gate and create a fresh session without the pending event.
  session.reset();
  input->forceSetCurrentState(State::HIGH);
  source->removeComponent(gate);
  auto replacementGate =
      std::make_shared<AndGate>(std::vector<Wire_ptr>{input, input}, output);
  replacementGate->setPropertyValue("delay", 0);
  source->addComponent(replacementGate);
  session = std::make_unique<SILICON::simulation::Session>(
      source, Simulator::CancellationCheck{}, &resolver);
  session->setTraceBuses({{"output", Bus{output}}});
  session->setTraceSink(traceSink);

  EXPECT_EQ(session->getCurrentTime(), 0);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
  ASSERT_FALSE(snapshots.empty());
  EXPECT_EQ(snapshots.back().first, 0);
  ASSERT_EQ(snapshots.back().second.size(), 1);
  EXPECT_EQ(snapshots.back().second[0], busValueFromBits("1"));

  EXPECT_EQ(session->run(6), Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, FailedReplacementSessionKeepsPreviousRuntimeUsable)
{
  project.upsertDocument(subcircuitDocument("and_gate", andSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("and_gate"));
  auto                         source = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session session(source, {}, &resolver);

  EXPECT_EQ(session.run(1), Simulator::RunResult::Completed);
  // A replacement without the required project resolver must not disturb the runtime.
  EXPECT_THROW(SILICON::simulation::Session replacement(source), std::runtime_error);

  EXPECT_EQ(session.getCurrentTime(), 1);
  EXPECT_EQ(session.run(1), Simulator::RunResult::Completed);
  EXPECT_EQ(session.getCurrentTime(), 2);
}

TEST_F(SubcircuitTest, RejectsRecursiveSubcircuitDependenciesDuringElaboration)
{
  RecursiveCircuitResolver recursiveResolver;
  auto                     component = SubcircuitComponent::imported("a", {}, {}, {}, {});
  Circuit                  source(component, false);
  SILICON::simulation::CircuitElaborator elaborator(registry, &recursiveResolver);

  try {
    static_cast<void>(elaborator.elaborate(source));
    FAIL() << "Expected recursive subcircuits to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what())
                  .find("Recursive subcircuit dependency detected: a -> b -> a"),
              std::string::npos);
  }
}

TEST_F(SubcircuitTest, ParsesCircuitDocumentWithoutGuiObjects)
{
  auto circuit = SILICON::core::parseCircuitDocument(
                     andSubcircuitDocument(), ComponentRegistry::instance(), &resolver)
                     .circuit;

  ASSERT_EQ(circuit.getInputs().size(), 2);
  ASSERT_EQ(circuit.getOutputs().size(), 1);
  EXPECT_EQ(circuit.getInputs()[0].size(), 1);
  EXPECT_EQ(circuit.getInputs()[1].size(), 1);
  EXPECT_EQ(circuit.getOutputs()[0].size(), 1);
  ASSERT_NE(circuit.getComponentByVertexId(0), nullptr);
  EXPECT_EQ(circuit.getComponentByVertexId(0)->typeName(), AndGate::Type);
}

TEST_F(SubcircuitTest, ResolvesGraphicalSubcircuitFromPersistedContents)
{
  project.upsertDocument(
      subcircuitDocument("graphical_bus", graphicalBusSubcircuitDocument()));

  const auto definition = resolver.resolve("graphical_bus");
  ASSERT_EQ(definition.inputs.size(), 1);
  ASSERT_EQ(definition.outputs.size(), 1);
  EXPECT_EQ(definition.inputs[0].name, "data");
  EXPECT_EQ(definition.outputs[0].name, "result");

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("graphical_bus"));

  ASSERT_EQ(component->getInputs().size(), 1);
  ASSERT_EQ(component->getOutputs().size(), 1);
  EXPECT_EQ(component->getInputs()[0].size(), 8);
  EXPECT_EQ(component->getOutputs()[0].size(), 8);
}

TEST_F(SubcircuitTest, ResolvesCircuitReadDirectlyFromProjectArchive)
{
  const auto         path = temporaryProjectPath();
  ProjectFileCleanup cleanup{path};
  const auto         timestamp = currentUtcTimestamp();
  writeProjectFile(
      path, {.metadata  = {.formatVersion  = FORMAT_VERSION,
                           .siliconVersion = SILICON_VERSION,
                           .creationDate   = timestamp,
                           .lastModify     = timestamp},
             .project   = {.name = "resolver test", .description = ""},
             .documents = {
                 {std::string(DEFAULT_CIRCUIT_PATH), graphicalAndSubcircuitDocument()},
                 subcircuitDocument("persisted", graphicalBusSubcircuitDocument())}});

  ProjectContext loadedProject;
  loadedProject.setDocuments(readProjectFile(path).documents);
  ProjectCircuitResolver loadedResolver{loadedProject, registry};

  const auto definition = loadedResolver.resolve("persisted");
  ASSERT_EQ(definition.inputs.size(), 1);
  ASSERT_EQ(definition.outputs.size(), 1);
  EXPECT_EQ(definition.inputs[0].name, "data");
  EXPECT_EQ(definition.outputs[0].name, "result");
}

TEST_F(SubcircuitTest, HydratesRomSnapshotsInNestedResolvedDefinitions)
{
  project.upsertDocument({documentPathForSlug(DocumentType::RawBinary, "program"),
                          std::string("\x10\x20\x30\x40", 4)});
  project.upsertDocument(subcircuitDocument("rom_leaf", romSubcircuitDocument()));
  project.upsertDocument(subcircuitDocument("rom_parent", nestedRomSubcircuitDocument()));

  auto root = std::make_shared<SubcircuitComponent>(&resolver);
  root->setPropertyValue("slug", std::string("rom_parent"));
  Circuit source(root, false);

  CircuitElaborator elaborator(registry, &resolver);
  auto              runtime = elaborator.elaborate(source);

  ASSERT_EQ(runtime->getComponentToVertex().size(), 1);
  const auto rom = std::dynamic_pointer_cast<ROM>(runtime->getComponentByVertexId(0));
  ASSERT_NE(rom, nullptr);
  ASSERT_NE(rom->binaryContentsSnapshot(), nullptr);
  EXPECT_EQ(*rom->binaryContentsSnapshot(), std::string("\x10\x20\x30\x40", 4));
  EXPECT_EQ(rom->inputBuses()[0].size(), 2);
}

TEST_F(SubcircuitTest, SimulatesRomInsideSingleSubcircuit)
{
  project.upsertDocument({documentPathForSlug(DocumentType::RawBinary, "program"),
                          std::string("\x10\x20\x30\x40", 4)});
  project.upsertDocument(subcircuitDocument("rom_sim_leaf", romSimulationLeafDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("rom_sim_leaf"));

  Bus  address(2);
  auto cs = std::make_shared<Wire>();
  auto oe = std::make_shared<Wire>();
  Bus  data(8);
  component->setInput(0, address);
  component->setInput(1, Bus{cs});
  component->setInput(2, Bus{oe});
  component->setOutput(0, data);

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  ASSERT_EQ(simulator.setBus(Bus{cs}, valueFor(Bus{cs}, 1)),
            Simulator::RunResult::Completed);
  const std::array<std::uint8_t, 4> expected{0x10, 0x20, 0x30, 0x40};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    ASSERT_EQ(simulator.setBus(address, valueFor(address, index)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, expected[index])) << index;
  }
}

TEST_F(SubcircuitTest, SimulatesRomInsideNestedSubcircuits)
{
  project.upsertDocument({documentPathForSlug(DocumentType::RawBinary, "program"),
                          std::string("\x10\x20\x30\x40", 4)});
  project.upsertDocument(subcircuitDocument("rom_sim_leaf", romSimulationLeafDocument()));
  project.upsertDocument(
      subcircuitDocument("rom_sim_parent", romSimulationParentDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("rom_sim_parent"));

  Bus  address(2);
  auto cs = std::make_shared<Wire>();
  auto oe = std::make_shared<Wire>();
  Bus  data(8);
  component->setInput(0, address);
  component->setInput(1, Bus{cs});
  component->setInput(2, Bus{oe});
  component->setOutput(0, data);

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  ASSERT_EQ(simulator.setBus(Bus{cs}, valueFor(Bus{cs}, 1)),
            Simulator::RunResult::Completed);
  const std::array<std::uint8_t, 4> expected{0x10, 0x20, 0x30, 0x40};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    ASSERT_EQ(simulator.setBus(address, valueFor(address, index)),
              Simulator::RunResult::Completed);
    EXPECT_EQ(data.getCurrentValue(), valueFor(data, expected[index])) << index;
  }
}

TEST_F(SubcircuitTest, ElaboratedRomRuntimeStaysStableAfterSourceRefresh)
{
  project.upsertDocument({documentPathForSlug(DocumentType::RawBinary, "program"),
                          std::string("\x10\x20\x30\x40", 4)});
  project.upsertDocument(subcircuitDocument("rom_sim_leaf", romSimulationLeafDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("rom_sim_leaf"));

  Bus  address(2);
  auto cs = std::make_shared<Wire>();
  auto oe = std::make_shared<Wire>();
  Bus  data(8);
  component->setInput(0, address);
  component->setInput(1, Bus{cs});
  component->setInput(2, Bus{oe});
  component->setOutput(0, data);

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  ASSERT_EQ(simulator.setBus(Bus{cs}, valueFor(Bus{cs}, 1)),
            Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x20));

  // Replacing the project binary document and re-resolving the definition must not
  // disturb the already elaborated runtime clone.
  project.upsertDocument({documentPathForSlug(DocumentType::RawBinary, "program"),
                          std::string("\xDE\xAD\xBE\xEF", 4)});
  static_cast<void>(resolver.resolve("rom_sim_leaf"));

  ASSERT_EQ(simulator.setBus(address, valueFor(address, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(data.getCurrentValue(), valueFor(data, 0x20));
}

TEST_F(SubcircuitTest, ProjectResolversWithSameSlugRemainIndependent)
{
  ProjectContext firstProject;
  ProjectContext secondProject;
  firstProject.setDocuments(
      {subcircuitDocument("shared", graphicalAndSubcircuitDocument())});
  secondProject.setDocuments(
      {subcircuitDocument("shared", graphicalBusSubcircuitDocument())});
  ProjectCircuitResolver firstResolver{firstProject, registry};
  ProjectCircuitResolver secondResolver{secondProject, registry};

  const auto first  = firstResolver.resolve("shared");
  const auto second = secondResolver.resolve("shared");
  EXPECT_EQ(first.inputs.size(), 2);
  ASSERT_EQ(second.inputs.size(), 1);
  EXPECT_EQ(second.inputs[0].bus.size(), 8);
}

TEST_F(SubcircuitTest, ElaboratesMultipleGraphicalSubcircuits)
{
  project.upsertDocument(
      subcircuitDocument("graphical_and", graphicalAndSubcircuitDocument()));
  project.upsertDocument(
      subcircuitDocument("graphical_not", delayedNotSubcircuitDocument()));

  auto graphical = std::make_shared<SubcircuitComponent>(&resolver);
  auto inverter  = std::make_shared<SubcircuitComponent>(&resolver);
  graphical->setPropertyValue("slug", std::string("graphical_and"));
  inverter->setPropertyValue("slug", std::string("graphical_not"));

  auto graphicalInputA = std::make_shared<Wire>();
  auto graphicalInputB = std::make_shared<Wire>();
  auto graphicalOutput = std::make_shared<Wire>();
  auto inverterInput   = std::make_shared<Wire>();
  auto inverterOutput  = std::make_shared<Wire>();
  graphical->setInput(0, Bus{graphicalInputA});
  graphical->setInput(1, Bus{graphicalInputB});
  graphical->setOutput(0, Bus{graphicalOutput});
  inverter->setInput(0, Bus{inverterInput});
  inverter->setOutput(0, Bus{inverterOutput});

  auto source = std::make_shared<Circuit>(Component_set{graphical, inverter}, false);
  SILICON::simulation::CircuitElaborator elaborator(ComponentRegistry::instance(),
                                                    &resolver);
  auto                                   runtime = elaborator.elaborate(*source);

  std::size_t andCount = 0;
  std::size_t notCount = 0;
  for (const auto& [component, _vertex] : runtime->getComponentToVertex()) {
    EXPECT_NE(component->typeName(), SubcircuitComponent::Type);
    andCount += component->typeName() == AndGate::Type;
    notCount += component->typeName() == NotGate::Type;
  }
  EXPECT_EQ(andCount, 1);
  EXPECT_EQ(notCount, 1);

  SILICON::simulation::Session session(source, {}, &resolver);
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
  const auto definition = SILICON::core::parseCircuitDocument(
      graphicalAndSubcircuitDocument(), ComponentRegistry::instance(), &resolver);
  const auto& inputs  = definition.inputs;
  const auto& outputs = definition.outputs;
  ASSERT_EQ(inputs.size(), 2);
  ASSERT_EQ(outputs.size(), 1);
  EXPECT_EQ(inputs[0].name, "a");
  EXPECT_EQ(inputs[1].name, "b");
  EXPECT_EQ(outputs[0].name, "q");
  EXPECT_EQ(inputs[0].boundaryVertex, 0);
  EXPECT_EQ(inputs[1].boundaryVertex, 1);
  EXPECT_EQ(outputs[0].boundaryVertex, 3);
}

TEST_F(SubcircuitTest, CircuitUsesDeterministicTopologyFallbackPortNames)
{
  const auto definition = SILICON::core::parseCircuitDocument(
      andSubcircuitDocument(), ComponentRegistry::instance(), &resolver);
  const auto& inputs  = definition.inputs;
  const auto& outputs = definition.outputs;
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
  const auto circuit =
      Circuit::deserialize(document["circuit"].dump(), ComponentRegistry::instance());

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
  project.upsertDocument(
      subcircuitDocument("feedback_latch", feedbackLatchCoreDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("feedback_latch"));

  EXPECT_EQ(component->getInputs().size(), 2);
  ASSERT_EQ(component->getOutputs().size(), 2);
  EXPECT_EQ(component->getOutputs()[0].size(), 1);
  EXPECT_EQ(component->getOutputs()[1].size(), 1);
}

TEST_F(SubcircuitTest, SimulatesFeedbackSubcircuitWithBoundaryOutputs)
{
  project.upsertDocument(
      subcircuitDocument("feedback_latch", feedbackLatchCoreDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("feedback_latch"));

  auto set   = std::make_shared<Wire>(State::LOW);
  auto reset = std::make_shared<Wire>(State::HIGH);
  auto q     = std::make_shared<Wire>(State::UNKNOWN);
  auto notQ  = std::make_shared<Wire>(State::UNKNOWN);
  component->setInput(0, Bus{set});
  component->setInput(1, Bus{reset});
  component->setOutput(0, Bus{q});
  component->setOutput(1, Bus{notQ});

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

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
  project.upsertDocument(
      subcircuitDocument("graphical_and", graphicalAndSubcircuitDocument()));

  auto component = std::make_shared<SubcircuitComponent>(&resolver);
  component->setPropertyValue("slug", std::string("graphical_and"));

  auto inputA = std::make_shared<Wire>();
  auto inputB = std::make_shared<Wire>();
  auto output = std::make_shared<Wire>();
  component->setInput(0, Bus{inputA});
  component->setInput(1, Bus{inputB});
  component->setOutput(0, Bus{output});

  auto                         circuit = std::make_shared<Circuit>(component, false);
  SILICON::simulation::Session simulator(circuit, {}, &resolver);

  EXPECT_EQ(simulator.setBus(Bus{inputA}, valueFor(Bus{inputA}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(simulator.setBus(Bus{inputB}, valueFor(Bus{inputB}, 1)),
            Simulator::RunResult::Completed);
  EXPECT_EQ(output->getCurrentState(), State::HIGH);
}

TEST_F(SubcircuitTest, RejectsUnknownSlug)
{
  auto component = std::make_shared<SubcircuitComponent>(&resolver);
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
  const auto&                                   store = project.documents();
  std::vector<SILICON::project::DocumentChange> notifications;
  const auto                                    listenerId =
      store.addListener([&notifications](const SILICON::project::DocumentChange& change) {
        notifications.push_back(change);
      });

  project.upsertDocument(subcircuitDocument("adder", andSubcircuitDocument()));
  project.removeDocument("circuits/adder.json");
  project.setDocuments({});
  store.removeListener(listenerId);

  ASSERT_EQ(notifications.size(), 3);
  EXPECT_EQ(notifications[0].kind, SILICON::project::DocumentChangeKind::Added);
  EXPECT_EQ(notifications[0].path, "circuits/adder.json");
  EXPECT_EQ(notifications[1].kind, SILICON::project::DocumentChangeKind::Removed);
  EXPECT_EQ(notifications[1].path, "circuits/adder.json");
  EXPECT_EQ(notifications[2].kind, SILICON::project::DocumentChangeKind::Reset);
  EXPECT_FALSE(notifications[2].path);
}

namespace {

nlohmann::json identityTestDocument()
{
  auto  document   = nlohmann::json::parse(graphicalAndSubcircuitDocument())["circuit"];
  auto& components = document["components"];
  components[2]["type"]   = "NotGate";
  components[2]["inputs"] = {{1}};
  components.push_back({{"id", 4},
                        {"type", "DummyOutputComponent"},
                        {"properties", {{"name", "pass_b"}}},
                        {"inputs", {{5}}},
                        {"outputs", nlohmann::json::array()}});
  // Use real drivers on both outputs so neither boundary aliases an input wire.
  for (int id = 5; id <= 6; ++id)
    components.push_back({{"id", id},
                          {"type", "NotGate"},
                          {"properties", {{"delay", 0}, {"size", 1}}},
                          {"inputs", {{id == 5 ? 2 : 4}}},
                          {"outputs", {{id == 5 ? 4 : 5}}}});
  return document;
}

nlohmann::json reorderedIdentityDocument(nlohmann::json document)
{
  for (auto& component : document["components"]) {
    const auto id = component["id"].get<int>();
    if (id == 0 || id == 1)
      component["id"] = 1 - id;
    if (id == 3 || id == 4)
      component["id"] = 7 - id;
    // Names, including duplicate names, must not act as identities.
    if (component["properties"].contains("name"))
      component["properties"]["name"] = "renamed";
  }
  return document;
}

}  // namespace

TEST_F(SubcircuitTest, ReloadPreservesConnectionsWhenPortsReorderAndRename)
{
  const auto definition = Circuit::deserialize(identityTestDocument().dump(), registry);
  const auto savedDefinition = nlohmann::json::parse(definition.serialize());
  project.upsertDocument(subcircuitDocument("identity", savedDefinition.dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  const auto a         = instance->getInputs()[0];
  const auto b         = instance->getInputs()[1];
  const auto invertedA = instance->getOutputs()[0];
  const auto passB     = instance->getOutputs()[1];
  const auto inputIds  = instance->inputPortIds();
  const auto outputIds = instance->outputPortIds();

  project.upsertDocument(
      subcircuitDocument("identity", reorderedIdentityDocument(savedDefinition).dump()));
  instance->reloadFromResolver();
  EXPECT_EQ(instance->getInputs()[0], b);
  EXPECT_EQ(instance->getInputs()[1], a);
  EXPECT_EQ(instance->getOutputs()[0], passB);
  EXPECT_EQ(instance->getOutputs()[1], invertedA);
  EXPECT_EQ(instance->inputPortIds()[1], inputIds[0]);
  EXPECT_EQ(instance->outputPortIds()[1], outputIds[0]);

  auto    circuit = std::make_shared<Circuit>(instance, false);
  Session simulator(circuit, {}, &resolver);
  ASSERT_EQ(simulator.setBus(a, valueFor(a, 1)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(b, valueFor(b, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(invertedA.getCurrentValue(), valueFor(invertedA, 0));
  EXPECT_EQ(passB.getCurrentValue(), valueFor(passB, 1));
}

TEST_F(SubcircuitTest, SavedAndUnrefreshedInstancesRemapToChangedDefinition)
{
  const auto definition = Circuit::deserialize(identityTestDocument().dump(), registry);
  const auto savedDefinition = nlohmann::json::parse(definition.serialize());
  project.upsertDocument(subcircuitDocument("identity", savedDefinition.dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  auto       circuit       = std::make_shared<Circuit>(instance, false);
  const auto savedInstance = circuit->serialize();
  const auto a             = instance->getInputs()[0];
  const auto b             = instance->getInputs()[1];
  const auto invertedA     = instance->getOutputs()[0];
  const auto passB         = instance->getOutputs()[1];

  project.upsertDocument(
      subcircuitDocument("identity", reorderedIdentityDocument(savedDefinition).dump()));
  // Headless callers do not receive GUI document-change notifications.
  Session simulator(circuit, {}, &resolver);
  ASSERT_EQ(simulator.setBus(a, valueFor(a, 1)), Simulator::RunResult::Completed);
  ASSERT_EQ(simulator.setBus(b, valueFor(b, 1)), Simulator::RunResult::Completed);
  EXPECT_EQ(invertedA.getCurrentValue(), valueFor(invertedA, 0));
  EXPECT_EQ(passB.getCurrentValue(), valueFor(passB, 1));

  auto       restored         = Circuit::deserialize(savedInstance, registry, &resolver);
  const auto restoredInstance = restored.getComponentByVertexId(0);
  EXPECT_EQ(restoredInstance->getInputs()[1][0]->getId(), a[0]->getId());
  EXPECT_EQ(restoredInstance->getInputs()[0][0]->getId(), b[0]->getId());
  EXPECT_EQ(restoredInstance->getOutputs()[1][0]->getId(), invertedA[0]->getId());
  EXPECT_EQ(restoredInstance->getOutputs()[0][0]->getId(), passB[0]->getId());
  const auto savedAgain = nlohmann::json::parse(restored.serialize())["components"][0];
  EXPECT_EQ(savedAgain["inputPortIds"][0], instance->inputPortIds()[1]);
  EXPECT_EQ(savedAgain["outputPortIds"][0], instance->outputPortIds()[1]);
}

TEST_F(SubcircuitTest, ReloadKeepsResizedPortAndDoesNotReuseRemovedConnection)
{
  auto document   = nlohmann::json::parse(graphicalBusSubcircuitDocument())["circuit"];
  auto definition = Circuit::deserialize(document.dump(), registry);
  document        = nlohmann::json::parse(definition.serialize());
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  const auto original = instance->getInputs()[0];

  document["components"][0]["properties"]["size"] = 16;
  for (int wire = 20; wire < 28; ++wire)
    document["components"][0]["outputs"][0].push_back(wire);
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  instance->reloadFromResolver();
  ASSERT_EQ(instance->getInputs()[0].size(), 16);
  for (std::size_t bit = 0; bit < original.size(); ++bit)
    EXPECT_EQ(instance->getInputs()[0][bit], original[bit]);

  document["components"][0]["portId"] = 1000000;
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  instance->reloadFromResolver();
  EXPECT_NE(instance->getInputs()[0][0], original[0]);
}

TEST_F(SubcircuitTest, RejectsAmbiguousSavedPortIdentities)
{
  project.upsertDocument(subcircuitDocument("identity", identityTestDocument().dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  Circuit circuit(instance, false);
  auto    document                          = nlohmann::json::parse(circuit.serialize());
  document["components"][0]["inputPortIds"] = {1, 1};
  EXPECT_THROW(
      static_cast<void>(Circuit::deserialize(document.dump(), registry, &resolver)),
      std::runtime_error);
}

TEST_F(SubcircuitTest, YosysExportPreservesConnectionsAfterDefinitionReorder)
{
  auto definition      = Circuit::deserialize(identityTestDocument().dump(), registry);
  auto savedDefinition = nlohmann::json::parse(definition.serialize());
  project.upsertDocument(subcircuitDocument("identity", savedDefinition.dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  auto       a = std::make_shared<DummyInputComponent>(instance->getInputs()[0], "a");
  auto       b = std::make_shared<DummyInputComponent>(instance->getInputs()[1], "b");
  auto       y = std::make_shared<DummyOutputComponent>(instance->getOutputs()[0], "y");
  auto       z = std::make_shared<DummyOutputComponent>(instance->getOutputs()[1], "z");
  Circuit    root(std::vector<Component_ptr>{a, b, instance, y, z}, false);
  const auto before = nlohmann::json::parse(SILICON::yosys::serialize(root, "top"));
  for (auto& component : savedDefinition["components"]) {
    const auto id = component["id"].get<int>();
    if (id == 0 || id == 1)
      component["id"] = 1 - id;
    if (id == 3 || id == 4)
      component["id"] = 7 - id;
  }
  project.upsertDocument(subcircuitDocument("identity", savedDefinition.dump()));
  const auto after = nlohmann::json::parse(SILICON::yosys::serialize(root, "top"));
  for (const auto* design : {&before, &after}) {
    const auto& top         = (*design)["modules"]["top"];
    const auto& connections = top["cells"].begin().value()["connections"];
    EXPECT_EQ(connections["a"], top["ports"]["a"]["bits"]);
    EXPECT_EQ(connections["b"], top["ports"]["b"]["bits"]);
    EXPECT_EQ(connections["q"], top["ports"]["y"]["bits"]);
    EXPECT_EQ(connections["pass_b"], top["ports"]["z"]["bits"]);
  }
}

TEST_F(SubcircuitTest, UnrefreshedInstancesReconcileWidthsWithUnchangedIds)
{
  auto document   = nlohmann::json::parse(graphicalBusSubcircuitDocument())["circuit"];
  auto definition = Circuit::deserialize(document.dump(), registry);
  document        = nlohmann::json::parse(definition.serialize());
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  const auto originalInput  = instance->getInputs()[0];
  const auto originalOutput = instance->getOutputs()[0];
  const auto inputIds       = instance->inputPortIds();
  const auto outputIds      = instance->outputPortIds();

  for (auto& component : document["components"]) {
    component["properties"]["size"] = 16;
    for (const auto* direction : {"inputs", "outputs"}) {
      for (auto& bus : component[direction]) {
        const int start = bus[0] == 1 ? 17 : 25;
        for (int i = 0; i < 8; ++i)
          bus.push_back(start + i);
      }
    }
  }
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  const auto updated = resolver.resolve("identity");
  const auto inputs  = instance->inputsForPorts(updated.inputs);
  const auto outputs = instance->outputsForPorts(updated.outputs);
  EXPECT_EQ(instance->inputPortIds(), inputIds);
  EXPECT_EQ(instance->outputPortIds(), outputIds);
  ASSERT_EQ(inputs[0].size(), 16);
  ASSERT_EQ(outputs[0].size(), 16);
  for (std::size_t bit = 0; bit < 8; ++bit) {
    EXPECT_EQ(inputs[0][bit], originalInput[bit]);
    EXPECT_EQ(outputs[0][bit], originalOutput[bit]);
  }
  Circuit     root(instance, false);
  const auto  exported = nlohmann::json::parse(SILICON::yosys::serialize(root, "top"));
  const auto& connections =
      exported["modules"]["top"]["cells"].begin().value()["connections"];
  EXPECT_EQ(connections["data"].size(), 16);
  EXPECT_EQ(connections["result"].size(), 16);

  for (auto& component : document["components"]) {
    component["properties"]["size"] = 4;
    for (const auto* direction : {"inputs", "outputs"})
      for (auto& bus : component[direction])
        bus.erase(bus.begin() + 4, bus.end());
  }
  project.upsertDocument(subcircuitDocument("identity", document.dump()));
  const auto reduced        = resolver.resolve("identity");
  const auto reducedInputs  = instance->inputsForPorts(reduced.inputs);
  const auto reducedOutputs = instance->outputsForPorts(reduced.outputs);
  ASSERT_EQ(reducedInputs[0].size(), 4);
  ASSERT_EQ(reducedOutputs[0].size(), 4);
  for (std::size_t bit = 0; bit < 4; ++bit) {
    EXPECT_EQ(reducedInputs[0][bit], originalInput[bit]);
    EXPECT_EQ(reducedOutputs[0][bit], originalOutput[bit]);
  }
}

TEST_F(SubcircuitTest, RemovingBoundariesDoesNotRetargetNamedConnections)
{
  project.upsertDocument(
      subcircuitDocument("identity", graphicalAndSubcircuitDocument()));
  auto instance = std::make_shared<SubcircuitComponent>(&resolver);
  instance->setPropertyValue("slug", std::string("identity"));
  const auto oldInputs = instance->getInputs();
  const auto oldOutput = instance->getOutputs()[0];
  project.upsertDocument(subcircuitDocument("identity", andSubcircuitDocument()));
  const auto definition = resolver.resolve("identity");
  const auto inputs     = instance->inputsForPorts(definition.inputs);
  const auto outputs    = instance->outputsForPorts(definition.outputs);
  ASSERT_EQ(inputs.size(), 2);
  for (const auto& input : inputs)
    for (const auto& previous : oldInputs)
      EXPECT_NE(input[0], previous[0]);
  ASSERT_EQ(outputs.size(), 1);
  EXPECT_NE(outputs[0][0], oldOutput[0]);
}
