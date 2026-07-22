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

silicon::project::Document circuit(std::string path, std::string sceneJson)
{
  return {std::move(path), std::move(sceneJson)};
}

silicon::project::Document subcircuit(std::string slug, std::string sceneJson)
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

TEST(ProjectDependencyGraphTest, ExtractsEdgesFromCircuitsAndSubcircuits)
{
  silicon::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder"})),
       subcircuit("adder", sceneWithSubcircuits({"half_adder"})),
       subcircuit("half_adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("subcircuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
  EXPECT_EQ(graph.dependentsOf("subcircuits/half_adder.json"),
            std::vector<std::string>({"subcircuits/adder.json"}));
}

TEST(ProjectDependencyGraphTest, DuplicatePlacementsProduceOneDependency)
{
  silicon::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject(
      {circuit("circuits/main.json", sceneWithSubcircuits({"adder", "adder"})),
       subcircuit("adder", emptyScene())});

  EXPECT_EQ(graph.dependentsOf("subcircuits/adder.json"),
            std::vector<std::string>({"circuits/main.json"}));
}

TEST(ProjectDependencyGraphTest, RejectsMissingSubcircuitTarget)
{
  silicon::project::ProjectDependencyGraph graph;

  EXPECT_THROW(
      graph.rebuildFromProject(
          {circuit("circuits/main.json", sceneWithSubcircuits({"missing"}))}),
      std::runtime_error);
}

TEST(ProjectDependencyGraphTest, DetectsDirectSelfCycle)
{
  silicon::project::ProjectDependencyGraph graph;
  graph.addDocument("subcircuits/adder.json");

  EXPECT_TRUE(graph.wouldIntroduceCycle("subcircuits/adder.json",
                                        sceneWithSubcircuits({"adder"})));
}

TEST(ProjectDependencyGraphTest, RejectsDirectSelfCycleWithSlugTrace)
{
  silicon::project::ProjectDependencyGraph graph;
  graph.addDocument("subcircuits/adder.json");

  expectRuntimeErrorContaining(
      [&] {
        graph.replaceDocumentDependencies("subcircuits/adder.json",
                                          sceneWithSubcircuits({"adder"}));
      },
      "recursion trace: [adder, adder]");

  EXPECT_TRUE(graph.dependentsOf("subcircuits/adder.json").empty());
}

TEST(ProjectDependencyGraphTest, DetectsIndirectCycle)
{
  silicon::project::ProjectDependencyGraph graph;
  graph.rebuildFromProject({circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                            subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                            subcircuit("alu", emptyScene())});

  EXPECT_TRUE(
      graph.wouldIntroduceCycle("subcircuits/alu.json", sceneWithSubcircuits({"cpu"})));
}

TEST(ProjectDependencyGraphTest, RejectsIndirectCycleWithSlugTrace)
{
  silicon::project::ProjectDependencyGraph graph;
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

TEST(ProjectDependencyGraphTest, RebuildRejectsCyclicProject)
{
  silicon::project::ProjectDependencyGraph graph;

  EXPECT_THROW(graph.rebuildFromProject(
                   {circuit("circuits/main.json", sceneWithSubcircuits({"cpu"})),
                    subcircuit("cpu", sceneWithSubcircuits({"alu"})),
                    subcircuit("alu", sceneWithSubcircuits({"cpu"}))}),
               std::runtime_error);
}

TEST(ProjectDependencyGraphTest, LooksUpDependentsForDeletionBlocking)
{
  silicon::project::ProjectDependencyGraph graph;
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
