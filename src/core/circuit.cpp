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

#include <algorithm>
#include <format>
#include <ranges>

#include <core/circuit.hpp>
#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>

#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>

#include <nlohmann/json.hpp>

// --- Internal helpers ------------------------------------------------------------------

VertexDescriptor Circuit::getOrAddVertex(const Component_weakPtr& component)
{
  const auto cPtr = component.lock();
  if (!cPtr)
    return {};

  const Component* key = cPtr.get();

  if (auto it = componentToVertex.find(key); it != componentToVertex.end()) {
    return it->second;
  }

  const VertexDescriptor v = boost::add_vertex(VertexProperty{component}, graph);
  componentToVertex.emplace(key, v);

  return v;
}

void Circuit::rebuildEdges(VertexDescriptor v)
{
  const auto cPtr = graph[v].component.lock();
  if (!cPtr)
    return;

  for (const auto& outBus : cPtr->getOutputs()) {
    for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi) {
      if (*vi == v)
        continue;

      auto neighbor = graph[*vi].component.lock();
      if (!neighbor)
        continue;

      const auto& neighborInputs = neighbor->getInputs();
      if (std::ranges::find(neighborInputs, outBus) == neighborInputs.end())
        continue;

      auto [eBegin, eEnd]  = boost::out_edges(v, graph);
      auto out_edges_range = std::ranges::subrange(eBegin, eEnd);

      const bool isDuplicate =
          std::ranges::any_of(out_edges_range, [&](const auto& edge) {
            return boost::target(edge, graph) == *vi && graph[edge].bus == outBus;
          });

      if (!isDuplicate)
        boost::add_edge(v, *vi, EdgeProperty{outBus}, graph);
    }
  }
}

std::pair<std::vector<Bus>, std::vector<Bus>> Circuit::getComponentIOs() const
{
  std::vector<Bus> inputs  = {};
  std::vector<Bus> outputs = {};

  for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi) {
    auto cPtr = graph[*vi].component.lock();
    if (!cPtr)
      continue;

    for (const Bus& bus : cPtr->getInputs())
      inputs.push_back(bus);

    for (const Bus& bus : cPtr->getOutputs())
      outputs.push_back(bus);
  }

  std::ranges::sort(inputs);
  inputs.erase(std::ranges::unique(inputs).begin(), inputs.end());

  std::ranges::sort(outputs);
  outputs.erase(std::ranges::unique(outputs).begin(), outputs.end());

  return {inputs, outputs};
}

// --- Constructors ----------------------------------------------------------------------

Circuit::Circuit(const Component_set& components, bool explore)
{
  if (!explore) {
    // Just add all components as vertices, then wire up edges.
    for (const auto& c : components)
      getOrAddVertex(c);

    // Now build edges between vertices that share buses.
    for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi)
      rebuildEdges(*vi);
  } else {
    for (const auto& c : components)
      addComponent(c);
  }
}

Circuit::Circuit(const Component_weakPtr& component, const bool explore)
{
  if (!explore) {
    getOrAddVertex(component);
  } else {
    addComponent(component);
  }
}

// --- Public methods --------------------------------------------------------------------

void Circuit::addComponentRecursive(const Component_weakPtr&       component,
                                    std::vector<VertexDescriptor>& newlyAdded)
{
  const auto cPtr = component.lock();
  if (!cPtr)
    return;

  // Already present — skip.
  if (componentToVertex.contains(cPtr.get()))
    return;

  const VertexDescriptor v = getOrAddVertex(component);
  newlyAdded.push_back(v);

  // Recursively explore neighbors through shared buses.
  for (const Bus& bus : cPtr->getInputs())
    for (const Component_weakPtr& c : bus.getConnectedComponents())
      addComponentRecursive(c, newlyAdded);

  for (const Bus& bus : cPtr->getOutputs())
    for (const Component_weakPtr& c : bus.getConnectedComponents())
      addComponentRecursive(c, newlyAdded);
}

void Circuit::addComponent(const Component_weakPtr& component)
{
  std::vector<VertexDescriptor> newlyAdded;
  addComponentRecursive(component, newlyAdded);

  // Rebuild edges for every vertex (including newly added ones).
  // This is safe because addComponent guards against re-entry via the map check.
  for (VertexDescriptor v : newlyAdded)
    rebuildEdges(v);
}

std::vector<Bus> Circuit::getInputs() const
{
  auto [inputBuses, outputBuses] = getComponentIOs();

  std::vector<Bus> result;
  std::ranges::set_difference(inputBuses, outputBuses, std::back_inserter(result));

  return result;
}

std::vector<Bus> Circuit::getOutputs() const
{
  auto [inputBuses, outputBuses] = getComponentIOs();

  std::vector<Bus> result;
  std::ranges::set_difference(outputBuses, inputBuses, std::back_inserter(result));

  return result;
}

Component_set Circuit::getComponentsForBus(Bus b) const
{
  Component_set connectedComponents = {};
  for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi) {
    auto cPtr = graph[*vi].component.lock();
    if (cPtr && cPtr->isConnectedTo(b))
      connectedComponents.insert(graph[*vi].component);
  }

  return connectedComponents;
}

Circuit Circuit::getBackwardsSubgraph(const Bus& targetOutput) const
{
  Component_set                 coiComponents;  // "Cone of Influence" components
  std::vector<VertexDescriptor> stack;
  std::set<VertexDescriptor>    visited;

  // 1. Find the component(s) that drive this target output bus.
  for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi) {
    auto cPtr = graph[*vi].component.lock();
    if (!cPtr)
      continue;

    const auto& outputs = cPtr->getOutputs();
    // If this component outputs to our target bus, it's a root for our backwards search
    if (std::ranges::find(outputs, targetOutput) != outputs.end()) {
      stack.push_back(*vi);
    }
  }

  // 2. Traverse BACKWARDS through the graph using in_edges (dependencies)
  while (!stack.empty()) {
    VertexDescriptor v = stack.back();
    stack.pop_back();

    // If we haven't visited this component yet
    if (visited.insert(v).second) {
      coiComponents.insert(graph[v].component);

      // Look at all edges coming INTO this vertex
      for (auto [ie, ie_end] = boost::in_edges(v, graph); ie != ie_end; ++ie) {
        // Push the source of the incoming edge (the upstream component) onto the stack
        stack.push_back(boost::source(*ie, graph));
      }
    }
  }

  // 3. Construct and return a new Circuit using only the required components.
  // We pass 'false' to explore, meaning it will only wire up the components
  // in 'coiComponents' and ignore the rest of the original circuit.
  return Circuit(coiComponents, false);
}

Circuit Circuit::getForwardSubgraph(const Bus& sourceInput) const
{
  Component_set                 focComponents;  // "Fan-Out Cone" components
  std::vector<VertexDescriptor> stack;
  std::set<VertexDescriptor>    visited;

  // 1. Find the component(s) that read directly from this source input bus.
  for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi) {
    auto cPtr = graph[*vi].component.lock();
    if (!cPtr)
      continue;

    const auto& inputs = cPtr->getInputs();
    // If this component reads the target input, it's a starting point
    if (std::ranges::find(inputs, sourceInput) != inputs.end()) {
      stack.push_back(*vi);
      visited.insert(*vi);  // Mark visited so we don't process it twice
    }
  }

  // 2. Traverse FORWARDS through the graph using out_edges (downstream dependencies)
  while (!stack.empty()) {
    VertexDescriptor v = stack.back();
    stack.pop_back();

    focComponents.insert(graph[v].component);

    // Look at all edges going OUT of this vertex
    for (auto [oe, oe_end] = boost::out_edges(v, graph); oe != oe_end; ++oe) {
      // Get the destination component of this wire
      VertexDescriptor targetVertex = boost::target(*oe, graph);

      // If we haven't visited this downstream component yet, add it to the queue
      if (visited.insert(targetVertex).second) {
        stack.push_back(targetVertex);
      }
    }
  }

  // 3. Construct and return a new Circuit using only the affected downstream components.
  return Circuit(focComponents, false);
}

std::vector<Component_weakPtr> Circuit::topologicalOrder() const
{
  std::vector<VertexDescriptor> order;
  order.reserve(boost::num_vertices(graph));

  try {
    boost::topological_sort(graph, std::back_inserter(order));
  } catch (const boost::not_a_dag&) {
    return {};
  }

  // topological_sort produces reverse order, so reverse it.
  std::ranges::reverse(order);

  std::vector<Component_weakPtr> result;
  result.reserve(order.size());
  for (auto v : order)
    result.push_back(graph[v].component);

  return result;
}

std::vector<Circuit::SimulationBlock> Circuit::splitCyclic() const
{
  const auto n = boost::num_vertices(graph);

  // Early exit: Avoids allocating maps and vectors if there's nothing to process.
  if (n == 0)
    return {};

  // --- STEP 1: Identify Strongly Connected Components (SCCs) ---------------------------
  // An SCC is a subgraph where every vertex can reach every other vertex. An SCC of size
  // > 1 represents a feedback loop (cycle).

  std::vector<int> componentMap(n);
  const auto       vertexIndexMap = boost::get(boost::vertex_index, graph);

  // BGL guarantees a crucial topological property here:
  // If a path exists from u to v, then componentMap[u] >= componentMap[v].
  const int numSccs = boost::strong_components(
      graph, boost::make_iterator_property_map(componentMap.begin(), vertexIndexMap));

  // --- STEP 2: Group vertices by their SCC ID ------------------------------------------
  std::vector<std::vector<VertexDescriptor>> sccVertices(numSccs);
  auto [v_begin, v_end] = boost::vertices(graph);

  for (const auto v : std::ranges::subrange(v_begin, v_end)) {
    const auto scc_id = componentMap[boost::get(vertexIndexMap, v)];
    sccVertices[scc_id].push_back(v);
  }

  std::vector<SimulationBlock> blocks;

  // This vector acts as an accumulator for adjacent acyclic (DAG) components.
  // We want to combine adjacent DAG segments into a single SimulationBlock to minimize
  // execution overhead.
  std::vector<Component_weakPtr> currentDagComps;

  // Helper lambda to finalize and store the accumulated DAG segment.
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
    const auto& verts = sccVertices[i];

    // Check A: If an SCC has multiple vertices it's a cycle.
    bool isCyclic = verts.size() > 1;

    // Check B: If it's a single vertex, it's only a cycle if it has a self-loop.
    if (verts.size() == 1) {
      auto [e_begin, e_end] = boost::out_edges(verts.front(), graph);

      // std::ranges::any_of succinctly checks if any target matches our vertex.
      isCyclic =
          std::ranges::any_of(std::ranges::subrange(e_begin, e_end), [&](const auto& e) {
            return boost::target(e, graph) == verts.front();
          });
    }

    // This view is evaluated on-demand in the if/else block below.
    auto validComps =
        verts | std::views::transform([this](auto v) { return graph[v].component; })
        | std::views::filter([](const auto& weakComp) { return !weakComp.expired(); });

    if (isCyclic) {
      // A cycle interrupts standard execution flow. We must flush any pending
      // DAG components so they execute *before* this cycle block.
      flushDag();

      // C++23 std::ranges::to evaluates the lazy view above.
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

  // Don't forget to flush the final DAG segment (if the graph ended with acyclic parts).
  flushDag();

  return blocks;
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
    if (auto cPtr = graph[v].component.lock()) {
      j["components"].push_back(
          nlohmann::ordered_json{{"id", v},
                                 {"type", cPtr->typeName()},
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

  std::vector<Component_ptr> componentList;

  if (auto it = j.find("components"); it != j.end() && it->is_array()) {
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

      componentList.push_back(std::move(cPtr));
    }
  }

  Circuit result(componentList | std::ranges::to<Component_set>(), true);

  // Transfer ownership
  result.ownedComponents = std::move(componentList);
  result.ownedWires      = wireMap | std::views::values | std::ranges::to<std::vector>();

  if (auto it = j.find("name"); it != j.end() && it->is_string()) {
    result.name = it->get<std::string>();
  }

  return result;
}