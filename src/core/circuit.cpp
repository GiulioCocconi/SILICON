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
#include "component.hpp"

#include <algorithm>
#include <ranges>

#include <boost/graph/topological_sort.hpp>

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