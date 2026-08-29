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

#include "projectDependencyGraph.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

#include <boost/graph/topological_sort.hpp>
#include <nlohmann/json.hpp>

namespace SILICON::project {
namespace {

  [[nodiscard]] std::string slugForDocumentPath(const std::string_view documentPath)
  {
    if (const auto slug = subcircuitSlugForPath(documentPath))
      return *slug;
    return std::string(documentPath);
  }

  [[nodiscard]] std::string formatRecursionTrace(const std::vector<std::string>& trace)
  {
    std::string result = "[";
    for (std::size_t i = 0; i < trace.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += trace[i];
    }
    result += "]";
    return result;
  }

  [[nodiscard]] std::vector<std::string>
  extractSubcircuitDependencies(const std::string_view sceneJson)
  {
    std::vector<std::string> dependencies;

    const auto  parsed  = nlohmann::json::parse(sceneJson);
    const auto* circuit = &parsed;
    if (const auto circuitIt = parsed.find("circuit");
        circuitIt != parsed.end() && circuitIt->is_object()) {
      circuit = &*circuitIt;
    }

    const auto componentsIt = circuit->find("components");
    if (componentsIt == circuit->end() || !componentsIt->is_array())
      return dependencies;

    std::unordered_set<std::string> seen;
    for (const auto& component : *componentsIt) {
      if (!component.is_object()
          || component.value("type", std::string()) != "Subcircuit")
        continue;

      const auto propertiesIt = component.find("properties");
      if (propertiesIt == component.end() || !propertiesIt->is_object())
        continue;

      const auto slugIt = propertiesIt->find("slug");
      if (slugIt == propertiesIt->end() || !slugIt->is_string())
        continue;

      const auto slug = slugIt->get<std::string>();
      if (slug.empty())
        continue;

      auto path = subcircuitPathForSlug(slug);
      if (seen.insert(path).second)
        dependencies.push_back(std::move(path));
    }

    std::ranges::sort(dependencies);
    return dependencies;
  }

}  // namespace

void ProjectDependencyGraph::clear()
{
  graph.clear();
  pathToVertex.clear();
}

void ProjectDependencyGraph::addDocument(const std::string_view documentPath)
{
  static_cast<void>(ensureVertex(documentPath));
}

void ProjectDependencyGraph::removeDocument(const std::string_view documentPath)
{
  const auto vertex = findVertex(documentPath);
  if (!vertex)
    return;

  boost::clear_vertex(*vertex, graph);
  boost::remove_vertex(*vertex, graph);
  rebuildPathIndex();
}

void ProjectDependencyGraph::rebuildFromProject(const std::vector<Document>& documents)
{
  ProjectDependencyGraph rebuilt;

  for (const auto& document : documents) {
    if (document.getType() != DocumentType::Code)
      rebuilt.addDocument(document.getPath());
  }

  for (const auto& document : documents) {
    if (document.getType() != DocumentType::Code)
      rebuilt.replaceDependencyEdges(document.getPath(), document.getContents());
  }

  rebuilt.throwIfCyclic();
  *this = std::move(rebuilt);
}

void ProjectDependencyGraph::replaceDocumentDependencies(
    const std::string_view documentPath, const std::string_view sceneJson)
{
  auto updated = withDocumentDependencies(documentPath, sceneJson);
  updated.throwIfCyclic();
  *this = std::move(updated);
}

bool ProjectDependencyGraph::wouldIntroduceCycle(const std::string_view documentPath,
                                                 const std::string_view sceneJson) const
{
  try {
    const auto candidate = withDocumentDependencies(documentPath, sceneJson);
    candidate.validateAcyclic();
  } catch (const boost::not_a_dag&) {
    return true;
  }

  return false;
}

ProjectDependencyGraph::DocumentPathList
ProjectDependencyGraph::dependentsOf(const std::string_view subcircuitPath) const
{
  DocumentPathList dependents;
  const auto       target = findVertex(subcircuitPath);
  if (!target)
    return dependents;

  for (const auto edge : boost::make_iterator_range(boost::edges(graph))) {
    if (boost::target(edge, graph) == *target)
      dependents.push_back(graph[boost::source(edge, graph)].path);
  }

  std::ranges::sort(dependents);
  return dependents;
}

bool ProjectDependencyGraph::containsDocument(const std::string_view documentPath) const
{
  return findVertex(documentPath).has_value();
}

std::optional<ProjectDependencyGraph::Vertex>
ProjectDependencyGraph::findVertex(const std::string_view documentPath) const
{
  const auto it = pathToVertex.find(std::string(documentPath));
  if (it == pathToVertex.end())
    return std::nullopt;

  return it->second;
}

ProjectDependencyGraph
ProjectDependencyGraph::withDocumentDependencies(const std::string_view documentPath,
                                                 const std::string_view sceneJson) const
{
  auto updated = *this;
  updated.replaceDependencyEdges(documentPath, sceneJson);
  return updated;
}

void ProjectDependencyGraph::replaceDependencyEdges(const std::string_view documentPath,
                                                    const std::string_view sceneJson)
{
  const auto          documentVertex = ensureVertex(documentPath);
  const auto          dependencies   = extractSubcircuitDependencies(sceneJson);
  std::vector<Vertex> dependencyVertices;
  dependencyVertices.reserve(dependencies.size());

  for (const auto& dependency : dependencies) {
    if (const auto vertex = findVertex(dependency)) {
      dependencyVertices.push_back(*vertex);
    } else {
      throw std::runtime_error(
          std::format("{} references missing subcircuit {}", documentPath, dependency));
    }
  }

  boost::clear_out_edges(documentVertex, graph);
  for (const auto dependencyVertex : dependencyVertices)
    boost::add_edge(documentVertex, dependencyVertex, graph);
}

std::vector<std::string> ProjectDependencyGraph::findCycleTrace() const
{
  std::vector<VisitState> states(boost::num_vertices(graph), VisitState::Unvisited);
  std::vector<Vertex>     path;

  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    if (states[vertex] != VisitState::Unvisited)
      continue;

    if (auto trace = findCycleTraceFrom(vertex, states, path); !trace.empty())
      return trace;
  }

  return {};
}

std::vector<std::string> ProjectDependencyGraph::findCycleTraceFrom(
    const Vertex vertex, std::vector<VisitState>& states, std::vector<Vertex>& path) const
{
  states[vertex] = VisitState::Visiting;
  path.push_back(vertex);

  for (const auto edge : boost::make_iterator_range(boost::out_edges(vertex, graph))) {
    const auto dependency = boost::target(edge, graph);

    if (states[dependency] == VisitState::Visiting)
      return cycleTraceEndingAt(dependency, path);

    if (states[dependency] != VisitState::Unvisited)
      continue;

    if (auto trace = findCycleTraceFrom(dependency, states, path); !trace.empty())
      return trace;
  }

  path.pop_back();
  states[vertex] = VisitState::Visited;
  return {};
}

std::vector<std::string>
ProjectDependencyGraph::cycleTraceEndingAt(const Vertex               repeated,
                                           const std::vector<Vertex>& path) const
{
  const auto cycleStart = std::ranges::find(path, repeated);

  std::vector<std::string> trace;
  trace.reserve(static_cast<std::size_t>(std::ranges::distance(cycleStart, path.end()))
                + 1);

  for (auto it = cycleStart; it != path.end(); ++it)
    trace.push_back(slugForDocumentPath(graph[*it].path));
  trace.push_back(slugForDocumentPath(graph[repeated].path));

  return trace;
}

ProjectDependencyGraph::Vertex
ProjectDependencyGraph::ensureVertex(const std::string_view documentPath)
{
  const auto key = std::string(documentPath);
  if (const auto it = pathToVertex.find(key); it != pathToVertex.end())
    return it->second;

  const auto vertex  = boost::add_vertex(graph);
  graph[vertex].path = key;
  pathToVertex.emplace(key, vertex);
  return vertex;
}

void ProjectDependencyGraph::rebuildPathIndex()
{
  pathToVertex.clear();
  pathToVertex.reserve(boost::num_vertices(graph));
  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph)))
    pathToVertex.emplace(graph[vertex].path, vertex);
}

void ProjectDependencyGraph::validateAcyclic() const
{
  std::vector<Vertex> order;
  order.reserve(boost::num_vertices(graph));
  boost::topological_sort(graph, std::back_inserter(order));
}

void ProjectDependencyGraph::throwIfCyclic() const
{
  try {
    validateAcyclic();
  } catch (const boost::not_a_dag&) {
    const auto trace = findCycleTrace();
    throw std::runtime_error(std::format(
        "Project contains recursive subcircuit dependencies; recursion trace: {}",
        formatRecursionTrace(trace)));
  }
}

}  // namespace SILICON::project
