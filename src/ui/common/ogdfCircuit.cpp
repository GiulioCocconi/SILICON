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

#include "ogdfCircuit.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QRectF>

#include <boost/graph/graph_traits.hpp>
#include <boost/range/iterator_range.hpp>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/layered/FastHierarchyLayout.h>
#include <ogdf/layered/SugiyamaLayout.h>

#include <core/circuit.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  QPointF orientedLayerPoint(const double x, const double y,
                             const GraphLayoutDirection direction)
  {
    switch (direction) {
      case GraphLayoutDirection::TopToBottom: return {x, y};
      case GraphLayoutDirection::LeftToRight: return {y, x};
    }
    std::unreachable();
  }

}  // namespace

GraphLayout::PlacementMap
GraphLayout::compute(const Circuit&                            circuit,
                     std::span<GraphicalLogicComponent* const> components,
                     const GraphLayoutOptions&                 options)
{
  ogdf::Graph graph;

  const auto& circuitGraph = circuit.getGraph();
  const auto  vertexCount  = boost::num_vertices(circuitGraph);

  std::vector<ogdf::node> vertexToNode(vertexCount, nullptr);
  std::unordered_map<const Component*, GraphicalLogicComponent*> componentToGraphics;
  std::unordered_map<ogdf::node, GraphicalLogicComponent*>       nodeToGraphics;

  for (GraphicalLogicComponent* component : components) {
    if (!component || !component->getComponent())
      continue;

    componentToGraphics.emplace(component->getComponent().get(), component);
  }

  for (auto vertex : boost::make_iterator_range(boost::vertices(circuitGraph))) {
    const auto& component = circuitGraph[vertex].component;
    if (!component)
      continue;

    auto graphicsIt = componentToGraphics.find(component.get());
    if (graphicsIt == componentToGraphics.end())
      continue;

    ogdf::node node                                = graph.newNode();
    vertexToNode[static_cast<std::size_t>(vertex)] = node;
    nodeToGraphics.emplace(node, graphicsIt->second);
  }

  if (graph.numberOfNodes() == 0)
    return {};

  for (auto edge : boost::make_iterator_range(boost::edges(circuitGraph))) {
    const auto sourceVertex = boost::source(edge, circuitGraph);
    const auto targetVertex = boost::target(edge, circuitGraph);
    if (sourceVertex == targetVertex)
      continue;

    ogdf::node source = vertexToNode[static_cast<std::size_t>(sourceVertex)];
    ogdf::node target = vertexToNode[static_cast<std::size_t>(targetVertex)];
    if (!source || !target)
      continue;

    graph.newEdge(source, target);
  }

  ogdf::GraphAttributes attributes(graph, ogdf::GraphAttributes::nodeGraphics
                                              | ogdf::GraphAttributes::edgeGraphics);

  const bool horizontalLayers = options.algorithm == GraphLayoutAlgorithm::Layered
                                && options.direction == GraphLayoutDirection::LeftToRight;
  for (ogdf::node node : graph.nodes) {
    const QRectF bounds = nodeToGraphics.at(node)->sceneBoundingRect();
    attributes.width(node) =
        std::max(static_cast<double>(horizontalLayers ? options.fallbackNodeHeight
                                                      : options.fallbackNodeWidth),
                 horizontalLayers ? bounds.height() : bounds.width());
    attributes.height(node) =
        std::max(static_cast<double>(horizontalLayers ? options.fallbackNodeWidth
                                                      : options.fallbackNodeHeight),
                 horizontalLayers ? bounds.width() : bounds.height());
  }

  if (options.algorithm == GraphLayoutAlgorithm::ForceDirected) {
    ogdf::FMMMLayout layout;
    layout.useHighLevelOptions(true);
    layout.unitEdgeLength(options.layerDistance);
    layout.minDistCC(options.connectedComponentDistance);
    layout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
    layout.newInitialPlacement(true);
    layout.randSeed(options.randomSeed);
    layout.call(attributes);
  } else {
    ogdf::SugiyamaLayout layout;
    layout.arrangeCCs(true);
    layout.minDistCC(options.connectedComponentDistance);
    if (options.deterministic) {
      layout.runs(1);
      layout.permuteFirst(false);
    }

    auto* hierarchyLayout = new ogdf::FastHierarchyLayout();
    hierarchyLayout->nodeDistance(options.nodeDistance);
    hierarchyLayout->layerDistance(options.layerDistance);
    hierarchyLayout->fixedLayerDistance(true);
    layout.setLayout(hierarchyLayout);
    layout.call(attributes);
  }

  double minSceneLeft = std::numeric_limits<double>::infinity();
  double minSceneTop  = std::numeric_limits<double>::infinity();

  for (ogdf::node node : graph.nodes) {
    const QPointF layoutPoint =
        options.algorithm == GraphLayoutAlgorithm::Layered
            ? orientedLayerPoint(attributes.x(node), attributes.y(node),
                                 options.direction)
            : QPointF(attributes.x(node), attributes.y(node));
    const double x = layoutPoint.x();
    const double y = layoutPoint.y();
    const double sceneWidth =
        horizontalLayers ? attributes.height(node) : attributes.width(node);
    const double sceneHeight =
        horizontalLayers ? attributes.width(node) : attributes.height(node);
    minSceneLeft = std::min(minSceneLeft, x - sceneWidth / 2.0);
    minSceneTop  = std::min(minSceneTop, y - sceneHeight / 2.0);
  }

  PlacementMap placements;
  placements.reserve(static_cast<std::size_t>(graph.numberOfNodes()));

  for (ogdf::node node : graph.nodes) {
    GraphicalLogicComponent* component = nodeToGraphics.at(node);
    const QRectF             bounds    = component->sceneBoundingRect();
    const QPointF            centerOffset =
        bounds.isValid() ? bounds.center() - component->pos() : QPointF(0.0, 0.0);

    const QPointF layoutPoint =
        options.algorithm == GraphLayoutAlgorithm::Layered
            ? orientedLayerPoint(attributes.x(node), attributes.y(node),
                                 options.direction)
            : QPointF(attributes.x(node), attributes.y(node));
    const double  x = layoutPoint.x();
    const double  y = layoutPoint.y();
    const QPointF arrangedCenter(options.origin.x() + x - minSceneLeft,
                                 options.origin.y() + y - minSceneTop);

    placements.emplace(component,
                       DiagramScene::snapToGrid(arrangedCenter - centerOffset));
  }

  return placements;
}

}  // namespace SILICON::ui
