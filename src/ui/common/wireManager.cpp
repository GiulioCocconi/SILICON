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
#include "graphicalWire.hpp"

#include <QDebug>
#include <QLineF>
#include <algorithm>
#include <cassert>
#include <queue>
#include <unordered_set>

WireManager::~WireManager()
{
  for (auto* segment : allSegments) {
    segment->setGraphicalWire(nullptr);
  }

  for (auto wire : managedWires) {
    wire->setManager(nullptr);
  }

  allSegments.clear();
  managedWires.clear();
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
  assert(wire && "removeWire() called with nullptr");

  // Detach all segments from this wire first
  assert(wire->getSegments().empty()
         && "removeWire() called without detaching the segments first");

  std::erase_if(managedWires, [wire](const auto& uptr) { return uptr.get() == wire; });
}

void WireManager::addSegment(GraphicalWireSegment* segment)
{
  assert(segment);

  if (std::ranges::find(allSegments, segment) != allSegments.end())
    return;

  allSegments.push_back(segment);

  // If the segment has no wire yet, create one
  if (!segment->getGraphicalWire()) {
    auto wire = createWire(1);
    segment->setGraphicalWire(wire.get());
  }

  calculateJunctions();
}

void WireManager::removeSegment(GraphicalWireSegment* segment)
{
  qDebug() << "Remove segment!!!";

  assert(segment);

  std::erase(allSegments, segment);

  auto* wire = segment->getGraphicalWire();
  assert(wire);

  if (wire->empty()) {
    removeWire(wire);
    return;
  }

  // The wire still has other segments. Removing this segment might have
  // broken the remaining wire into multiple pieces. Evaluate it!
  evaluateWireSplits(wire);
  calculateJunctions();
}

bool WireManager::pointsAreSame(QPointF a, QPointF b)
{
  return QLineF(a, b).length() <= collisionTolerance;
}

void WireManager::segmentMoved(GraphicalWireSegment* segment)
{
  assert(segment && "segmentMoved() called with null segment");

  if (segment->empty())
    return;

  // --- 1. MERGE PHASE ---
  // If moving this segment makes it touch segments of OTHER wires, merge them.
  bool hasTopologyChanged = false;
  for (GraphicalWireSegment* sibling : allSegments)
    if (segmentsTouching(segment, sibling)) {
      merge(segment, sibling);
      hasTopologyChanged = true;
    }

  // --- 2. SPLIT PHASE ---
  // Did moving this segment break its CURRENT wire into disconnected pieces?
  if (GraphicalWire* currentWire = segment->getGraphicalWire()) {
    if (evaluateWireSplits(currentWire))
      hasTopologyChanged = true;
  }

  if (hasTopologyChanged)
    calculateJunctions();
}

void WireManager::merge(GraphicalWireSegment* a, GraphicalWireSegment* b)
{
  assert(a && b);

  auto* wireA = a->getGraphicalWire();
  auto* wireB = b->getGraphicalWire();

  assert(wireA && wireB);

  // 1. Aligned path: fuse them into a single segment
  if (a->isAlignedWith(b)) {
    fuseSegments(a, b);
    return;
  }

  // 2. Unaligned path: keep segments separate, but unify their GraphicalWire
  if (wireA != wireB) {
    bool aIsDominant = wireA->getBusSize() >= wireB->getBusSize();
    mergeWires(aIsDominant ? wireA : wireB, aIsDominant ? wireB : wireA);
  }
}

void WireManager::calculateJunctions() const
{
  for (auto* segment : allSegments)
    calculateJunctions(segment);
}

void WireManager::calculateJunctions(GraphicalWireSegment* segment) const
{
  if (segment->empty())
    return;

  segment->setFirstPointJunction(false);
  segment->setLastPointJunction(false);

  // For each pair of segments, check whether one's endpoint lies on the other's body.
  // Only endpoints can be junctions.
  const QPointF firstScene = segment->mapToScene(segment->firstPoint());
  const QPointF lastScene  = segment->mapToScene(segment->lastPoint());

  for (const auto* other : allSegments) {
    if (other == segment || other->empty())
      continue;

    // Check if seg's first endpoint lies on other's body
    const QPointF firstLocal = other->mapFromScene(firstScene);
    if (other->isPointOnPath(firstLocal))
      segment->setFirstPointJunction(true);

    // Check if seg's last endpoint lies on other's body
    const QPointF lastLocal = other->mapFromScene(lastScene);
    if (other->isPointOnPath(lastLocal))
      segment->setLastPointJunction(true);
  }
}

bool WireManager::evaluateWireSplits(GraphicalWire* wire)
{
  if (!wire || wire->empty())
    return false;

  const auto segments = wire->getSegments();

  // A wire with 0 or 1 segments cannot be split
  if (segments.size() <= 1)
    return false;

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

      for (auto* sibling : segments) {
        if (!visited.contains(sibling) && segmentsTouching(curr, sibling)) {
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
      seg->setGraphicalWire(newWire.get());
    }
  }

  return true;  // Indicate that a split happened
}

// --- Query -----------------------------------------------------------------------------

GraphicalWireSegment*
WireManager::segmentAtPoint(QPointF                     scenePoint,
                            const GraphicalWireSegment* ignoredSegment) const
{
  for (auto* seg : allSegments) {
    const QPointF localPt = seg->mapFromScene(scenePoint);
    if (seg->isPointOnPath(localPt) && seg != ignoredSegment)
      return seg;
  }
  return nullptr;
}

// --- Helpers ---------------------------------------------------------------------------

bool WireManager::segmentsTouching(const GraphicalWireSegment* segment,
                                   const GraphicalWireSegment* other)
{
  assert(segment && other);

  if (segment == other)
    return false;
  if (segment->empty() || other->empty())
    return false;

  // Check if segment's endpoints lie on other's body
  const QPointF segFirst = segment->mapToScene(segment->firstPoint());
  const QPointF segLast  = segment->mapToScene(segment->lastPoint());

  const QPointF segFirstLocal = other->mapFromScene(segFirst);
  if (other->isPointOnPath(segFirstLocal))
    return true;

  const QPointF segLastLocal = other->mapFromScene(segLast);
  if (other->isPointOnPath(segLastLocal))
    return true;

  // Check if other's endpoints lie on segment's body (the reverse direction)
  const QPointF otherFirst = other->mapToScene(other->firstPoint());
  const QPointF otherLast  = other->mapToScene(other->lastPoint());

  const QPointF otherFirstLocal = segment->mapFromScene(otherFirst);
  if (segment->isPointOnPath(otherFirstLocal))
    return true;

  const QPointF otherLastLocal = segment->mapFromScene(otherLast);
  if (segment->isPointOnPath(otherLastLocal))
    return true;

  return false;
}

void WireManager::mergeWires(GraphicalWire* dst, GraphicalWire* src)
{
  assert(dst && src);

  if (dst == src)
    return;

  // Move all segments from src to dst
  const auto& segsCopy = src->getSegments();  // copy to avoid iterator invalidation
  for (auto* seg : segsCopy) {
    src->removeSegment(seg);
    seg->setGraphicalWire(dst);
  }

  // Destroy the now-empty source wire
  removeWire(src);
}

void WireManager::fuseSegments(GraphicalWireSegment* a, GraphicalWireSegment* b)
{
  assert(a && b);

  const QPointF aFirst = a->mapToScene(a->firstPoint());
  const QPointF aLast  = a->mapToScene(a->lastPoint());
  const QPointF bFirst = b->mapToScene(b->firstPoint());
  const QPointF bLast  = b->mapToScene(b->lastPoint());

  const auto& aPts = a->getPoints();
  const auto& bPts = b->getPoints();

  auto* wireA = a->getGraphicalWire();
  auto* wireB = b->getGraphicalWire();

  assert(wireA && wireB);

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
  if (pointsAreSame(aLast, bFirst)) {
    // aLast == bFirst -> append b forward (skip b's first point)
    addA();
    addMappedB(bPts.begin() + 1, bPts.end());
  } else if (pointsAreSame(aLast, bLast)) {
    // aLast == bLast -> append b reversed (skip b's last point)
    addA();
    addMappedB(bPts.rbegin() + 1, bPts.rend());
  } else if (pointsAreSame(aFirst, bLast)) {
    // aFirst == bLast -> prepend b forward (skip b's last point)
    addMappedB(bPts.begin(), bPts.end() - 1);
    addA();
  } else if (pointsAreSame(aFirst, bFirst)) {
    // aFirst == bFirst -> prepend b reversed (skip b's first point)
    addMappedB(bPts.rbegin(), bPts.rend() - 1);
    addA();
  }

  // If a collision was found and points were merged, clean up 'b'
  if (!newPoints.empty()) {
    a->setPoints(std::move(newPoints));

    wireB->removeSegment(b);
    if (wireB->empty() && wireB != wireA) {
      removeWire(wireB);
    }

    removeSegment(b);
    delete b;
  }
}
