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

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#include <core/projectDocument.hpp>

namespace SILICON::project {

/**
 * @brief Runtime-only graph of project document containment dependencies.
 *
 * Vertices are project-relative document paths such as `circuits/main.json` and
 * `subcircuits/adder.json`. Edges point from a document to the subcircuit
 * document it instantiates. The graph is derived from scene JSON and is never
 * serialized into project archives.
 *
 * The graph uses `boost::vecS` for its vertex container, which means vertex
 * descriptors are integer indices. As a consequence, removing a vertex
 * reindexes every subsequent vertex, so the `pathToVertex` index must be
 * rebuilt after a removal. Path lookup remains average O(1) thanks to the
 * `std::unordered_map` index. Public mutations use project-sized candidate
 * copies for strong exception safety; removal additionally incurs an O(V)
 * path-index rebuild.
 */
class ProjectDependencyGraph {
public:
  /// @brief List of project-relative document paths.
  using DocumentPathList = std::vector<std::string>;

  /**
   * @brief Removes every vertex and edge, returning the graph to an empty state.
   */
  void clear();

  /**
   * @brief Ensures a document vertex exists, creating it if necessary.
   *
   * @param documentPath Project-relative path of the document to register.
   */
  void addDocument(std::string_view documentPath);

  /**
   * @brief Removes an unreferenced document atomically.
   *
   * If @p documentPath is not present in the graph this call is a no-op. If any
   * document depends on it, removal is rejected and the graph is unchanged.
   * Removing a vertex reindexes the remaining vertices (due to `vecS`), so the
   * internal path index is rebuilt afterwards.
   *
   * @param documentPath Project-relative path of the document to remove.
   */
  void removeDocument(std::string_view documentPath);

  /** Validates that a document can be removed without losing a source reference. */
  void validateDocumentRemoval(std::string_view documentPath) const;

  /**
   * @brief Rebuilds the graph from a full project description.
   *
   * All graphical documents are first registered as vertices and then wired with
   * their dependency edges. The resulting graph must be acyclic; if it is not,
   * a `std::runtime_error` is thrown and the graph is left unchanged.
   *
   * @param documents The project's ordered documents. Non-graphical documents are ignored.
   * @throw std::runtime_error If the described dependencies form a cycle.
   */
  void rebuildFromProject(const std::vector<Document>& documents);

  /**
   * @brief Replaces the dependency edges of a single document atomically.
   *
   * The new edges are computed against a private copy of the graph and only
   * committed if every referenced subcircuit already exists and the resulting
   * graph is acyclic; otherwise, a `std::runtime_error` is thrown and the graph
   * is left unchanged.
   *
   * @param documentPath Project-relative path of the document to update.
   * @param contents     Graphical document JSON to extract dependencies from.
   * Both `{"components":[...]}` and the serialized scene form
   * `{"circuit":{"components":[...]}}` are supported. A missing components
   * member is accepted for legacy empty circuits; a present member must be an array.
   *
   * @throw std::runtime_error If @p contents is malformed, references a missing
   *                           subcircuit, or would create recursive dependencies.
   */
  void replaceDocumentDependencies(std::string_view documentPath,
                                   std::string_view contents);

  /**
   * Validates a candidate dependency update without mutating the graph.
   * Missing documents, malformed references, and recursion retain distinct errors.
   */
  void validateDocumentDependencies(std::string_view documentPath,
                                    std::string_view contents) const;

  /**
   * @brief Tests whether rewiring a document's dependencies would create a cycle.
   *
   * Operates on a private copy of the graph, so it has no side effects.
   *
   * @param documentPath Project-relative path of the document to test.
   * @param contents     Graphical document JSON to extract candidate dependencies from.
   * @return `true` only if an otherwise valid candidate introduces a cycle.
   * Other validation failures propagate their specific exception.
   */
  [[nodiscard]] bool wouldIntroduceCycle(std::string_view documentPath,
                                         std::string_view contents) const;

  /**
   * @brief Returns the sorted list of documents that depend on a subcircuit.
   *
   * @param subcircuitPath Project-relative path of the subcircuit document.
   * @return The dependents, sorted lexicographically; empty if the subcircuit
   *         is unknown or has no dependents.
   */
  [[nodiscard]] DocumentPathList dependentsOf(std::string_view subcircuitPath) const;

  /**
   * @brief Checks whether a document vertex is present in the graph.
   *
   * @param documentPath Project-relative path of the document to look up.
   * @return `true` if the document is registered.
   */
  [[nodiscard]] bool containsDocument(std::string_view documentPath) const;

private:
  /// @brief Per-vertex payload holding the document's project-relative path.
  struct VertexProperty {
    std::string path;
  };

  /// @brief Boost graph type: set out-edges, vector vertices, directed.
  using Graph =
      boost::adjacency_list<boost::setS, boost::vecS, boost::directedS, VertexProperty>;
  /// @brief Boost vertex descriptor (an integer index under `vecS`).
  using Vertex = boost::graph_traits<Graph>::vertex_descriptor;

  /// @brief DFS visit marker used while reconstructing a dependency cycle.
  enum class VisitState { Unvisited, Visiting, Visited };

  /// @brief O(1) index mapping a document path to its graph vertex.
  using PathToVertex = std::unordered_map<std::string, Vertex>;

  /// @brief The dependency graph itself.
  Graph graph;
  /// @brief Path-to-vertex index kept in sync with @ref graph.
  PathToVertex pathToVertex;

  [[nodiscard]] std::optional<Vertex> findVertex(std::string_view documentPath) const;

  [[nodiscard]] ProjectDependencyGraph
  withDocumentDependencies(std::string_view documentPath,
                           std::string_view contents) const;

  void replaceDependencyEdges(std::string_view documentPath, std::string_view contents);

  /**
   * @brief Finds one recursive dependency cycle and returns it as ordered slugs.
   *
   * The repeated slug is appended at the end of the trace, e.g.
   * `[cpu, alu, cpu]`. Returns an empty vector when the graph is acyclic.
   */
  [[nodiscard]] std::vector<std::string> findCycleTrace() const;

  /**
   * @brief DFS step used by @ref findCycleTrace.
   *
   * @param vertex Vertex currently being visited.
   * @param states Per-vertex DFS state, indexed by vertex descriptor.
   * @param path Current recursion path from the DFS root to @p vertex.
   * @return Ordered slug trace for the first cycle found, or empty when this
   *         branch is acyclic.
   */
  [[nodiscard]] std::vector<std::string>
  findCycleTraceFrom(Vertex vertex, std::vector<VisitState>& states,
                     std::vector<Vertex>& path) const;

  /**
   * @brief Builds a slug trace when DFS reaches a vertex already in @p path.
   *
   * @param repeated Vertex that closes the cycle.
   * @param path Current DFS recursion path containing @p repeated.
   * @return Ordered slug trace from the first occurrence of @p repeated through
   *         the current path, with @p repeated appended again at the end.
   */
  [[nodiscard]] std::vector<std::string>
  cycleTraceEndingAt(Vertex repeated, const std::vector<Vertex>& path) const;

  /**
   * @brief Throws a runtime error with a recursion trace if the graph is cyclic.
   */
  void throwIfCyclic() const;

  /**
   * @brief Ensures a document vertex exists in this graph (O(1)).
   *
   * @param documentPath Project-relative path of the document.
   * @return The descriptor of the existing or newly created vertex.
   */
  [[nodiscard]] Vertex ensureVertex(std::string_view documentPath);

  /**
   * @brief Rebuilds @ref pathToVertex from the current @ref graph.
   *
   * Required after `remove_vertex` because `vecS` reindexes descriptors.
   */
  void rebuildPathIndex();

  /**
   * @brief Verifies the graph is acyclic.
   *
   * @throw boost::not_a_dag If the graph contains a cycle.
   */
  void validateAcyclic() const;
};

}  // namespace SILICON::project
