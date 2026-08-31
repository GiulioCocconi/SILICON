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
#include <utility>

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
  extractSubcircuitDependencies(const std::string_view documentPath,
                                const std::string_view contents)
  {
    std::vector<std::string> dependencies;

    nlohmann::json parsed;
    try {
      parsed = nlohmann::json::parse(contents);
    } catch (const nlohmann::json::parse_error& error) {
      throw std::runtime_error(
          std::format("{} contains invalid JSON: {}", documentPath, error.what()));
    }
    if (!parsed.is_object())
      throw std::runtime_error(
          std::format("{} must contain a JSON object", documentPath));

    const auto* circuit = &parsed;
    if (const auto circuitIt = parsed.find("circuit"); circuitIt != parsed.end()) {
      if (!circuitIt->is_object())
        throw std::runtime_error(
            std::format("{}.circuit must be an object", documentPath));
      circuit = &*circuitIt;
    }

    const auto componentsIt = circuit->find("components");
    // Older project documents without a components member represent an empty circuit.
    if (componentsIt == circuit->end())
      return dependencies;
    if (!componentsIt->is_array())
      throw std::runtime_error(
          std::format("{}.components must be an array", documentPath));

    std::unordered_set<std::string> seen;
    for (const auto& component : *componentsIt) {
      if (!component.is_object())
        continue;
      const auto typeIt = component.find("type");
      if (typeIt == component.end() || !typeIt->is_string()
          || typeIt->get_ref<const std::string&>() != "Subcircuit")
        continue;

      const auto propertiesIt = component.find("properties");
      if (propertiesIt == component.end() || !propertiesIt->is_object())
        throw std::runtime_error(std::format(
            "{} contains a Subcircuit component whose 'properties' must be an object",
            documentPath));

      const auto slugIt = propertiesIt->find("slug");
      if (slugIt == propertiesIt->end() || !slugIt->is_string())
        throw std::runtime_error(std::format(
            "{} contains a Subcircuit component with no string 'slug' property",
            documentPath));

      const auto slug = slugIt->get<std::string>();
      if (!isValidSubcircuitSlug(slug))
        throw std::runtime_error(
            std::format("{} contains a Subcircuit component with invalid slug '{}'",
                        documentPath, slug));

      auto path = subcircuitPathForSlug(slug);
      if (seen.insert(path).second)
        dependencies.push_back(std::move(path));
    }

    std::ranges::sort(dependencies);
    return dependencies;
  }

  [[nodiscard]] std::string
  formatReferencedDocumentMessage(const std::string_view documentPath,
                                  const std::vector<std::string>& dependents)
  {
    auto message =
        std::format("Cannot remove {} because it is referenced by:", documentPath);
    for (const auto& dependent : dependents)
      message += std::format("\n  {}", dependent);
    return message;
  }

}  // namespace

void ProjectDependencyGraph::clear()
{
  graph.clear();
  pathToVertex.clear();
}

void ProjectDependencyGraph::addDocument(const std::string_view documentPath)
{
  const auto type = documentTypeForPath(documentPath);
  if (!type || !isGraphicalDocumentType(*type))
    throw std::invalid_argument(
        "Dependency graph documents must be circuits or subcircuits");
  if (containsDocument(documentPath))
    return;

  auto updated = *this;
  static_cast<void>(updated.ensureVertex(documentPath));
  *this = std::move(updated);
}

void ProjectDependencyGraph::removeDocument(const std::string_view documentPath)
{
  const auto vertex = findVertex(documentPath);
  if (!vertex)
    return;

  validateDocumentRemoval(documentPath);

  auto updated       = *this;
  const auto removed = updated.findVertex(documentPath);
  boost::clear_vertex(*removed, updated.graph);
  boost::remove_vertex(*removed, updated.graph);
  updated.rebuildPathIndex();
  *this = std::move(updated);
}

void ProjectDependencyGraph::validateDocumentRemoval(
    const std::string_view documentPath) const
{
  if (!containsDocument(documentPath))
    return;
  auto dependents = dependentsOf(documentPath);
  if (!dependents.empty()) {
    auto message = formatReferencedDocumentMessage(documentPath, dependents);
    throw std::runtime_error(std::move(message));
  }
}

void ProjectDependencyGraph::rebuildFromProject(const std::vector<Document>& documents)
{
  ProjectDependencyGraph rebuilt;
  std::unordered_set<std::string_view> registeredPaths;

  for (const auto& document : documents) {
    if (!isGraphicalDocumentType(document.getType()))
      continue;
    if (!registeredPaths.insert(document.getPath()).second)
      throw std::runtime_error(
          std::format("Duplicate dependency graph document {}", document.getPath()));
    static_cast<void>(rebuilt.ensureVertex(document.getPath()));
  }

  for (const auto& document : documents) {
    if (isGraphicalDocumentType(document.getType()))
      rebuilt.replaceDependencyEdges(document.getPath(), document.getContents());
  }

  rebuilt.throwIfCyclic();
  *this = std::move(rebuilt);
}

void ProjectDependencyGraph::replaceDocumentDependencies(
    const std::string_view documentPath, const std::string_view contents)
{
  auto updated = withDocumentDependencies(documentPath, contents);
  updated.throwIfCyclic();
  *this = std::move(updated);
}

void ProjectDependencyGraph::validateDocumentDependencies(
    const std::string_view documentPath, const std::string_view contents) const
{
  auto candidate = withDocumentDependencies(documentPath, contents);
  candidate.throwIfCyclic();
}

bool ProjectDependencyGraph::wouldIntroduceCycle(const std::string_view documentPath,
                                                 const std::string_view contents) const
{
  const auto candidate = withDocumentDependencies(documentPath, contents);
  try {
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
                                                 const std::string_view contents) const
{
  auto updated = *this;
  updated.replaceDependencyEdges(documentPath, contents);
  return updated;
}

void ProjectDependencyGraph::replaceDependencyEdges(const std::string_view documentPath,
                                                    const std::string_view contents)
{
  const auto documentVertex = findVertex(documentPath);
  if (!documentVertex)
    throw std::runtime_error(
        std::format("Unknown dependency graph document {}", documentPath));

  const auto dependencies = extractSubcircuitDependencies(documentPath, contents);
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

  boost::clear_out_edges(*documentVertex, graph);
  for (const auto dependencyVertex : dependencyVertices)
    boost::add_edge(*documentVertex, dependencyVertex, graph);
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
