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

#include <format>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/circuitDependencyGraph.hpp>
#include <core/projectContext.hpp>
#include <nlohmann/json.hpp>

using namespace SILICON::core;
using namespace SILICON::project;

namespace {

std::string sceneWithSubcircuits(std::vector<std::string> slugs)
{
  nlohmann::ordered_json scene;
  scene["circuit"] =
      nlohmann::ordered_json{{"version", SILICON_VERSION},
                             {"name", "Document"},
                             {"components", nlohmann::ordered_json::array()}};
  scene["visual"]["components"] = nlohmann::ordered_json::array();
  scene["visual"]["wires"]      = nlohmann::ordered_json::array();

  for (const auto& slug : slugs) {
    scene["circuit"]["components"].push_back(
        nlohmann::ordered_json{{"id", scene["circuit"]["components"].size()},
                               {"type", "Subcircuit"},
                               {"properties", {{"slug", slug}}},
                               {"inputs", nlohmann::ordered_json::array()},
                               {"outputs", nlohmann::ordered_json::array()}});
  }

  return scene.dump(2);
}

std::string emptyScene()
{
  return sceneWithSubcircuits({});
}

SILICON::project::Document circuit(std::string path, std::string sceneJson)
{
  return {std::move(path), std::move(sceneJson)};
}

SILICON::project::Document subcircuit(std::string slug, std::string sceneJson)
{
  return {std::format("subcircuits/{}.json", slug), std::move(sceneJson)};
}

void expectRuntimeErrorContaining(auto callback, const std::string& expected)
{
  try {
    callback();
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
        << "Actual message: " << e.what();
  }
}

}  // namespace

TEST(CircuitDependencyGraphTest, ExtractsEdgesFromCircuitsAndSubcircuits)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder"})),
       subcircuit("adder", sceneWithSubcircuits({"half_adder"})),
       subcircuit("half_adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("subcircuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
  EXPECT_EQ(graph.dependentsOf("subcircuits/half_adder.json"),
            std::vector<std::string>({"subcircuits/adder.json"}));
}

TEST(CircuitDependencyGraphTest, DuplicatePlacementsProduceOneDependency)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder", "adder"})),
       subcircuit("adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("subcircuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
}

TEST(CircuitDependencyGraphTest, RejectsMissingSubcircuitTarget)
{
  SILICON::project::CircuitDependencyGraph graph;

  EXPECT_THROW(
      graph.rebuildFromProject(
          {circuit("circuits/main.json", sceneWithSubcircuits({"missing"}))}),
      std::runtime_error);
}

TEST(CircuitDependencyGraphTest, DetectsDirectSelfCycle)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.addDocument("subcircuits/adder.json");

  EXPECT_TRUE(graph.wouldIntroduceCycle("subcircuits/adder.json",
                                        sceneWithSubcircuits({"adder"})));
}

TEST(CircuitDependencyGraphTest, RejectsDirectSelfCycleWithSlugTrace)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.addDocument("subcircuits/adder.json");

  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("subcircuits/adder.json",
                                          sceneWithSubcircuits({"adder"}));
      },
      "recursion trace: [adder, adder]");

  EXPECT_TRUE(graph.dependentsOf("subcircuits/adder.json").empty());
}

TEST(CircuitDependencyGraphTest, DetectsIndirectCycle)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                            subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                            subcircuit("alu", emptyScene())});

  EXPECT_TRUE(
      graph.wouldIntroduceCycle("subcircuits/alu.json", sceneWithSubcircuits({"cpu"})));
}

TEST(CircuitDependencyGraphTest, RejectsIndirectCycleWithSlugTrace)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                            subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                            subcircuit("alu", emptyScene())});

  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("subcircuits/alu.json",
                                          sceneWithSubcircuits({"cpu"}));
      },
      "recursion trace: [cpu, alu, cpu]");

  EXPECT_EQ(graph.dependentsOf("subcircuits/cpu.json"),
            std::vector<std::string>({"circuits/main.json"}));
}

TEST(CircuitDependencyGraphTest, RebuildRejectsCyclicProject)
{
  SILICON::project::CircuitDependencyGraph graph;

  EXPECT_THROW(graph.rebuildFromProject(
                   {circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                    subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                    subcircuit("alu", sceneWithSubcircuits({"cpu"}))}),
               std::runtime_error);
}

TEST(CircuitDependencyGraphTest, LooksUpDependentsForDeletionBlocking)
{
  SILICON::project::CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
       circuit("circuits/debug.json", sceneWithSubcircuits({"alu"})),
       subcircuit("alu", sceneWithSubcircuits({"adder"})),
       subcircuit("adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("subcircuits/alu.json"),
            (std::vector<std::string>{"circuits/debug.json", "circuits/main.json"}));
  EXPECT_EQ(graph.dependentsOf("subcircuits/adder.json"),
            std::vector<std::string>({"subcircuits/alu.json"}));
}

TEST(CircuitDependencyGraphTest, ReplacementRequiresRegisteredDocument)
{
  CircuitDependencyGraph graph;

  EXPECT_THROW(
      graph.replaceDocumentDependencies("circuits/missing.json", emptyScene()),
      std::runtime_error);
  EXPECT_FALSE(graph.containsDocument("circuits/missing.json"));
}

TEST(CircuitDependencyGraphTest, ReferencedDocumentRemovalIsRejectedAtomically)
{
  CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
       circuit("circuits/debug.json", sceneWithSubcircuits({"alu"})),
       subcircuit("alu", emptyScene())});

  try {
    graph.removeDocument("subcircuits/alu.json");
    FAIL() << "Expected referenced removal to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("circuits/debug.json"),
              std::string::npos);
    EXPECT_NE(std::string(error.what()).find("circuits/main.json"), std::string::npos);
  }

  EXPECT_TRUE(graph.containsDocument("subcircuits/alu.json"));
  EXPECT_EQ(graph.dependentsOf("subcircuits/alu.json"),
            (std::vector<std::string>{"circuits/debug.json", "circuits/main.json"}));

  graph.removeDocument("circuits/main.json");
  EXPECT_FALSE(graph.containsDocument("circuits/main.json"));
  EXPECT_EQ(graph.dependentsOf("subcircuits/alu.json"),
            std::vector<std::string>{"circuits/debug.json"});
}

TEST(CircuitDependencyGraphTest, RejectsMalformedSubcircuitComponentsWithContext)
{
  for (const std::string_view malformed : {
           R"({"components":[{"type":"Subcircuit"}]})",
           R"({"components":[{"type":"Subcircuit","properties":{}}]})",
           R"({"components":[{"type":"Subcircuit","properties":{"slug":123}}]})",
           R"({"components":[{"type":"Subcircuit","properties":{"slug":""}}]})",
           R"({"components":[{"type":"Subcircuit","properties":{"slug":"a/b"}}]})"}) {
    CircuitDependencyGraph graph;
    graph.addDocument("circuits/main.json");
    expectRuntimeErrorContaining(
        [&] { graph.replaceDocumentDependencies("circuits/main.json", malformed); },
        "circuits/main.json");
  }
}

TEST(CircuitDependencyGraphTest, RejectsMalformedComponentsField)
{
  CircuitDependencyGraph graph;
  graph.addDocument("circuits/main.json");
  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("circuits/main.json",
                                          R"({"components":"not-an-array"})");
      },
      "components must be an array");
}

TEST(CircuitDependencyGraphTest, SupportsBothEstablishedJsonRootForms)
{
  CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json",
               R"({"components":[{"type":"Subcircuit","properties":{"slug":"alu"}}]})"),
       subcircuit("alu", emptyScene())});
  EXPECT_EQ(graph.dependentsOf("subcircuits/alu.json"),
            std::vector<std::string>{"circuits/main.json"});
}

TEST(CircuitDependencyGraphTest, CyclePredicatePropagatesNonCycleFailures)
{
  CircuitDependencyGraph graph;
  graph.addDocument("circuits/main.json");

  EXPECT_FALSE(graph.wouldIntroduceCycle("circuits/main.json", emptyScene()));
  EXPECT_THROW(static_cast<void>(graph.wouldIntroduceCycle(
                   "circuits/main.json", sceneWithSubcircuits({"missing"}))),
               std::runtime_error);
  EXPECT_THROW(static_cast<void>(
                   graph.wouldIntroduceCycle("circuits/main.json", "not json")),
               std::runtime_error);
}

TEST(CircuitDependencyGraphTest, RecursiveErrorMessageIncludesTrace)
{
  CircuitDependencyGraph graph;
  graph.rebuildFromProject({subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                            subcircuit("alu", emptyScene())});
  try {
    graph.validateDocumentDependencies("subcircuits/alu.json",
                                       sceneWithSubcircuits({"cpu"}));
    FAIL() << "Expected recursion";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("[cpu, alu, cpu]"),
              std::string::npos);
  }
}

TEST(CircuitDependencyGraphTest, FailedFullRebuildPreservesPreviousGraph)
{
  CircuitDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
       subcircuit("alu", emptyScene())});

  EXPECT_THROW(
      graph.rebuildFromProject(
          {circuit("circuits/replacement.json", sceneWithSubcircuits({"missing"}))}),
      std::runtime_error);

  EXPECT_TRUE(graph.containsDocument("circuits/main.json"));
  EXPECT_TRUE(graph.containsDocument("subcircuits/alu.json"));
  EXPECT_FALSE(graph.containsDocument("circuits/replacement.json"));
  EXPECT_EQ(graph.dependentsOf("subcircuits/alu.json"),
            std::vector<std::string>{"circuits/main.json"});
}

TEST(CircuitDependencyGraphTest, CodeDocumentsAreExcludedFromRebuild)
{
  CircuitDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", emptyScene()),
                            Document("code/adder.v", "module adder; endmodule")});
  EXPECT_TRUE(graph.containsDocument("circuits/main.json"));
  EXPECT_FALSE(graph.containsDocument("code/adder.v"));
  EXPECT_THROW(graph.addDocument("code/adder.v"), std::invalid_argument);
}

TEST(CircuitDependencyGraphTest, RebuildRejectsDuplicateGraphicalDocuments)
{
  CircuitDependencyGraph graph;
  EXPECT_THROW(graph.rebuildFromProject(
                   {circuit("circuits/main.json", emptyScene()),
                    circuit("circuits/main.json", emptyScene())}),
               std::runtime_error);
  EXPECT_FALSE(graph.containsDocument("circuits/main.json"));
}

TEST(ProjectContextTest, FailedCircuitUpdateLeavesDocumentsAndDependenciesUnchanged)
{
  ProjectContext project;
  project.setDocuments(
      {circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
       subcircuit("alu", emptyScene())});

  EXPECT_THROW(project.upsertDocument(circuit(
                   "circuits/main.json", sceneWithSubcircuits({"missing"}))),
               std::runtime_error);

  ASSERT_NE(project.documents.find("circuits/main.json"), nullptr);
  EXPECT_EQ(project.documents.find("circuits/main.json")->getContents(),
            sceneWithSubcircuits({"alu"}));
  EXPECT_EQ(project.circuitDependencies.dependentsOf("subcircuits/alu.json"),
            std::vector<std::string>{"circuits/main.json"});
}

TEST(ProjectContextTest, ReferencedRemovalLeavesProjectStateUnchanged)
{
  ProjectContext project;
  project.setDocuments(
      {circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
       subcircuit("alu", emptyScene())});

  EXPECT_THROW(project.removeDocument("subcircuits/alu.json"), std::runtime_error);
  EXPECT_TRUE(project.documents.contains("subcircuits/alu.json"));
  EXPECT_TRUE(
      project.circuitDependencies.containsDocument("subcircuits/alu.json"));
}
