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

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#include <core/wire.hpp>

class ComponentRegistry;

/** @brief Property stored in each vertex of the circuit graph */
struct VertexProperty {
  /** @brief The component this vertex represents */
  Component_ptr component;
};

/** @brief Property stored in each edge of the circuit graph */
struct EdgeProperty {
  /** @brief The bus of wires this edge represents */
  Bus bus;
};

/** @brief Boost graph type representing the circuit topology */
/**
 * @brief Boost graph type representing the circuit topology.
 *
 * @note Uses vecS for vertex storage - vertex descriptors are indices. If vertices
 * are ever removed, descriptors stored in componentToVertex will be invalidated and
 * require rebuilding.
 */
using CircuitGraph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, VertexProperty,
                          EdgeProperty>;

/** @brief Descriptor type for vertices in the circuit graph */
using VertexDescriptor = boost::graph_traits<CircuitGraph>::vertex_descriptor;

/** @brief Descriptor type for edges in the circuit graph */
using EdgeDescriptor = boost::graph_traits<CircuitGraph>::edge_descriptor;

/**
 * @class Circuit
 * @brief Represents a digital circuit as a directed graph of components.
 *
 * The Circuit class manages a collection of components and their connections through
 * wires. It uses a Boost Graph-based internal representation to track component
 * relationships and enable operations like topological sorting, subgraph extraction,
 * and reactive simulation.
 *
 * @note The circuit tracks connectivity through shared wires between component
 * input/output buses, enabling automatic graph construction and dynamic updates.
 */
class Circuit : public std::enable_shared_from_this<Circuit> {
public:
  /**
   * @brief Callback type for topology change notifications.
   *
   * Topology observers are invoked whenever the circuit's component graph changes,
   * such as when components are added or their I/O connections are modified.
   */
  using TopologyObserver = std::function<void()>;

private:
  /** @brief The Boost graph representing the circuit topology */
  CircuitGraph graph;

  /** @brief Name of the circuit */
  std::string name;

  /** @brief Maps components to their vertex descriptors in the graph */
  std::unordered_map<const Component*, VertexDescriptor> componentToVertex;

  /** @brief Maps wire IDs to components that listen to changes on that wire */
  std::unordered_map<uint64_t, std::vector<Component_ptr>> wireListeners;

  /** @brief Components owned by this circuit (for deserialization) */
  std::vector<Component_ptr> ownedComponents;

  /** @brief Wires owned by this circuit (for deserialization) */
  std::vector<Wire_ptr> ownedWires;

  /** @brief Whether interactive mode is enabled for live editing */
  bool isInteractive = false;

  /** @brief Counter for generating unique topology listener IDs */
  uint64_t nextTopologyListenerId = 0;

  /** @brief Map of topology listener callbacks indexed by ID */
  std::unordered_map<uint64_t, TopologyObserver> topologyListeners;

  /**
   * @brief Gets or adds a vertex for a component
   * @param component The component to get or add
   * @return The vertex descriptor
   */
  VertexDescriptor getOrAddVertex(const Component_ptr& component);

  /**
   * @brief Rebuilds directed edges in the graph based on wire connections.
   *
   * A directed edge (u -> v) is added when component u's output bus shares at least one
   * wire with component v's input bus. This creates the dataflow graph used for
   * topological sorting and subgraph extraction.
   *
   * @param v The vertex whose edges should be rebuilt
   */
  void rebuildEdges(VertexDescriptor v);

  /**
   * @brief Collects all unique input and output buses from every component in the
   * circuit.
   *
   * Used to determine circuit interface.
   *
   * @return A pair of vectors: first is input buses, second is output buses
   */
  std::pair<std::vector<Bus>, std::vector<Bus>> getComponentIOs() const;

  /**
   * @brief Recursively adds a component and all connected components
   * @param component The component to add
   * @param newlyAdded Vector to collect newly added vertex descriptors
   */
  void addComponentRecursive(const Component_ptr&           component,
                             std::vector<VertexDescriptor>& newlyAdded);

  /**
   * @brief Computes a valid execution order for all vertices in the circuit.
   *
   * @note Uses Boost's topological_sort algorithm which requires a DAG (Directed Acyclic
   * Graph).
   */
  [[nodiscard]] std::vector<VertexDescriptor> topologicalOrder() const;

public:
  struct SimulationBlock;

  Circuit() = default;

  /**
   * @brief Constructs a circuit from a set of components
   * @param components The set of components to include
   * @param explore If true, recursively explore and add connected components
   */
  explicit Circuit(const Component_set& components, bool explore = false);

  /**
   * @brief Constructs a circuit from a single component
   * @param component The root component to add
   * @param explore If true, recursively explore and add connected components
   */
  explicit Circuit(const Component_ptr& component, bool explore = true);

  /**
   * @brief Gets the name of the circuit
   * @return Reference to the circuit name
   */
  [[nodiscard]] const std::string& getName() const { return name; }

  /**
   * @brief Enables interactive mode for live circuit editing.
   *
   * When enabled, the circuit automatically tracks I/O changes for all components
   * and notifies listeners when connections change.
   */
  void makeInteractive();

  /**
   * @brief Adds a component to the circuit recursively.
   *
   * Adds the component and all components reachable through shared buses.
   *
   * @param component The component to add
   */
  void addComponent(const Component_ptr& component);

  /**
   * @brief Removes a component from the circuit.
   *
   * Removes the vertex and all incident edges from the graph, and removes the
   * component from componentToVertex map.
   *
   * @param component The component to remove
   */
  void removeComponent(const Component_ptr& component);

  /**
   * @brief Gets the vertex ID for a component.
   * @param component The component
   * @return Vertex ID, or -1 if not found
   */
  [[nodiscard]] std::optional<VertexDescriptor>
  getVertexId(const Component* component) const;

  /**
   * @brief Gets all component-to-vertex mappings.
   * @return Map from component pointers to vertex IDs
   */
  [[nodiscard]] const std::unordered_map<const Component*, VertexDescriptor>&
  getComponentToVertex() const
  {
    return componentToVertex;
  }

  /**
   * @brief Gets a component by its vertex ID.
   * @param vertexId The vertex ID
   * @return Component pointer, or nullptr if not found
   */
  [[nodiscard]] Component_ptr getComponentByVertexId(VertexDescriptor vertexId) const;

  /**
   * @brief Updates the circuit after a component's I/O has changed.
   *
   * Rebuilds all affected edges and notifies topology listeners.
   *
   * @param component The component whose I/O changed
   */
  void updateComponentIO(const Component_ptr& component);

  /**
   * @brief Builds a reverse mapping from wires to components that use them.
   *
   * This enables O(1) lookup of components connected to a specific wire, used for
   * subgraph extraction and reactive updates.
   */
  void buildTopologyMap();

  /**
   * @brief Adds a listener for topology changes.
   * @param cb Callback function to invoke when topology changes
   * @return Unique ID for later removing the listener
   */
  uint64_t addTopologyListener(TopologyObserver cb);

  /**
   * @brief Removes a previously added topology listener.
   * @param id The ID returned by addTopologyListener
   */
  void removeTopologyListener(uint64_t id);

  /**
   * @brief Notifies all topology listeners of a change.
   */
  void notifyTopologyListeners();

  /**
   * @brief Gets all input buses of the circuit.
   *
   * An input bus is one that appears as an input to some component but never
   * as an output of any component in the circuit.
   *
   * @return Vector of input buses
   */
  [[nodiscard]] std::vector<Bus> getInputs() const;

  /**
   * @brief Gets all output buses of the circuit.
   *
   * An output bus is one that appears as an output from some component but never
   * as an input to any component in the circuit.
   *
   * @return Vector of output buses
   */
  [[nodiscard]] std::vector<Bus> getOutputs() const;

  /**
   * @brief Finds all components that are connected to a given bus.
   *
   * A component is connected if any of its input or output buses share wires with
   * the target bus.
   *
   * @param b The bus to find components for
   * @return Set of components connected to the bus
   */
  [[nodiscard]] Component_set getComponentsForBus(Bus b) const;

  /**
   * @brief Gets all components listening to changes on a specific wire.
   *
   * @param wireId The ID of the wire
   * @return Vector of weak pointers to components
   */
  [[nodiscard]] std::vector<Component_weakPtr> getListenersForWire(uint64_t wireId) const;

  /**
   * @brief Extracts the cone of influence (COI) for a target output bus.
   *
   * Returns a new Circuit containing all components that can affect the target bus
   * through dataflow dependencies (reading from sources that eventually drive the
   * target).
   *
   * @param targetOutput The output bus to get the backwards subgraph for
   * @return A new Circuit containing all affecting components
   */
  [[nodiscard]] Circuit getBackwardsSubgraph(const Bus& targetOutput) const;

  /**
   * @brief Extracts the forward cone of influence for a source input bus.
   *
   * Returns a new Circuit containing all components that can be affected by changes to
   * the source bus (directly or indirectly through dataflow).
   *
   * @param sourceInput The input bus to get the forward subgraph for
   * @return A new Circuit containing all affected components
   */
  [[nodiscard]] Circuit getForwardSubgraph(const Bus& sourceInput) const;


  /**
   * @brief Gets the underlying circuit graph
   * @return Reference to the Boost graph
   */
  [[nodiscard]] const CircuitGraph& getGraph() const { return graph; }

  /**
   * @brief Splits the circuit into simulation blocks separating cyclic from acyclic
   * parts.
   *
   * This is essential for proper simulation: acyclic parts execute once in
   * topological order, while cyclic parts require iterative delta cycles to converge.
   *
   * @return Vector of SimulationBlocks ordered for proper execution
   */
  [[nodiscard]] std::vector<SimulationBlock> splitCyclic() const;

  using LevelMap = std::map<Component_weakPtr, unsigned int, std::owner_less<Component_weakPtr>>;

  /** @brief Get a levelized map between the components and their level number */
  [[nodiscard]] Circuit::LevelMap getLevelMap() const;

  /**
   * @brief Serializes the core circuit model to a JSON string.
   *
   * @anchor core_serialization_format
   * This is the canonical serialization for the logical circuit only. It stores the
   * component graph, not the editor layout.
   *
   * The payload contains:
   * - `version` and `name`
   * - `components[]`
   * - for each component: its serialized graph `id`, registered `type`,
   *   `properties`, and `inputs` / `outputs` bus wiring
   *
   * Wire connectivity is represented by shared wire IDs inside the serialized bus
   * lists. During deserialization, components are reinserted in serialized `id` order
   * so their Boost vertex descriptors match the saved graph IDs again.
   *
   * This format intentionally does not store any UI state such as graphical positions,
   * rotations, wire segment geometry, or runtime scene item IDs. Those belong to the
   * DiagramScene serialization format documented in @ref
   * diagramscene_serialization_format.
   *
   * @return JSON string representation of the logical circuit model
   */
  [[nodiscard]] std::string serialize() const;

  /**
   * @brief Deserializes the core circuit model from a JSON string.
   *
   * Expects the payload documented in @ref core_serialization_format. The registry is
   * used to recreate component instances by serialized type name.
   *
   * @param jsonStr The JSON string to deserialize
   * @param reg The component registry to create components
   * @return The deserialized circuit
   */
  [[nodiscard]] static Circuit deserialize(const std::string&       jsonStr,
                                           const ComponentRegistry& reg);
};

/** @brief Represents a block of components for simulation execution */
struct Circuit::SimulationBlock {
  /** @brief Whether this block contains cyclic dependencies */
  bool isCyclic;

  /** @brief The sub-circuit containing the components in this block */
  Circuit circuit;

  /** @brief Pre-computed execution order for acyclic blocks */
  std::vector<Component_weakPtr> executionOrder;
};
