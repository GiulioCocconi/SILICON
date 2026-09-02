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

#include <core/projectDependencyGraph.hpp>
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

SILICON::project::Document reusableCircuit(std::string slug, std::string sceneJson)
{
  return {std::format("circuits/{}.json", slug), std::move(sceneJson)};
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

TEST(ProjectDependencyGraphTest, ExtractsEdgesBetweenCircuits)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder"})),
       reusableCircuit("adder", sceneWithSubcircuits({"half_adder"})),
       reusableCircuit("half_adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("circuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
  EXPECT_EQ(graph.dependentsOf("circuits/half_adder.json"),
            std::vector<std::string>({"circuits/adder.json"}));
}

TEST(ProjectDependencyGraphTest, DuplicatePlacementsProduceOneDependency)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder", "adder"})),
       reusableCircuit("adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("circuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
}

TEST(ProjectDependencyGraphTest, RejectsMissingSubcircuitTarget)
{
  SILICON::project::ProjectDependencyGraph graph;

  EXPECT_THROW(graph.rebuildFromProject(
                   {circuit("circuits/main.json", sceneWithSubcircuits({"missing"}))}),
               std::runtime_error);
}

TEST(ProjectDependencyGraphTest, DetectsDirectSelfCycle)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.addDocument("circuits/adder.json");

  EXPECT_TRUE(graph.wouldIntroduceCycle("circuits/adder.json",
                                        sceneWithSubcircuits({"adder"})));
}

TEST(ProjectDependencyGraphTest, RejectsDirectSelfCycleWithSlugTrace)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.addDocument("circuits/adder.json");

  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("circuits/adder.json",
                                          sceneWithSubcircuits({"adder"}));
      },
      "recursion trace: [adder, adder]");

  EXPECT_TRUE(graph.dependentsOf("circuits/adder.json").empty());
}

TEST(ProjectDependencyGraphTest, DetectsIndirectCycle)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                            reusableCircuit("cpu", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", emptyScene())});

  EXPECT_TRUE(
      graph.wouldIntroduceCycle("circuits/alu.json", sceneWithSubcircuits({"cpu"})));
}

TEST(ProjectDependencyGraphTest, RejectsIndirectCycleWithSlugTrace)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                            reusableCircuit("cpu", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", emptyScene())});

  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("circuits/alu.json",
                                          sceneWithSubcircuits({"cpu"}));
      },
      "recursion trace: [cpu, alu, cpu]");

  EXPECT_EQ(graph.dependentsOf("circuits/cpu.json"),
            std::vector<std::string>({"circuits/main.json"}));
}

TEST(ProjectDependencyGraphTest, RebuildRejectsCyclicProject)
{
  SILICON::project::ProjectDependencyGraph graph;

  EXPECT_THROW(graph.rebuildFromProject(
                   {circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                    reusableCircuit("cpu", sceneWithSubcircuits({"alu"})),
                    reusableCircuit("alu", sceneWithSubcircuits({"cpu"}))}),
               std::runtime_error);
}

TEST(ProjectDependencyGraphTest, LooksUpDependentsForDeletionBlocking)
{
  SILICON::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
                            circuit("circuits/debug.json", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", sceneWithSubcircuits({"adder"})),
                            reusableCircuit("adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("circuits/alu.json"),
            (std::vector<std::string>{"circuits/debug.json", "circuits/main.json"}));
  EXPECT_EQ(graph.dependentsOf("circuits/adder.json"),
            std::vector<std::string>({"circuits/alu.json"}));
}

TEST(ProjectDependencyGraphTest, ReplacementRequiresRegisteredDocument)
{
  ProjectDependencyGraph graph;

  EXPECT_THROW(graph.replaceDocumentDependencies("circuits/missing.json", emptyScene()),
               std::runtime_error);
  EXPECT_FALSE(graph.containsDocument("circuits/missing.json"));
}

TEST(ProjectDependencyGraphTest, ReferencedDocumentRemovalIsRejectedAtomically)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
                            circuit("circuits/debug.json", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", emptyScene())});

  try {
    graph.removeDocument("circuits/alu.json");
    FAIL() << "Expected referenced removal to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("circuits/debug.json"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("circuits/main.json"), std::string::npos);
  }

  EXPECT_TRUE(graph.containsDocument("circuits/alu.json"));
  EXPECT_EQ(graph.dependentsOf("circuits/alu.json"),
            (std::vector<std::string>{"circuits/debug.json", "circuits/main.json"}));

  graph.removeDocument("circuits/main.json");
  EXPECT_FALSE(graph.containsDocument("circuits/main.json"));
  EXPECT_EQ(graph.dependentsOf("circuits/alu.json"),
            std::vector<std::string>{"circuits/debug.json"});
}

TEST(ProjectDependencyGraphTest, RejectsMalformedSubcircuitComponentsWithContext)
{
  for (const std::string_view malformed :
       {R"({"components":[{"type":"Subcircuit"}]})",
        R"({"components":[{"type":"Subcircuit","properties":{}}]})",
        R"({"components":[{"type":"Subcircuit","properties":{"slug":123}}]})",
        R"({"components":[{"type":"Subcircuit","properties":{"slug":""}}]})",
        R"({"components":[{"type":"Subcircuit","properties":{"slug":"a/b"}}]})"}) {
    ProjectDependencyGraph graph;
    graph.addDocument("circuits/main.json");
    expectRuntimeErrorContaining(
        [&] { graph.replaceDocumentDependencies("circuits/main.json", malformed); },
        "circuits/main.json");
  }
}

TEST(ProjectDependencyGraphTest, RejectsMalformedComponentsField)
{
  ProjectDependencyGraph graph;
  graph.addDocument("circuits/main.json");
  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("circuits/main.json",
                                          R"({"components":"not-an-array"})");
      },
      "components must be an array");
}

TEST(ProjectDependencyGraphTest, SupportsBothEstablishedJsonRootForms)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json",
               R"({"components":[{"type":"Subcircuit","properties":{"slug":"alu"}}]})"),
       reusableCircuit("alu", emptyScene())});
  EXPECT_EQ(graph.dependentsOf("circuits/alu.json"),
            std::vector<std::string>{"circuits/main.json"});
}

TEST(ProjectDependencyGraphTest, CyclePredicatePropagatesNonCycleFailures)
{
  ProjectDependencyGraph graph;
  graph.addDocument("circuits/main.json");

  EXPECT_FALSE(graph.wouldIntroduceCycle("circuits/main.json", emptyScene()));
  EXPECT_THROW(static_cast<void>(graph.wouldIntroduceCycle(
                   "circuits/main.json", sceneWithSubcircuits({"missing"}))),
               std::runtime_error);
  EXPECT_THROW(
      static_cast<void>(graph.wouldIntroduceCycle("circuits/main.json", "not json")),
      std::runtime_error);
}

TEST(ProjectDependencyGraphTest, RecursiveErrorMessageIncludesTrace)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject({reusableCircuit("cpu", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", emptyScene())});
  try {
    graph.validateDocumentDependencies("circuits/alu.json",
                                       sceneWithSubcircuits({"cpu"}));
    FAIL() << "Expected recursion";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("[cpu, alu, cpu]"), std::string::npos);
  }
}

TEST(ProjectDependencyGraphTest, FailedFullRebuildPreservesPreviousGraph)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"alu"})),
                            reusableCircuit("alu", emptyScene())});

  EXPECT_THROW(graph.rebuildFromProject({circuit("circuits/replacement.json",
                                                 sceneWithSubcircuits({"missing"}))}),
               std::runtime_error);

  EXPECT_TRUE(graph.containsDocument("circuits/main.json"));
  EXPECT_TRUE(graph.containsDocument("circuits/alu.json"));
  EXPECT_FALSE(graph.containsDocument("circuits/replacement.json"));
  EXPECT_EQ(graph.dependentsOf("circuits/alu.json"),
            std::vector<std::string>{"circuits/main.json"});
}

TEST(ProjectDependencyGraphTest, CodeDocumentsAreExcludedFromRebuild)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", emptyScene()),
                            Document("code/adder.v", "module adder; endmodule")});
  EXPECT_TRUE(graph.containsDocument("circuits/main.json"));
  EXPECT_FALSE(graph.containsDocument("code/adder.v"));
  EXPECT_THROW(graph.addDocument("code/adder.v"), std::invalid_argument);
}

TEST(ProjectDependencyGraphTest, BinaryDocumentsAreExcludedFromRebuild)
{
  ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", emptyScene()),
                            Document("bin/firmware", std::string("\0\xff", 2))});
  EXPECT_TRUE(graph.containsDocument("circuits/main.json"));
  EXPECT_FALSE(graph.containsDocument("bin/firmware"));
  EXPECT_THROW(graph.addDocument("bin/firmware"), std::invalid_argument);
}

TEST(ProjectDependencyGraphTest, RebuildRejectsDuplicateGraphicalDocuments)
{
  ProjectDependencyGraph graph;
  EXPECT_THROW(graph.rebuildFromProject({circuit("circuits/main.json", emptyScene()),
                                         circuit("circuits/main.json", emptyScene())}),
               std::runtime_error);
  EXPECT_FALSE(graph.containsDocument("circuits/main.json"));
}
