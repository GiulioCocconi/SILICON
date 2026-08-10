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
#include <span>
#include <vector>

#include <QPointF>

#include <core/wire.hpp>

class QGraphicsScene;

namespace SILICON {
namespace ui {

class GraphicalWire;
class GraphicalWireSegment;

/** @brief Scene-coordinate wire path produced by an automatic router. */
struct RoutedWire {
  core::Bus            bus;
  std::vector<QPointF> points;
};

/* WireManager
 * Central orchestrator for GraphicalWires and GraphicalWireSegments.
 *
 * Responsibilities:
 *   - Owns all GraphicalWires.
 *   - Merge: when two segments' endpoints collide, their GraphicalWires are
 *     unified. If the segments are collinear (aligned), they are merged into a
 *     single segment. Otherwise, the two segments remain separate but share the
 *     same GraphicalWire (the one with the larger bus size is kept).
 *   - Split: Uses Graph Theory (Breadth-First Search) to group segments into
 *     connected components. If a wire breaks into multiple isolated components,
 *     they are split into new GraphicalWires.
 */

class WireManager {
public:
  WireManager() = default;
  ~WireManager() { clear(); }

  WireManager(const WireManager&)            = delete;
  WireManager& operator=(const WireManager&) = delete;
  WireManager(WireManager&&)                 = delete;
  WireManager& operator=(WireManager&&)      = delete;

  std::shared_ptr<GraphicalWire> createWire(unsigned int busSize = 1);
  void                           removeWire(GraphicalWire* wire);
  [[nodiscard]] const std::vector<std::shared_ptr<GraphicalWire>>& wires() const
  {
    return managedWires;
  }

  // Register an existing segment. If it has no GraphicalWire, a new one is
  // created for it.
  void addSegment(GraphicalWireSegment* segment);

  // Unregister a segment. If its GraphicalWire becomes empty, the wire is
  // removed.
  void removeSegment(GraphicalWireSegment* segment);

  // Called after a segment has been moved (e.g. by point dragging).
  // Checks endpoint collisions with all other segments, performs merge or split
  // as needed, and refreshes junctions across the complete scene.
  void updateSegmentTopology(GraphicalWireSegment* segment);

  // Merge: Unify the GraphicalWires of two segments whose endpoints collide.
  // If the segments are aligned (collinear), they are fused into one segment.
  // Otherwise they stay separate but share the GraphicalWire with the larger
  // bus size.
  void merge(GraphicalWireSegment* a, GraphicalWireSegment* b);

  // Recalculate the junction flags for a specific segment
  void calculateJunctions(GraphicalWireSegment* segment,
                          bool                  includeNeighborhood = true) const;

  // Recalculate all junction flags across every registered segment.
  void calculateJunctions() const;

  // Find a segment whose body or endpoint is at `scenePoint`.
  [[nodiscard]] GraphicalWireSegment*
  segmentAtPoint(QPointF                     scenePoint,
                 const GraphicalWireSegment* ignoredSegment = nullptr) const;

  // Returns a set of segments near the argument (and including it) using Qt's colliding
  // items [O(log n)]
  static std::vector<GraphicalWireSegment*>
  segmentNeighbors(GraphicalWireSegment* segment);

  [[nodiscard]] std::vector<GraphicalWireSegment*> getSegments() const
  {
    return allSegments;
  }

  /** @brief Deletes all managed graphical wire segments from @p scene. */
  void clearSegments(QGraphicsScene& scene);

  /** @brief Replaces all managed graphical wire segments with routed segments. */
  void replaceSegments(QGraphicsScene& scene, std::span<const RoutedWire> routedSegments);

  // Check whether `segment`'s first or last endpoint lies on `other`'s body
  // (or vice-versa). Used for T-junction and connectivity detection.
  [[nodiscard]] static bool segmentsTouching(const GraphicalWireSegment* segment,
                                             const GraphicalWireSegment* other);

  /**
   * @brief Bind an external callback to react to topology events.
   * By binding this, DiagramScene can automatically map new underlying hardware logic.
   */
  void setTopologyChangedCallback(std::function<void()> cb)
  {
    onTopologyChanged = std::move(cb);
  }

  /**
   * @brief Force execute the callback natively mapped to topological splits, merges, or
   * drags.
   */
  void notifyTopologyChanged() const
  {
    if (onTopologyChanged) {
      onTopologyChanged();
    }
  }

  void clear();

private:
  std::vector<std::shared_ptr<GraphicalWire>> managedWires;
  std::vector<GraphicalWireSegment*>          allSegments;

  /** @brief Callback invoked when wire topology changes (splits, merges, drags) */
  std::function<void()> onTopologyChanged;

  // Merge all wires from `src` into `dst`, then destroy `src`.
  void mergeWires(GraphicalWire* dst, GraphicalWire* src);

  // Fuse collinear segments
  void fuseSegments(GraphicalWireSegment* a, GraphicalWireSegment* b);

  // Uses BFS to determine if a wire's segments have become disconnected.
  // Returns true if a split occurred, false otherwise.
  bool evaluateWireSplits(GraphicalWire* wire);
};

}  // namespace ui
}  // namespace SILICON
