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

#include "wireManager.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>


#include <QGraphicsScene>
#include <QLineF>
#include <ui/circuit/diagram/graphicalWire.hpp>
#include <ui/circuit/diagram/wireRouting.hpp>
#include <utils/ranges_wrapper.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

WireManager::Edit::Edit(WireManager& owner, GraphicalWireSegment* editedSegment)
  : manager(&owner), segmentId(editedSegment->getUiId()), originalPoints(editedSegment->getPoints())
{}

WireManager::Edit::~Edit() noexcept
{
  try { cancel(); } catch (...) { /* Destructors must not throw. */ }
}

void WireManager::Edit::cancel()
{
  if (!manager)
    return;
  if (auto* segment = manager->registeredSegment(segmentId))
    segment->setPoints(originalPoints);
  manager = nullptr;
}

void WireManager::Edit::commit()
{
  if (!manager)
    return;
  auto* segment = manager->registeredSegment(segmentId);
  if (!segment) {
    manager = nullptr;
    return;
  }
  if (segment->getPoints().size() < 2 || !isOrthogonalRoute(segment->getPoints())
      || orthogonalRouteSelfIntersects(segment->getPoints())) {
    cancel();
    throw std::invalid_argument("A committed wire route must be orthogonal and simple");
  }
  if (segment->getPoints() == originalPoints) {
    manager = nullptr;
    return;
  }
  Batch batch(*manager);
  auto& pending = manager->pendingEdits;
  if (std::ranges::find(pending, segmentId) == pending.end())
    pending.push_back(segmentId);
  batch.commit();
  manager = nullptr;
}

void WireManager::finishBatch()
{
  if (!batchDepth)
    return;
  if (batchDepth > 1) {
    --batchDepth;
    return;
  }
  const bool geometryChanged = std::exchange(rebuildPending, false) || !pendingEdits.empty();
  if (geometryChanged)
    rebuildFromGeometry();
  pendingEdits.clear();
  batchDepth = 0;
  const auto changes = static_cast<Change>(pendingChanges
      | (geometryChanged ? static_cast<uint8_t>(Change::Geometry) : 0));
  pendingChanges = 0;
  notify(changes);
}

void WireManager::notify(const Change change) const
{
  if (change == Change::None)
    return;
  if (batchDepth) {
    pendingChanges |= static_cast<uint8_t>(change);
    return;
  }
  if (onChanged)
    onChanged(change);
}

void WireManager::discardBatch() noexcept
{
  if (batchDepth)
    --batchDepth;
  if (!batchDepth) {
    pendingEdits.clear();
    rebuildPending = false;
    pendingChanges = 0;
  }
}

GraphicalWireSegment* WireManager::registeredSegment(const uint64_t id) const
{
  const auto it = std::ranges::find_if(allSegments, [id](const auto* segment) {
    return segment->getUiId() == id;
  });
  return it == allSegments.end() ? nullptr : *it;
}

uint64_t WireManager::createVertex(const QPointF position, const VertexKind kind)
{
  const uint64_t id = nextVertexId++;
  TopologyVertex vertex{id, position, kind, {}};
  // Rebuild creates vertices in a batch and refreshPortAnchors binds all ports
  // afterwards. Fetching the entire scene's ports for each vertex is quadratic.
  vertices.emplace(id, vertex);
  return id;
}

const WireManager::SegmentTopology& WireManager::topologyFor(const uint64_t segmentId) const
{
  const auto it = segmentTopology.find(segmentId);
  static const SegmentTopology empty;
  return it == segmentTopology.end() ? empty : it->second;
}

void WireManager::refreshPortAnchors()
{
  const auto anchors = portAnchors ? portAnchors() : std::vector<PortAnchor>{};
  std::map<std::pair<qreal, qreal>, std::vector<PortReference>> portsByPosition;
  for (const auto& anchor : anchors)
    portsByPosition[{anchor.position.x(), anchor.position.y()}].push_back(
        {anchor.componentId, anchor.portIndex, anchor.output});

  std::map<uint64_t, size_t> incidences;
  for (const auto& topology : segmentTopology | std::views::values) {
    ++incidences[topology.firstVertex];
    ++incidences[topology.lastVertex];
    for (const auto& [vertexId, position] : topology.junctions)
      ++incidences[vertexId];
  }

  bool changed = false;
  for (auto& [id, vertex] : vertices) {
    auto ports = portsByPosition[{vertex.position.x(), vertex.position.y()}];
    const auto kind = !ports.empty() ? VertexKind::Port
        : (incidences[id] > 1 ? VertexKind::Junction : VertexKind::FreeEndpoint);
    if (vertex.ports != ports || vertex.kind != kind) {
      vertex.ports = std::move(ports);
      vertex.kind = kind;
      changed = true;
    }
  }
  if (changed) {
    notify(Change::Connectivity);
  }
}

void WireManager::rebuildTopologyEdges()
{
  topologyGraph.clear();
  std::map<uint64_t, VertexDescriptor> descriptors;
  for (const auto id : vertices | std::views::keys)
    descriptors.emplace(id, boost::add_vertex(id, topologyGraph));

  std::map<uint64_t, TopologyEdge> rebuilt;
  for (auto& [segmentId, topology] : segmentTopology) {
    topology.edgeIds.clear();
    const auto* segment = registeredSegment(segmentId);
    if (!segment)
      continue;
    const auto route = segment->getScenePoints();
    if (route.size() < 2)
      continue;
    std::vector<std::pair<uint64_t, QPointF>> ordered{{topology.firstVertex, route.front()}};
    for (const auto [i, endpoints] : route | SILICON::views::adjacent<2>
                                          | SILICON::views::enumerate) {
      const auto& [first, last] = endpoints;
      const std::array span{first, last};
      auto onSpan = topology.junctions | std::views::filter([&](const auto& junction) {
        return junction.second != first
               && core::pointOnOrthogonalRoute(junction.second, span);
      }) | std::ranges::to<std::vector>();
      std::ranges::sort(onSpan, {}, [&](const auto& junction) {
        return QLineF(first, junction.second).length();
      });
      ordered.append_range(onSpan);
      if (static_cast<size_t>(i) + 2 == route.size())
        ordered.emplace_back(topology.lastVertex, route.back());
      else
        ordered.emplace_back(0, last);
    }
    uint64_t startVertex = ordered.front().first;
    std::vector<QPointF> piece{ordered.front().second};
    std::vector<uint64_t> edgeIds;
    for (const auto& [endVertex, point] : ordered | std::views::drop(1)) {
      if (piece.back() != point)
        piece.push_back(point);
      if (!endVertex)
        continue;
      if (piece.size() >= 2) {
        const uint64_t id = nextEdgeId++;
        rebuilt.emplace(id, TopologyEdge{id, segmentId, startVertex, endVertex,
                                         core::canonicalizeOrthogonalRoute(piece)});
        boost::add_edge(descriptors.at(startVertex), descriptors.at(endVertex), id, topologyGraph);
        edgeIds.push_back(id);
      }
      piece = {point};
      startVertex = endVertex;
    }
    topology.edgeIds = std::move(edgeIds);
  }
  edges = std::move(rebuilt);
  vertexDescriptors = std::move(descriptors);
}

void WireManager::rebuildFromGeometry()
{
  Batch batch(*this);
  std::vector<GraphicalWireSegment*> segments = allSegments;
  std::ranges::sort(segments, {}, &GraphicalWireSegment::getUiId);
  // Contacts require a route endpoint. Index horizontal and vertical spans so
  // each endpoint only examines spans on its own coordinates, not every route.
  using PointKey = std::pair<qreal, qreal>;
  struct Span {
    qreal       first, last;
    std::size_t segment;
  };
  std::map<qreal, std::vector<Span>>        horizontal, vertical;
  std::map<PointKey, std::set<std::size_t>> contactsAt;
  std::vector<std::vector<QPointF>>         sceneRoutes;
  sceneRoutes.reserve(segments.size());
  for (std::size_t i = 0; i < segments.size(); ++i) {
    auto points = segments[i]->getScenePoints();
    if (points.size() >= 2) {
      contactsAt[{points.front().x(), points.front().y()}].insert(i);
      contactsAt[{points.back().x(), points.back().y()}].insert(i);
      for (std::size_t j = 1; j < points.size(); ++j) {
        const auto first = points[j - 1], last = points[j];
        if (first.y() == last.y())
          horizontal[first.y()].push_back(
              {std::min(first.x(), last.x()), std::max(first.x(), last.x()), i});
        else
          vertical[first.x()].push_back(
              {std::min(first.y(), last.y()), std::max(first.y(), last.y()), i});
      }
    }
    sceneRoutes.push_back(std::move(points));
  }
  std::vector<std::size_t> components(segments.size());
  std::iota(components.begin(), components.end(), 0);
  auto root = [&components](std::size_t index) {
    while (components[index] != index) {
      components[index] = components[components[index]];
      index             = components[index];
    }
    return index;
  };
  for (auto& [point, touching] : contactsAt) {
    const auto collect = [&](const auto& lines, qreal coordinate, qreal position) {
      if (const auto line = lines.find(coordinate); line != lines.end())
        for (const auto& span : line->second)
          if (span.first <= position && position <= span.last)
            touching.insert(span.segment);
    };
    collect(horizontal, point.second, point.first);
    collect(vertical, point.first, point.second);
    for (const auto index : touching) {
      const auto first = root(*touching.begin()), second = root(index);
      components[std::max(first, second)] = std::min(first, second);
    }
  }
  std::vector<std::vector<GraphicalWireSegment*>> groups(segments.size());
  for (std::size_t i = 0; i < segments.size(); ++i)
    groups[root(i)].push_back(segments[i]);

  std::unordered_set<GraphicalWire*> assigned;
  for (const auto& group : groups) {
    if (group.empty())
      continue;
    GraphicalWire* wire = nullptr;
    for (auto* member : group)
      if (auto* candidate = member->getGraphicalWire();
          candidate && !assigned.contains(candidate)
          && (!wire || candidate->getSegments().size() > wire->getSegments().size()))
        wire = candidate;
    if (!wire)
      wire = createWire(group.front()->getGraphicalWire()
                            ? group.front()->getGraphicalWire()->getBusSize() : 1).get();
    assigned.insert(wire);
    for (auto* member : group)
      member->setGraphicalWire(wire);
  }
  std::erase_if(managedWires, [](const auto& wire) { return wire->empty(); });

  vertices.clear();
  edges.clear();
  topologyGraph.clear();
  vertexDescriptors.clear();
  segmentTopology.clear();
  nextVertexId = 1;
  nextEdgeId = 1;
  std::map<std::pair<qreal, qreal>, uint64_t> vertexAt;
  const auto vertexFor = [&](const QPointF point) {
    const auto key = std::pair{point.x(), point.y()};
    auto& id = vertexAt[key];
    if (!id)
      id = createVertex(point);
    return id;
  };
  for (std::size_t i = 0; i < segments.size(); ++i) {
    const auto& points = sceneRoutes[i];
    if (points.size() < 2)
      continue;
    segmentTopology[segments[i]->getUiId()] = {
        vertexFor(points.front()), vertexFor(points.back()), {}, {}};
  }
  for (const auto& [point, touching] : contactsAt) {
    const QPointF position(point.first, point.second);
    const auto    vertex = vertexAt.at(point);
    for (const auto index : touching) {
      const auto& route = sceneRoutes[index];
      if (position != route.front() && position != route.back())
        segmentTopology.at(segments[index]->getUiId())
            .junctions.emplace_back(vertex, position);
    }
  }
  rebuildTopologyEdges();
  refreshPortAnchors();
  calculateJunctions();
  notifyTopologyChanged();
  batch.commit();
}

bool WireManager::commitSegmentTranslations(
    const std::span<const std::pair<uint64_t, QPointF>> translations)
{
  if (std::ranges::any_of(translations, [](const auto& entry) {
        return !entry.second.isNull();
      }))
    rebuildFromGeometry();
  return true;
}

void WireManager::clear()
{
  // Detach segments without side-effects (avoids creating new wires during
  // teardown, which would leave dangling pointers after managedWires is cleared).
  for (auto* segment : allSegments) {
    segment->detachFromWire();
  }

  for (auto& wire : managedWires) {
    wire->setManager(nullptr);
  }

  allSegments.clear();
  managedWires.clear();
  vertices.clear();
  edges.clear();
  topologyGraph.clear();
  vertexDescriptors.clear();
  segmentTopology.clear();
  pendingEdits.clear();
  rebuildPending = false;
  pendingChanges = 0;
  batchDepth = 0;
}

void WireManager::clearSegments(QGraphicsScene& scene)
{
  pendingEdits.clear();
  rebuildPending = false;
  pendingChanges = 0;
  batchDepth = 0;
  auto segments = std::exchange(allSegments, {});

  for (auto* segment : segments) {
    if (!segment)
      continue;

    scene.removeItem(segment);
    segment->detachFromWire();
    delete segment;
  }

  for (auto& wire : managedWires) {
    wire->setManager(nullptr);
  }

  managedWires.clear();
  vertices.clear();
  edges.clear();
  topologyGraph.clear();
  vertexDescriptors.clear();
  segmentTopology.clear();
}

void WireManager::replaceSegments(QGraphicsScene&             scene,
                                  std::span<const RoutedWire> routedSegments)
{
  clearSegments(scene);
  Batch batch(*this);

  std::map<std::vector<uint64_t>, std::shared_ptr<GraphicalWire>> wireByBus;

  for (const RoutedWire& routed : routedSegments) {
    if (routed.points.size() < 2)
      continue;

    std::vector<uint64_t> key;
    for (const auto& bit : routed.bus)
      key.push_back(bit ? bit->getId() : 0);
    // A bus is its complete ordered bit vector. Sharing the first bit does not
    // make two differently sized or differently ordered buses the same net.
    auto& wire = wireByBus[key];
    if (!wire) {
      wire = createWire(
          static_cast<unsigned int>(std::max<std::size_t>(routed.bus.size(), 1)));
      if (routed.bus.size() > 0)
        wire->setBus(routed.bus);
    }

    auto* segment = new GraphicalWireSegment(routed.points.front());
    segment->setPoints(routed.points);
    scene.addItem(segment);
    segment->setGraphicalWire(wire.get());
    addSegment(segment, false);
  }

  rebuildFromGeometry();
  batch.commit();
}

std::shared_ptr<GraphicalWire> WireManager::createWire(unsigned int busSize)
{
  auto wire = std::make_shared<GraphicalWire>(busSize);
  wire->setManager(this);
  managedWires.push_back(wire);
  return wire;
}

void WireManager::removeWire(GraphicalWire* wire)
{
  if (!wire)
    throw std::invalid_argument("removeWire() called with nullptr");
  if (!wire->getSegments().empty())
    throw std::logic_error("removeWire() called without detaching the segments first");
  std::erase_if(managedWires, [wire](const auto& managed) {
    return managed.get() == wire;
  });
}

void WireManager::addSegment(GraphicalWireSegment* segment, const bool updateTopology)
{
  if (!segment)
    throw std::invalid_argument("addSegment() called with null segment");
  if (std::ranges::find(allSegments, segment) != allSegments.end())
    return;
  if (segment->getPoints().size() < 2 || !isOrthogonalRoute(segment->getPoints())
      || orthogonalRouteSelfIntersects(segment->getPoints()))
    throw std::invalid_argument("A committed wire route must be orthogonal and simple");

  allSegments.push_back(segment);
  if (!segment->getGraphicalWire()) {
    auto wire = createWire(1);
    segment->setGraphicalWire(wire.get());
  }
  if (updateTopology)
    rebuildFromGeometry();
}

void WireManager::removeSegment(GraphicalWireSegment* segment)
{
  if (!segment)
    throw std::invalid_argument("removeSegment() called with null segment");
  std::erase(allSegments, segment);
  std::erase(pendingEdits, segment->getUiId());
  if (batchDepth)
    rebuildPending = true;
  else
    rebuildFromGeometry();
}

void WireManager::updateSegmentTopology(GraphicalWireSegment* segment)
{
  if (!segment)
    throw std::invalid_argument("updateSegmentTopology() called with null segment");
  if (!segment->empty())
    rebuildFromGeometry();
}

void WireManager::calculateJunctions() const
{
  calculateJunctionsFor(allSegments);
}

void WireManager::calculateJunctions(GraphicalWireSegment* segment,
                                     bool                  includeNeighborhood) const
{
  if (!segment)
    throw std::invalid_argument("calculateJunctions() called with null segment");
  if (segment->empty())
    return;

  const auto neighborhood =
      includeNeighborhood ? segmentNeighbors(segment) : std::vector{segment};

  calculateJunctionsFor(neighborhood);
}

void WireManager::calculateJunctionsFor(
    const std::span<GraphicalWireSegment* const> segments) const
{
  for (auto* segment : segments) {
    if (!segment || segment->empty() || !segmentTopology.contains(segment->getUiId()))
      continue;
    const auto& topology = segmentTopology.at(segment->getUiId());
    segment->setFirstPointJunction(
        boost::degree(vertexDescriptors.at(topology.firstVertex), topologyGraph) >= 3);
    segment->setLastPointJunction(
        boost::degree(vertexDescriptors.at(topology.lastVertex), topologyGraph) >= 3);
  }
}

// --- Query -----------------------------------------------------------------------------

GraphicalWireSegment*
WireManager::segmentAtPoint(QPointF                     scenePoint,
                            const GraphicalWireSegment* ignoredSegment) const
{
  if (allSegments.empty() || !allSegments.front()->scene())
    return nullptr;
  const QRectF query(scenePoint - QPointF(5, 5), QSizeF(10, 10));
  for (auto* item : allSegments.front()->scene()->items(query)) {
    auto* seg = category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment);
    if (!seg || seg == ignoredSegment
        || std::ranges::find(allSegments, seg) == allSegments.end())
      continue;
    const QPointF localPt = seg->mapFromScene(scenePoint);
    if (seg->isPointOnPath(localPt))
      return seg;
  }
  return nullptr;
}

// --- Helpers ---------------------------------------------------------------------------

std::vector<GraphicalWireSegment*>
WireManager::segmentNeighbors(GraphicalWireSegment* segment)
{
  if (!segment)
    throw std::invalid_argument("segmentNeighbors() called with null segment");
  if (!segment->scene())
    throw std::logic_error("segmentNeighbors() called on segment not in a scene");

  const auto colliding = segment->scene()->collidingItems(segment);

  auto neighbors =
      colliding | std::views::transform([](QGraphicsItem* item) {
        return category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment);
      })
      | std::views::filter([&](const GraphicalWireSegment* sibling) {
          return sibling && segmentsTouching(segment, sibling);
        })
      | std::ranges::to<std::vector>();

  neighbors.push_back(segment);
  return neighbors;
}

bool WireManager::segmentsTouching(const GraphicalWireSegment* segment,
                                   const GraphicalWireSegment* other)
{
  if (!segment || !other)
    throw std::invalid_argument("segmentsTouching() called with null segment");

  if (segment == other)
    return false;
  if (segment->empty() || other->empty())
    return false;

  return orthogonalRoutesTouch(segment->getScenePoints(), other->getScenePoints());
}

}  // namespace SILICON::ui
