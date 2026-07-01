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

#include "circuit.hpp"

#include <algorithm>
#include <format>
#include <ranges>

#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>

#include <logging/logger.hpp>

#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>

#include <nlohmann/json.hpp>

namespace {

const Logger circuitLog("circuit");
}

// --- Topology Observers & Live Editing -------------------------------------------------

uint64_t Circuit::addTopologyListener(TopologyObserver cb)
{
  uint64_t id           = ++nextTopologyListenerId;
  topologyListeners[id] = std::move(cb);
  return id;
}

void Circuit::removeTopologyListener(uint64_t id)
{ topologyListeners.erase(id); }

void Circuit::notifyTopologyListeners()
{
  for (auto& [id, cb] : topologyListeners) {
    cb();
  }
}

void Circuit::makeInteractive()
{
  if (isInteractive)
    return;
  isInteractive = true;

  std::weak_ptr<Circuit> weakThis = weak_from_this();

  // Attach to all currently existing components
  for (auto v : boost::make_iterator_range(boost::vertices(graph))) {
    if (auto cPtr = graph[v].component) {
      cPtr->addIOListener([weakThis](Component* c) {
        if (auto circ = weakThis.lock()) {
          circ->updateComponentIO(c->shared_from_this());
        }
      });
    }
  }
}

Component_ptr Circuit::getComponentByVertexId(VertexDescriptor vertexId) const
{
  auto it = std::ranges::find_if(ownedComponents, [&](const auto& comp) {
    auto id = getVertexId(comp.get());
    return id && *id == vertexId;
  });

  return it != ownedComponents.end() ? *it : nullptr;
}

void Circuit::updateComponentIO(const Component_ptr& component)
{
  if (!component)
    return;

  auto it = componentToVertex.find(component.get());
  if (it == componentToVertex.end())
    return;
  VertexDescriptor v = it->second;

  // 1. Wipe old edges connected to this specific component
  boost::clear_vertex(v, graph);

  // 2. Rebuild out-edges (from this component to downstream)
  rebuildEdges(v);

  // 3. Rebuild in-edges (from upstream into this component)
  for (auto u : boost::make_iterator_range(boost::vertices(graph))) {
    if (u != v)
      rebuildEdges(u);
  }

  // 4. Update the spatial wire lookup and notify the Simulator!
  buildTopologyMap();
  notifyTopologyListeners();
}

// --- Internal Helpers ------------------------------------------------------------------

VertexDescriptor Circuit::getOrAddVertex(const Component_ptr& component)
{
  if (!component)
    return {};

  const Component* key = component.get();

  if (auto it = componentToVertex.find(key); it != componentToVertex.end()) {
    return it->second;
  }

  // If we are in interactive mode, automatically attach a listener to newly added
  // components
  if (isInteractive) {
    std::weak_ptr<Circuit> weakThis = weak_from_this();
    component->addIOListener([weakThis](Component* c) {
      if (auto circ = weakThis.lock()) {
        circ->updateComponentIO(c->shared_from_this());
      }
    });
  }

  const VertexDescriptor v = boost::add_vertex(VertexProperty{component}, graph);
  componentToVertex.emplace(key, v);

  return v;
}

void Circuit::rebuildEdges(VertexDescriptor v)
{
  const auto cPtr = graph[v].component;
  if (!cPtr)
    return;

  const auto& outputs = cPtr->outputBuses();

  // 1. subrange allows us to use range-based for loops with Boost.Graph
  for (auto target_v : boost::make_iterator_range(boost::vertices(graph))) {
    if (target_v == v)
      continue;

    const auto neighbor = graph[target_v].component;
    if (!neighbor)
      continue;

    // 2. Flatten all neighbor input buses into a single view of wires
    auto neighbor_wires = neighbor->inputBuses() | std::views::join;

    for (const Bus& outBus : outputs) {
      // 3. Filter out null/falsy wires lazily
      auto valid_out_wires =
          outBus | std::views::filter([](const auto& w) { return static_cast<bool>(w); });

      // 4. Check if any valid out-wire exists in the flattened neighbor inputs
      const bool sharesWire = std::ranges::any_of(valid_out_wires, [&](const auto& wire) {
        return std::ranges::contains(neighbor_wires, wire);  // C++23
      });

      if (sharesWire) {
        auto out_edges = boost::make_iterator_range(boost::out_edges(v, graph));

        const bool isDuplicate = std::ranges::any_of(out_edges, [&](const auto& edge) {
          return boost::target(edge, graph) == target_v && graph[edge].bus == outBus;
        });

        if (!isDuplicate) {
          boost::add_edge(v, target_v, EdgeProperty{outBus}, graph);
        }
      }
    }
  }
}

std::pair<std::vector<Bus>, std::vector<Bus>> Circuit::getComponentIOs() const
{
  std::vector<Bus> inputs  = {};
  std::vector<Bus> outputs = {};

  for (auto v : boost::make_iterator_range(boost::vertices(graph))) {
    auto cPtr = graph[v].component;
    if (!cPtr)
      continue;

    for (const Bus& bus : cPtr->inputBuses())
      inputs.push_back(bus);

    for (const Bus& bus : cPtr->outputBuses())
      outputs.push_back(bus);
  }

  std::ranges::sort(inputs);
  inputs.erase(std::ranges::unique(inputs).begin(), inputs.end());

  std::ranges::sort(outputs);
  outputs.erase(std::ranges::unique(outputs).begin(), outputs.end());

  return {std::move(inputs), std::move(outputs)};
}

// --- Topology Map ----------------------------------------------------------------------

void Circuit::buildTopologyMap()
{
  wireListeners.clear();

  for (auto v : boost::make_iterator_range(boost::vertices(graph))) {
    const auto comp = graph[v].component;
    if (!comp)
      continue;

    auto valid_wires =
        comp->inputBuses() | std::views::join
        | std::views::filter([](const auto& wire) { return static_cast<bool>(wire); });

    for (const auto& wire : valid_wires) {
      wireListeners[wire->getId()].push_back(comp);
    }
  }
}
std::vector<Component_weakPtr> Circuit::getListenersForWire(uint64_t wireId) const
{
  auto it = wireListeners.find(wireId);

  if (it == wireListeners.end())
    return {};

  std::vector<Component_weakPtr> result;
  result.reserve(it->second.size());

  for (const auto& ptr : it->second) {
    result.push_back(ptr);
  }

  return result;
}

// --- Constructors ----------------------------------------------------------------------

Circuit::Circuit(const Component_set& components, bool explore)
{
  if (!explore) {
    for (const auto& c : components)
      getOrAddVertex(c);

    for (auto v : boost::make_iterator_range(boost::vertices(graph)))
      rebuildEdges(v);
  }

  else {
    for (const auto& c : components)
      addComponent(c);
  }

  buildTopologyMap();
}

Circuit::Circuit(const Component_ptr& component, const bool explore)
{
  if (!explore) {
    getOrAddVertex(component);
  } else {
    addComponent(component);
  }

  buildTopologyMap();
}

// --- Public methods --------------------------------------------------------------------

void Circuit::addComponentRecursive(const Component_ptr&           component,
                                    std::vector<VertexDescriptor>& newlyAdded)
{
  // Already present or null = early return
  if (!component.get() || componentToVertex.contains(component.get()))
    return;

  const VertexDescriptor v = getOrAddVertex(component);
  newlyAdded.push_back(v);

  // Recursively explore neighbors through shared buses
  for (const Bus& bus : component->inputBuses())
    for (const auto& c : getComponentsForBus(bus))
      addComponentRecursive(c, newlyAdded);

  for (const Bus& bus : component->outputBuses())
    for (const auto& c : getComponentsForBus(bus))
      addComponentRecursive(c, newlyAdded);
}

void Circuit::addComponent(const Component_ptr& component)
{
  std::vector<VertexDescriptor> newlyAdded;
  addComponentRecursive(component, newlyAdded);

  // Rebuild edges for every vertex (including newly added ones).
  // This is safe because addComponent guards against re-entry via the map check.
  for (VertexDescriptor v : newlyAdded)
    rebuildEdges(v);

  buildTopologyMap();
  notifyTopologyListeners();
}

void Circuit::removeComponent(const Component_ptr& component)
{
  if (!component)
    return;

  auto it = componentToVertex.find(component.get());
  if (it == componentToVertex.end())
    return;

  VertexDescriptor v = it->second;

  boost::clear_vertex(v, graph);
  boost::remove_vertex(v, graph);

  componentToVertex.clear();

  for (auto vd : boost::make_iterator_range(vertices(graph))) {
    componentToVertex[graph[vd].component.get()] = vd;
  }

  buildTopologyMap();
  notifyTopologyListeners();
}

std::optional<VertexDescriptor> Circuit::getVertexId(const Component* component) const
{
  if (!component)
    return std::nullopt;

  if (auto it = componentToVertex.find(component); it != componentToVertex.end())
    return it->second;

  return std::nullopt;
}

std::vector<Bus> Circuit::getInputs() const
{
  auto [inputBuses, outputBuses] = getComponentIOs();

  std::ranges::sort(inputBuses);
  std::ranges::sort(outputBuses);

  std::vector<Bus> result;

  std::ranges::set_difference(inputBuses, outputBuses, std::back_inserter(result));

  return result;
}

std::vector<Bus> Circuit::getOutputs() const
{
  auto [inputBuses, outputBuses] = getComponentIOs();

  std::ranges::sort(inputBuses);
  std::ranges::sort(outputBuses);

  std::vector<Bus> result;

  std::ranges::set_difference(outputBuses, inputBuses, std::back_inserter(result));
  return result;
}

Component_set Circuit::getComponentsForBus(Bus b) const
{
  Component_set connectedComponents;

  auto valid_wires =
      b | std::views::filter([](const auto& w) { return static_cast<bool>(w); });

  // Gather components that are listening to these wires
  for (const auto& wire : valid_wires) {
    for (const auto& weakComp : getListenersForWire(wire->getId())) {
      if (auto comp = weakComp.lock()) {
        connectedComponents.insert(comp);
      }
    }
  }

  // Gather components that output to these wires
  for (auto v : boost::make_iterator_range(boost::vertices(graph))) {
    const auto cPtr = graph[v].component;
    if (!cPtr)
      continue;

    // Flatten all of this component's output buses into a single stream of wires
    auto out_wires = cPtr->outputBuses() | std::views::join;

    // Check if ANY valid wire from `b` exists in this component's outputs
    const bool matches = std::ranges::any_of(valid_wires, [&](const auto& wire) {
      return std::ranges::contains(out_wires, wire);  // C++23
    });

    if (matches) {
      connectedComponents.insert(cPtr);  // Re-use cPtr, no double lookup!
    }
  }

  return connectedComponents;
}

// --- Wire Reactivity & Cone Subgraphs --------------------------------------------------

Circuit Circuit::getBackwardsSubgraph(const Bus& targetOutput) const
{
  Component_set                        coiComponents;
  std::vector<VertexDescriptor>        stack;
  std::unordered_set<VertexDescriptor> visited;

  auto valid_target_wires = targetOutput | std::views::filter([](const auto& w) {
                              return static_cast<bool>(w);
                            });

  // Find starting vertices
  for (auto v : boost::make_iterator_range(boost::vertices(graph))) {
    const auto cPtr = graph[v].component;
    if (!cPtr)
      continue;

    // Flatten all output buses into a single stream of wires
    auto out_wires = cPtr->outputBuses() | std::views::join;

    // Check if this component drives ANY of our valid target wires
    const bool drivesWire =
        std::ranges::any_of(valid_target_wires, [&](const auto& wire) {
          return std::ranges::contains(out_wires, wire);  // C++23
        });

    if (drivesWire) {
      stack.push_back(v);
      visited.insert(v);
    }
  }

  // DFS backwards traversal
  while (!stack.empty()) {
    const VertexDescriptor v = stack.back();
    stack.pop_back();

    if (const auto comp = graph[v].component) {
      coiComponents.insert(comp);
    }

    for (auto edge : boost::make_iterator_range(boost::in_edges(v, graph))) {
      VertexDescriptor sourceVertex = boost::source(edge, graph);
      if (visited.insert(sourceVertex).second) {
        stack.push_back(sourceVertex);
      }
    }
  }

  return Circuit(coiComponents, false);
}

Circuit Circuit::getForwardSubgraph(const Bus& sourceInput) const
{
  Component_set                        focComponents;
  std::vector<VertexDescriptor>        stack;
  std::unordered_set<VertexDescriptor> visited;

  auto valid_source_wires = sourceInput | std::views::filter([](const auto& w) {
                              return static_cast<bool>(w);
                            });

  for (const auto& wire : valid_source_wires) {
    for (const auto& weakComp : getListenersForWire(wire->getId())) {
      if (auto cPtr = weakComp.lock()) {
        auto it = componentToVertex.find(cPtr.get());

        if (it != componentToVertex.end() && visited.insert(it->second).second) {
          stack.push_back(it->second);
        }
      }
    }
  }

  while (!stack.empty()) {
    const VertexDescriptor v = stack.back();
    stack.pop_back();

    if (const auto comp = graph[v].component) {
      focComponents.insert(comp);
    }

    for (auto edge : boost::make_iterator_range(boost::out_edges(v, graph))) {
      VertexDescriptor targetVertex = boost::target(edge, graph);
      if (visited.insert(targetVertex).second) {
        stack.push_back(targetVertex);
      }
    }
  }

  return Circuit(focComponents, false);
}

// --- Execution Blocks -----------------------------------------------------------------

std::vector<Circuit::SimulationBlock> Circuit::splitCyclic() const
{
  circuitLog.debug("Splitting the circuit in cyclic and acyclic blocks...");
  const auto n = boost::num_vertices(graph);

  // Early exit: Avoids allocating maps and vectors if there's nothing to process.
  if (n == 0)
    return {};

  // --- STEP 1: Identify Strongly Connected Components (SCCs) --------------------------
  // An SCC is a subgraph where every vertex can reach every other vertex. An SCC of size
  // > 1 represents a feedback loop (cycle). BGL guarantees topological order.

  std::vector<int> componentMap(n);
  const auto       vertexIndexMap = boost::get(boost::vertex_index, graph);

  const int numSccs = boost::strong_components(
      graph, boost::make_iterator_property_map(componentMap.begin(), vertexIndexMap));

  // --- STEP 2: Group vertices by their SCC ID ------------------------------------------
  std::vector<std::vector<VertexDescriptor>> sccVertices(numSccs);
  auto [v_begin, v_end] = boost::vertices(graph);

  for (const auto v : std::ranges::subrange(v_begin, v_end)) {
    const auto scc_id = componentMap[boost::get(vertexIndexMap, v)];
    sccVertices[scc_id].push_back(v);
  }

  // This vector acts as an accumulator for adjacent acyclic (DAG) components.
  // We want to combine adjacent DAG segments into a single SimulationBlock to minimize
  // execution overhead.

  std::vector<SimulationBlock>   blocks;
  std::vector<Component_weakPtr> currentDagComps;

  // Helper: finalize and store the accumulated DAG
  auto flushDag = [&]() {
    if (!currentDagComps.empty()) {
      Component_set comps(currentDagComps.begin(), currentDagComps.end());
      auto          execOrder = std::move(currentDagComps);
      blocks.emplace_back(false, Circuit(comps, false), std::move(execOrder));
    }
  };

  // --- STEP 3: Topologically traverse and build SimulationBlocks -----------------------
  // We iterate backwards (numSccs - 1 down to 0) because BGL's component IDs represent a
  // reversed topological sort. High IDs are "upstream" dependencies.

  for (int i = numSccs - 1; i >= 0; --i) {
    const auto& verts    = sccVertices[i];
    bool        isCyclic = verts.size() > 1;

    if (verts.size() == 1) {
      auto [e_begin, e_end] = boost::out_edges(verts.front(), graph);
      isCyclic =
          std::ranges::any_of(std::ranges::subrange(e_begin, e_end), [&](const auto& e) {
            return boost::target(e, graph) == verts.front();
          });
    }

    auto validComps =
        verts | std::views::transform([this](auto v) { return graph[v].component; })
        | std::views::filter([](const auto& comp) { return comp != nullptr; });

    if (isCyclic) {
      // A cycle interrupts standard execution flow. We must flush any pending
      // DAG components so they execute *before* this cycle block.

      flushDag();
      auto cyclicComps = validComps | std::ranges::to<Component_set>();

      if (!cyclicComps.empty()) {
        blocks.emplace_back(true, Circuit(cyclicComps, false),
                            std::vector<Component_weakPtr>{});
      }
    } else {
      for (const auto& comp : validComps)
        currentDagComps.push_back(comp);
    }
  }

  // Finalize the final DAG tail (that's present if the circuit ends with a DAG block)
  flushDag();
  circuitLog.debug(std::format("Found {} blocks!", blocks.size()));
  return blocks;
}

std::vector<VertexDescriptor> Circuit::topologicalOrder() const
{
  std::vector<VertexDescriptor> order;
  order.reserve(boost::num_vertices(graph));

  circuitLog.debug("Topologically sorting the circuit...");
  boost::topological_sort(graph, std::back_inserter(order));

  // Boost's topological_sort produces reverse order, so we need to reverse it.
  std::ranges::reverse(order);

  return order;
}

Circuit::LevelMap Circuit::getLevelMap() const
{
  LevelMap levelMap;

  // Because vertex descriptors are integers 0 to N-1, a flat vector is the fastest way to
  // cache levels.
  std::vector<unsigned int> vertexLevels(boost::num_vertices(graph), 0);

  for (const VertexDescriptor v : topologicalOrder()) {
    unsigned int maxLevel = 0;

    // 1. Calculate the level based on predecessors
    for (const auto& edge : boost::make_iterator_range(boost::in_edges(v, graph))) {
      const VertexDescriptor pred = boost::source(edge, graph);

      // the topological sort guarantees that the pred level has already been processed
      const auto predLevel = vertexLevels[pred];
      maxLevel = std::max(maxLevel, predLevel + 1);
    }

    // Cache this vertex's level
    vertexLevels[v] = maxLevel;

    // 2. Map the calculated level back to the actual Component
    const Component_weakPtr compWeak = graph[v].component;

    if (compWeak.lock()) {
      levelMap[compWeak] = maxLevel;
    }
  }

  return levelMap;
}

// --- Serialization ---------------------------------------------------------------------

std::string Circuit::serialize() const
{
  nlohmann::ordered_json j = {{"version", SILICON_VERSION},
                              {"name", name},
                              {"components", nlohmann::ordered_json::array()}};

  auto serializeBusList = [](const std::vector<Bus>& buses) -> nlohmann::ordered_json {
    nlohmann::ordered_json busArray = nlohmann::ordered_json::array();
    for (const Bus& bus : buses) {
      nlohmann::ordered_json wireArray = nlohmann::ordered_json::array();
      for (const auto& wire : bus) {
        wireArray.push_back(wire ? nlohmann::ordered_json(wire->getId())
                                 : nlohmann::ordered_json(nullptr));
      }
      busArray.push_back(std::move(wireArray));
    }
    return busArray;
  };

  for (auto [vi, vi_end] = boost::vertices(graph);
       auto v : std::ranges::subrange(vi, vi_end)) {
    if (auto cPtr = graph[v].component) {
      nlohmann::ordered_json propsJson = nlohmann::ordered_json::object();
      for (const auto& [key, val] : cPtr->getProperties()) {
        std::visit([&](auto&& arg) { propsJson[key] = arg; }, val);
      }

      j["components"].push_back(
          nlohmann::ordered_json{{"id", v},
                                 {"type", cPtr->typeName()},
                                 {"properties", propsJson},
                                 {"inputs", serializeBusList(cPtr->getInputs())},
                                 {"outputs", serializeBusList(cPtr->getOutputs())}});
    }
  }

  return j.dump(2);
}

Circuit Circuit::deserialize(const std::string& jsonStr, const ComponentRegistry& reg)
{
  auto j = nlohmann::json::parse(jsonStr);

  if (j.value("version", "") != SILICON_VERSION) {
    throw std::runtime_error(std::format("The version must be {}", SILICON_VERSION));
  }

  std::unordered_map<uint64_t, Wire_ptr> wireMap;

  auto deserializeBusList = [&wireMap](const nlohmann::json& busListJson) {
    std::vector<Bus> buses;
    buses.reserve(busListJson.size());

    for (const auto& busJson : busListJson) {
      std::vector<Wire_ptr> wires;
      wires.reserve(busJson.size());

      for (const auto& wireJson : busJson) {
        if (wireJson.is_null()) {
          wires.push_back(nullptr);
          continue;
        }

        const uint64_t w_id = wireJson.get<uint64_t>();

        auto& wire = wireMap[w_id];
        if (!wire) {
          wire = std::make_shared<Wire>();
        }
        wires.push_back(wire);
      }
      buses.emplace_back(std::move(wires));
    }
    return buses;
  };

  std::vector<std::pair<int, Component_ptr>> parsedComponents;

  if (auto it = j.find("components"); it != j.end() && it->is_array()) {
    parsedComponents.reserve(it->size());
    for (const auto& compJson : *it) {
      auto type_it = compJson.find("type");
      if (type_it == compJson.end())
        continue;

      auto type = type_it->get<std::string>();
      auto cPtr = reg.create(type);
      if (!cPtr) {
        throw std::runtime_error(
            std::format("Failed to create unknown component type: {}", type));
      }

      if (auto in_it = compJson.find("inputs");
          in_it != compJson.end() && in_it->is_array()) {
        auto inputs = deserializeBusList(*in_it);
        cPtr->setInputs(inputs);
      }

      if (auto out_it = compJson.find("outputs");
          out_it != compJson.end() && out_it->is_array()) {
        auto outputs = deserializeBusList(*out_it);
        cPtr->setOutputs(outputs);
      }

      if (auto props_it = compJson.find("properties");
          props_it != compJson.end() && props_it->is_object()) {
        for (const auto& [key, val] : props_it->items()) {
          if (!cPtr->getProperty(key).has_value())
            continue;

          PropertyValue propValue;
          if (val.is_string()) {
            propValue = val.get<std::string>();
          } else if (val.is_boolean()) {
            propValue = val.get<bool>();
          } else if (val.is_number_integer()) {
            propValue = val.get<int>();
          } else {
            continue;
          }
          cPtr->setProperty(key, propValue);
        }
      }

      /* --- CRITICAL ORDERING STEP ------------------------------------------------------
       * Boost.Graph (using boost::vecS) assigns VertexDescriptors strictly as sequential
       * integers (0, 1, 2...) based on the order vertices are added.
       * Visual components in the JSON rely on these exact VertexDescriptors ('vertexId')
       * to link back to the correct underlying logic component.
       * We must sort the components by their serialized 'id' before inserting them into
       * the graph so that Boost reassigns them the exact same IDs they had when saved.
       */

      // Extract the serialized ID so we know what VertexDescriptor this component
      // originally had
      int id = compJson["id"].get<int>();
      parsedComponents.emplace_back(id, std::move(cPtr));
    }
  }

  std::ranges::sort(parsedComponents,
                    [](const auto& a, const auto& b) { return a.first < b.first; });

  Circuit result;
  result.ownedComponents.reserve(parsedComponents.size());

  // Insert components in strict serialized order
  for (auto& [id, cPtr] : parsedComponents) {
    result.getOrAddVertex(cPtr);  // Boost assigns ID: 0, then 1, then 2...
    result.ownedComponents.push_back(std::move(cPtr));
  }

  // Rebuild the edges for the correctly-aligned vertices
  for (auto v : boost::make_iterator_range(boost::vertices(result.graph))) {
    result.rebuildEdges(v);
  }

  result.ownedWires = wireMap | std::views::values | std::ranges::to<std::vector>();

  if (auto it = j.find("name"); it != j.end() && it->is_string()) {
    result.name = it->get<std::string>();
  }

  result.buildTopologyMap();
  return result;
}
