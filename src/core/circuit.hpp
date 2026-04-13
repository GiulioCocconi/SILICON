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

class Circuit : public std::enable_shared_from_this<Circuit> {
public:
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
  void             rebuildEdges(VertexDescriptor v);
  std::pair<std::vector<Bus>, std::vector<Bus>> getComponentIOs() const;
  void addComponentRecursive(const Component_ptr&           component,
                             std::vector<VertexDescriptor>& newlyAdded);

public:
  struct SimulationBlock;

  Circuit() = default;
  explicit Circuit(const Component_set& components, bool explore = false);
  explicit Circuit(const Component_ptr& component, bool explore = true);

  [[nodiscard]] const std::string& getName() const { return name; }

  void makeInteractive();
  void addComponent(const Component_ptr& component);
  void updateComponentIO(const Component_ptr& component);
  void buildTopologyMap();

  uint64_t addTopologyListener(TopologyObserver cb);
  void     removeTopologyListener(uint64_t id);
  void     notifyTopologyListeners();

  [[nodiscard]] std::vector<Bus>               getInputs() const;
  [[nodiscard]] std::vector<Bus>               getOutputs() const;
  [[nodiscard]] Component_set                  getComponentsForBus(Bus b) const;
  [[nodiscard]] std::vector<Component_weakPtr> getListenersForWire(uint64_t wireId) const;

  [[nodiscard]] Circuit getBackwardsSubgraph(const Bus& targetOutput) const;
  [[nodiscard]] Circuit getForwardSubgraph(const Bus& sourceInput) const;

  [[nodiscard]] std::vector<Component_weakPtr> topologicalOrder() const;
  [[nodiscard]] const CircuitGraph&            getGraph() const { return graph; }
  [[nodiscard]] std::vector<SimulationBlock>   splitCyclic() const;

  [[nodiscard]] std::string    serialize() const;
  [[nodiscard]] static Circuit deserialize(const std::string&       jsonStr,
                                           const ComponentRegistry& reg);
};

struct Circuit::SimulationBlock {
  bool                           isCyclic;
  Circuit                        circuit;
  std::vector<Component_weakPtr> executionOrder;
};
