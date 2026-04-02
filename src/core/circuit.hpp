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

#include <core/wire.hpp>

#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

// Bundled vertex property: each vertex holds a weak_ptr to a Component.
struct VertexProperty {
  Component_weakPtr component;
};

// Bundled edge property: each edge holds the Bus that connects two components.
struct EdgeProperty {
  Bus bus;
};

// The circuit graph: directed, using vecS for both vertex and edge storage,
// with bundled vertex and edge properties.
// Changed to bidirectionalS to allow backward traversal (Fan-in logic)
using CircuitGraph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, VertexProperty,
                          EdgeProperty>;

using VertexDescriptor = boost::graph_traits<CircuitGraph>::vertex_descriptor;
using EdgeDescriptor   = boost::graph_traits<CircuitGraph>::edge_descriptor;

class Circuit {
private:
  CircuitGraph graph;
  std::string  name;

  // Maps a Component (by its raw pointer identity) to its vertex in the graph.
  std::unordered_map<const Component*, VertexDescriptor> componentToVertex;

  // Get or create a vertex for the given component.
  VertexDescriptor getOrAddVertex(const Component_weakPtr& component);

  // Rebuild all edges from a component's inputs/outputs.
  void rebuildEdges(VertexDescriptor v);

  // Split all the buses between inputs and outputs.
  std::pair<std::vector<Bus>, std::vector<Bus>> getComponentIOs() const;

  void addComponentRecursive(const Component_weakPtr&       component,
                             std::vector<VertexDescriptor>& newlyAdded);

public:
  struct SimulationBlock;

  Circuit() = default;
  explicit Circuit(const Component_set& components, bool explore = false);
  explicit Circuit(const Component_weakPtr& component, bool explore = true);

  [[nodiscard]] const std::string& getName() const { return name; }

  void addComponent(const Component_weakPtr& component);

  [[nodiscard]] std::vector<Bus> getInputs() const;
  [[nodiscard]] std::vector<Bus> getOutputs() const;
  [[nodiscard]] Component_set    getComponentsForBus(Bus b) const;

  // Extracts a sub-circuit containing only the components needed to calculate
  // targetOutput (Fan-in).
  [[nodiscard]] Circuit getBackwardsSubgraph(const Bus& targetOutput) const;

  // Extracts a sub-circuit of all downstream components affected by sourceInput
  // (Fan-out).
  [[nodiscard]] Circuit getForwardSubgraph(const Bus& sourceInput) const;

  // New BGL-powered queries:

  /// Returns the components in topological order (inputs first, outputs last).
  [[nodiscard]] std::vector<Component_weakPtr> topologicalOrder() const;

  /// Returns the underlying BGL graph for algorithm use.
  [[nodiscard]] const CircuitGraph& getGraph() const { return graph; }

  /// Splits the circuit into ordered simulation blocks (DAG parts and Cyclic parts).
  /// Contiguous acyclic SCCs are merged into a single block to reduce solver overhead.
  /// Evaluation order runs sequentially from vector index 0 to size-1.
  [[nodiscard]] std::vector<SimulationBlock> splitCyclic() const;

  void serialize() const { throw std::logic_error("To be implemented"); }
};

struct Circuit::SimulationBlock {
  bool                           isCyclic;
  Circuit                        circuit;         // Populated if isCyclic == true
  std::vector<Component_weakPtr> executionOrder;  // Populated if isCyclic == false
};