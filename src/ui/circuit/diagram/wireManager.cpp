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
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <QGraphicsScene>
#include <ui/circuit/diagram/graphicalWire.hpp>
#include <ui/circuit/diagram/wireRouting.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

WireManager::Edit::Edit(WireManager& owner, GraphicalWireSegment* editedSegment)
  : manager(&owner), segment(editedSegment), originalPoints(editedSegment->getPoints()),
    originalBounds(editedSegment->sceneBoundingRect())
{}

WireManager::Edit::~Edit() { cancel(); }

void WireManager::Edit::cancel()
{
  if (!manager)
    return;
  segment->setPoints(std::move(originalPoints));
  manager = nullptr;
}

void WireManager::Edit::commit()
{
  if (!manager)
    return;
  if (!isOrthogonalRoute(segment->getPoints())) {
    cancel();
    throw std::invalid_argument("A committed wire route must be orthogonal");
  }
  if (segment->getPoints() == originalPoints) {
    manager = nullptr;
    return;
  }
  Batch batch(*manager);
  auto& pending = manager->pendingEdits;
  const auto it = std::ranges::find_if(pending, [this](const auto& edit) {
    return edit.first == segment;
  });
  if (it == pending.end())
    pending.emplace_back(segment, originalBounds);
  else
    it->second = it->second.united(originalBounds);
  manager = nullptr;
}

void WireManager::finishBatch()
{
  if (batchDepth > 1) {
    --batchDepth;
    return;
  }
  const bool changed = !pendingEdits.empty() || pendingNotification;
  std::vector<std::shared_ptr<GraphicalWire>> affectedWires;
  for (const auto& [segment, bounds] : pendingEdits) {
    auto* wire = segment->getGraphicalWire();
    const auto found = std::ranges::find_if(affectedWires, [wire](const auto& item) {
      return item.get() == wire;
    });
    if (found != affectedWires.end())
      continue;
    const auto managed = std::ranges::find_if(managedWires, [wire](const auto& item) {
      return item.get() == wire;
    });
    if (managed != managedWires.end())
      affectedWires.push_back(*managed);
  }
  for (const auto& wire : affectedWires)
    evaluateWireSplits(wire.get());
  for (const auto& [segment, bounds] : pendingEdits)
    calculateJunctions(segment);
  for (const auto& [segment, bounds] : pendingEdits)
    calculateJunctionsIn(bounds);
  pendingEdits.clear();
  batchDepth = 0;
  pendingNotification = false;
  if (changed)
    notifyTopologyChanged();
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
  pendingEdits.clear();
  pendingNotification = false;
}

void WireManager::clearSegments(QGraphicsScene& scene)
{
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
}

void WireManager::replaceSegments(QGraphicsScene&             scene,
                                  std::span<const RoutedWire> routedSegments)
{
  clearSegments(scene);

  std::map<uint64_t, std::shared_ptr<GraphicalWire>> wireById;
  uint64_t                                           fallbackWireId = 1;

  auto routeWireId = [&fallbackWireId](const Bus& bus) {
    if (bus.size() > 0 && bus[0])
      return bus[0]->getId();

    return fallbackWireId++;
  };

  for (const RoutedWire& routed : routedSegments) {
    if (routed.points.size() < 2)
      continue;

    auto& wire = wireById[routeWireId(routed.bus)];
    if (!wire) {
      wire = createWire(
          static_cast<unsigned int>(std::max<std::size_t>(routed.bus.size(), 1)));
      if (routed.bus.size() > 0)
        wire->setBus(routed.bus);
    }

    auto* segment = new GraphicalWireSegment(routed.points.front());
    segment->setPoints(routed.points);
    scene.addItem(segment);
    segment->setGraphicalWire(wire.get(), false);
    allSegments.push_back(segment);
  }

  calculateJunctions();
  notifyTopologyChanged();
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

  // Detach all segments from this wire first
  if (!wire->getSegments().empty())
    throw std::logic_error("removeWire() called without detaching the segments first");

  std::erase_if(managedWires, [wire](const auto& uptr) { return uptr.get() == wire; });
}

void WireManager::addSegment(GraphicalWireSegment* segment, const bool updateTopology)
{
  if (!segment)
    throw std::invalid_argument("addSegment() called with null segment");

  if (std::ranges::find(allSegments, segment) != allSegments.end())
    return;
  if (!isOrthogonalRoute(segment->getPoints()))
    throw std::invalid_argument("A committed wire route must be orthogonal");

  allSegments.push_back(segment);

  // If the segment has no wire yet, create one
  if (!segment->getGraphicalWire()) {
    auto wire = createWire(1);
    segment->setGraphicalWire(wire.get(), false);
  }

  if (updateTopology) {
    Batch edit(*this);
    updateSegmentTopology(segment);
    calculateJunctions(segment);
    notifyTopologyChanged();
  }
}

void WireManager::removeSegment(GraphicalWireSegment* segment)
{
  if (!segment)
    throw std::invalid_argument("removeSegment() called with null segment");

  const QRectF oldBounds = segment->sceneBoundingRect();
  std::erase(allSegments, segment);

  auto* wire = segment->getGraphicalWire();
  if (!wire)
    throw std::logic_error("Segment has no associated wire");

  if (wire->empty()) {
    removeWire(wire);
    notifyTopologyChanged();
    return;
  }

  // The wire still has other segments. Removing this segment might have
  // broken the remaining wire into multiple pieces. Evaluate it!
  evaluateWireSplits(wire);
  calculateJunctionsIn(oldBounds);
  notifyTopologyChanged();
}

void WireManager::updateSegmentTopology(GraphicalWireSegment* segment,
                                        const bool allowFusion, const bool allowNetMerge)
{
  if (!segment)
    throw std::invalid_argument("updateSegmentTopology() called with null segment");

  if (segment->empty())
    return;

  bool hasTopologyChanged = false;

  // --- 1. FUSION PHASE ---
  while (allowFusion) {
    const auto neighbors = segmentNeighbors(segment);
    auto it = std::ranges::find_if(neighbors, [&](const GraphicalWireSegment* sibling) {
      return segment != sibling && segment->isAlignedWith(sibling);
    });

    if (it == std::ranges::end(neighbors))
      break;

    fuseSegments(segment, *it);
    hasTopologyChanged = true;
  }

  // --- 2. MERGE PHASE ---
  const auto unmergedNeighbors =
      segmentNeighbors(segment) | std::views::filter([&](auto* sibling) {
        return segment->getGraphicalWire() != sibling->getGraphicalWire();
      })
      | std::ranges::to<std::vector>();

  for (GraphicalWireSegment* sibling : unmergedNeighbors) {
    if (!allowNetMerge)
      break;
    merge(segment, sibling);
    hasTopologyChanged = true;
  }

  // --- 3. SPLIT PHASE ---
  if (GraphicalWire* currentWire = segment->getGraphicalWire()) {
    if (evaluateWireSplits(currentWire))
      hasTopologyChanged = true;
  }

  if (!batchDepth)
    calculateJunctions(segment);

  if (hasTopologyChanged && !batchDepth)
    notifyTopologyChanged();
}

void WireManager::merge(GraphicalWireSegment* a, GraphicalWireSegment* b)
{
  if (!a || !b)
    throw std::invalid_argument("merge() called with null segment");

  auto* wireA = a->getGraphicalWire();
  auto* wireB = b->getGraphicalWire();

  if (!wireA || !wireB)
    throw std::logic_error("Segments must have associated wires for merge");

  // Unaligned path: keep segments separate, but unify their GraphicalWire. The
  // dominant wire is chosen in order to minimize segment's wire changes.
  if (wireA != wireB) {
    const bool aIsDominant = wireA->getSegments().size() >= wireB->getSegments().size();
    mergeWires(aIsDominant ? wireA : wireB, aIsDominant ? wireB : wireA);
  }
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
  std::map<GraphicalWire*, std::vector<std::vector<QPointF>>> routesByWire;
  for (auto* segment : segments) {
    if (!segment || segment->empty() || !segment->getGraphicalWire())
      continue;
    auto* wire = segment->getGraphicalWire();
    if (routesByWire.contains(wire))
      continue;
    auto& routes = routesByWire[wire];
    routes.reserve(wire->getSegments().size());
    for (const auto* member : wire->getSegments()) {
      if (member && !member->empty())
        routes.push_back(member->getScenePoints());
    }
  }
  for (auto* segment : segments) {
    if (!segment || segment->empty() || !segment->getGraphicalWire())
      continue;
    const auto& routes = routesByWire.at(segment->getGraphicalWire());
    segment->setFirstPointJunction(orthogonalRouteIncidentArmCount(
        segment->mapToScene(segment->firstPoint()), routes) >= 3);
    segment->setLastPointJunction(orthogonalRouteIncidentArmCount(
        segment->mapToScene(segment->lastPoint()), routes) >= 3);
  }
}

void WireManager::calculateJunctionsIn(const QRectF& sceneBounds) const
{
  if (allSegments.empty() || !allSegments.front()->scene())
    return;
  std::vector<GraphicalWireSegment*> affected;
  for (auto* item : allSegments.front()->scene()->items(sceneBounds)) {
    auto* segment = category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment);
    if (segment && std::ranges::find(allSegments, segment) != allSegments.end())
      affected.push_back(segment);
  }
  calculateJunctionsFor(affected);
}

bool WireManager::evaluateWireSplits(GraphicalWire* wire)
{
  if (!wire || wire->empty())
    return false;

  // Scene registration order determines which connected component retains the bus.
  auto segments = allSegments | std::views::filter([&](auto* segment) {
                    return segment->getGraphicalWire() == wire;
                  }) | std::ranges::to<std::vector>();

  // A wire with 0 or 1 segments cannot be split
  if (segments.size() <= 1)
    return false;
  const bool useSceneQueries = std::ranges::all_of(segments, [](const auto* segment) {
    return segment->scene() != nullptr;
  });

  // Track the connected components (islands of touching segments)
  std::vector<std::vector<GraphicalWireSegment*>> wireGroups;
  std::unordered_set<GraphicalWireSegment*>       visited;

  // Run Breadth-First Search (BFS) to find all connected groups
  for (auto* seg : segments) {
    if (visited.contains(seg))
      continue;

    std::vector<GraphicalWireSegment*> currentGroup;
    std::queue<GraphicalWireSegment*>  queue;

    queue.push(seg);
    visited.insert(seg);

    while (!queue.empty()) {
      auto* curr = queue.front();
      queue.pop();
      currentGroup.push_back(curr);

      const auto neighbors = useSceneQueries ? segmentNeighbors(curr) : segments;
      for (auto* sibling : neighbors) {
        if (sibling->getGraphicalWire() == wire && !visited.contains(sibling)
            && (useSceneQueries || segmentsTouching(curr, sibling))) {
          visited.insert(sibling);
          queue.push(sibling);
        }
      }
    }
    wireGroups.push_back(currentGroup);
  }

  // If everything is in one group, the wire is perfectly intact.
  if (wireGroups.size() <= 1)
    return false;

  // SPLIT OCCURRED:
  // Component 0 gets to keep the original wire identity.
  // Component 1...N get extracted into brand new wires.
  const unsigned int busSize = wire->getBusSize();

  for (size_t i = 1; i < wireGroups.size(); ++i) {
    auto newWire = createWire(busSize);
    for (auto* seg : wireGroups[i]) {
      wire->removeSegment(seg);
      seg->setGraphicalWire(newWire.get(), false);
    }
  }

  return true;  // Indicate that a split happened
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

void WireManager::mergeWires(GraphicalWire* dst, GraphicalWire* src)
{
  if (!dst || !src)
    throw std::invalid_argument("mergeWires() called with null wire");

  if (dst == src)
    return;

  // Move all segments from src to dst
  const auto segsCopy = src->getSegments();  // copy to avoid iterator invalidation
  for (auto* seg : segsCopy) {
    src->removeSegment(seg);
    seg->setGraphicalWire(dst, false);
  }

  // Destroy the now-empty source wire
  removeWire(src);
}

void WireManager::fuseSegments(GraphicalWireSegment* a, GraphicalWireSegment* b)
{
  if (!a || !b)
    throw std::invalid_argument("fuseSegments() called with null segment");
  if (!a->isAlignedWith(b))
    throw std::logic_error("fuseSegments() called with unaligned segments");

  const QPointF aFirst = a->mapToScene(a->firstPoint());
  const QPointF aLast  = a->mapToScene(a->lastPoint());
  const QPointF bFirst = b->mapToScene(b->firstPoint());
  const QPointF bLast  = b->mapToScene(b->lastPoint());

  const auto& aPts = a->getPoints();
  const auto& bPts = b->getPoints();

  std::vector<QPointF> newPoints;
  newPoints.reserve(aPts.size() + bPts.size() - 1);

  // Helper: Maps B's points to A's coordinates and appends them
  auto addMappedB = [&](const auto& start, const auto& end) {
    std::transform(start, end, std::back_inserter(newPoints),
                   [&](const QPointF& p) { return b->mapToItem(a, p); });
  };

  // Helper: Appends A's points
  auto addA = [&]() { newPoints.insert(newPoints.end(), aPts.begin(), aPts.end()); };

  // Assemble the points in the correct order:
  if (aLast == bFirst) {
    // aLast == bFirst -> append b forward (skip b's first point)
    addA();
    addMappedB(bPts.begin() + 1, bPts.end());
  } else if (aLast == bLast) {
    // aLast == bLast -> append b reversed (skip b's last point)
    addA();
    addMappedB(bPts.rbegin() + 1, bPts.rend());
  } else if (aFirst == bLast) {
    // aFirst == bLast -> prepend b forward (skip b's last point)
    addMappedB(bPts.begin(), bPts.end() - 1);
    addA();
  } else if (aFirst == bFirst) {
    // aFirst == bFirst -> prepend b reversed (skip b's first point)
    addMappedB(bPts.rbegin(), bPts.rend() - 1);
    addA();
  }

  if (newPoints.empty())
    throw std::logic_error("fuseSegments() produced no points");
  a->setPoints(std::move(newPoints));

  // The segment b is no longer needed, it was merged into a. Detach it from
  // topology bookkeeping before deleting it so its destructor cannot re-enter
  // the manager while this update is still in progress.
  auto* const aWire        = a->getGraphicalWire();
  auto* const absorbedWire = b->getGraphicalWire();

  std::erase(allSegments, b);
  if (absorbedWire)
    absorbedWire->removeSegment(b);
  b->detachFromWire();
  delete b;

  if (!absorbedWire || absorbedWire == aWire)
    return;

  if (absorbedWire->empty()) {
    removeWire(absorbedWire);
  } else {
    evaluateWireSplits(absorbedWire);
  }
}

}  // namespace SILICON::ui
