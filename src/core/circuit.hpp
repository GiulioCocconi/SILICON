#pragma once
#include <boost/graph/adjacency_list.hpp>
#include <core/wire.hpp>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

class ComponentRegistry;

struct VertexProperty {
  Component_ptr component;
};
struct EdgeProperty {
  Bus bus;
};

using CircuitGraph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, VertexProperty,
                          EdgeProperty>;
using VertexDescriptor = boost::graph_traits<CircuitGraph>::vertex_descriptor;
using EdgeDescriptor   = boost::graph_traits<CircuitGraph>::edge_descriptor;

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
  CircuitGraph graph;
  std::string  name;

  std::unordered_map<const Component*, VertexDescriptor>   componentToVertex;
  std::unordered_map<uint64_t, std::vector<Component_ptr>> wireListeners;

  std::vector<Component_ptr> ownedComponents;
  std::vector<Wire_ptr>      ownedWires;

  bool                                           isInteractive          = false;
  uint64_t                                       nextTopologyListenerId = 0;
  std::unordered_map<uint64_t, TopologyObserver> topologyListeners;

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
  void addComponentRecursive(const Component_ptr&           component,
                             std::vector<VertexDescriptor>& newlyAdded);

public:
  struct SimulationBlock;

  Circuit() = default;
  explicit Circuit(const Component_set& components, bool explore = false);
  explicit Circuit(const Component_ptr& component, bool explore = true);

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
   * @brief Computes a valid execution order for all components in the circuit.
   *
   * Uses Boost's topological_sort algorithm which requires a DAG (Directed Acyclic
   * Graph).
   *
   * @return Vector of components in topological order, or empty if cyclic
   */
  [[nodiscard]] std::vector<Component_weakPtr> topologicalOrder() const;

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

  [[nodiscard]] std::string    serialize() const;
  [[nodiscard]] static Circuit deserialize(const std::string&       jsonStr,
                                           const ComponentRegistry& reg);
};

struct Circuit::SimulationBlock {
  bool                           isCyclic;
  Circuit                        circuit;
  std::vector<Component_weakPtr> executionOrder;
};
