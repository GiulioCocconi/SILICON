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
#include <compare>
#include <cstdint>
#include <memory>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#include <QPointF>
#include <QRectF>

#include <core/wire.hpp>

class QGraphicsScene;

namespace SILICON::ui {

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
 *   - Group segments when either endpoint touches another segment's route.
 *   - Split groups when edited geometry separates them.
 */

class WireManager {
public:
  enum class Change : uint8_t { None = 0, Geometry = 1, Connectivity = 2, Electrical = 4 };
  using ChangeCallback = std::function<void(Change)>;
  enum class VertexKind : uint8_t { FreeEndpoint, Junction, Port };
  struct PortReference {
    uint64_t componentId;
    uint32_t portIndex;
    bool output;
    /** @brief Compares references by component identifier, port index, and direction. */
    auto operator<=>(const PortReference&) const = default;
  };
  struct TopologyVertex {
    uint64_t id;
    QPointF position;
    VertexKind kind = VertexKind::FreeEndpoint;
    std::vector<PortReference> ports;
  };
  struct TopologyEdge {
    uint64_t id;
    uint64_t segmentId;
    uint64_t firstVertex;
    uint64_t lastVertex;
    std::vector<QPointF> points;
  };
  using TopologyVertexMap = std::map<uint64_t, TopologyVertex>;
  using TopologyEdgeMap = std::map<uint64_t, TopologyEdge>;
  struct SegmentTopology {
    uint64_t firstVertex = 0;
    uint64_t lastVertex = 0;
    std::vector<std::pair<uint64_t, QPointF>> junctions;
    std::vector<uint64_t> edgeIds;
  };
  struct PortAnchor {
    uint64_t componentId;
    uint32_t portIndex;
    bool output;
    QPointF position;
  };
  using PortAnchorProvider = std::function<std::vector<PortAnchor>()>;
  struct ExplicitContact {
    uint64_t targetSegmentId;
    uint64_t segmentId;
    bool firstEndpoint;
  };

  /** @brief Borrows the topology vertex map until the topology is modified. */
  [[nodiscard]] const TopologyVertexMap& topologyVertices() const { return vertices; }
  /** @brief Borrows the topology edge map until the topology is modified. */
  [[nodiscard]] const TopologyEdgeMap& topologyEdges() const { return edges; }
  /** @brief Borrows segment topology until rebuilt; missing segments return an empty topology. */
  [[nodiscard]] const SegmentTopology& topologyFor(uint64_t segmentId) const;
  /** @brief Sets the provider of current component port anchors in scene coordinates. */
  void setPortAnchors(PortAnchorProvider anchors)
  { portAnchors = std::move(anchors); }
  /** @brief Refreshes port references after components enter or leave the scene. */
  void refreshPortAnchors();
  /** @brief Derives electrical groups, junctions, and port vertices from segment geometry. */
  void rebuildFromGeometry();
  /** A preview edit commits topology once, or restores its original route on cancel. */
  class Edit {
  public:
    /**
     * @brief Captures a registered segment's original local route for a preview edit.
     * @param manager Manager owning the segment.
     * @param segment Non-null registered segment whose route will be edited.
     */
    Edit(WireManager& manager, GraphicalWireSegment* segment);
    /** @brief Cancels an uncommitted edit and suppresses exceptions during cleanup. */
    ~Edit() noexcept;
    /** @brief Disables copying of an edit that owns route restoration. */
    Edit(const Edit&) = delete;
    /** @brief Disables copy assignment of a route edit. */
    Edit& operator=(const Edit&) = delete;
    /**
     * @brief Validates the preview route and queues its topology rebuild.
     * @throws std::invalid_argument If the route is not orthogonal and simple or has fewer than two points.
     * @note Invalid routes are restored; an enclosing batch defers topology reconstruction.
     */
    void commit();
    /** @brief Restores the original local route if the segment is still registered. */
    void cancel();

  private:
    WireManager* manager;
    uint64_t segmentId;
    std::vector<QPointF> originalPoints;
  };

  class Batch {
  public:
    /** @brief Begins a batch that defers topology rebuilds and change notifications. */
    explicit Batch(WireManager& manager) : manager(manager) { ++manager.batchDepth; }
    /** @brief Discards an unfinished batch's pending work without restoring geometry. */
    ~Batch() noexcept { if (active) manager.discardBatch(); }
    /** @brief Disables copying of the batch lifecycle guard. */
    Batch(const Batch&) = delete;
    /** @brief Disables copy assignment of the batch lifecycle guard. */
    Batch& operator=(const Batch&) = delete;
    /** @brief Finishes the batch, rebuilding topology and notifying at the outermost level. */
    void commit()
    {
      if (!active)
        return;
      active = false;
      try {
        manager.finishBatch();
      } catch (...) {
        manager.discardBatch();
        throw;
      }
    }
  private:
    WireManager& manager;
    bool active = true;
  };

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

  // Register an existing segment. A bulk load can defer geometry reconstruction.
  void addSegment(GraphicalWireSegment* segment, bool updateTopology = true);

  // Unregister a segment. If its GraphicalWire becomes empty, the wire is
  // removed.
  void removeSegment(GraphicalWireSegment* segment);
  // Recalculate membership after changing a segment's route.
  void updateSegmentTopology(GraphicalWireSegment* segment);

  /**
   * @brief Rebuilds connectivity after segment positions have been translated.
   * @param translations Segment identifiers and applied scene displacements.
   * @return True after processing the translations.
   */
  [[nodiscard]] bool commitSegmentTranslations(
      std::span<const std::pair<uint64_t, QPointF>> translations);

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

  /** Borrowed until segments are registered or removed. Copy for mutation loops. */
  [[nodiscard]] const std::vector<GraphicalWireSegment*>& getSegments() const
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
   * @brief Sets the callback for geometry, connectivity, and electrical changes.
   * @param cb Callback receiving combined change flags; an empty callback disables delivery.
   */
  void setChangedCallback(ChangeCallback cb)
  {
    onChanged = std::move(cb);
  }

  /**
   * @brief Force execute the callback natively mapped to topological splits, merges, or
   * drags.
   */
  void notifyTopologyChanged() const
  {
    notify(Change::Connectivity);
  }

  /** @brief Notifies listeners of an electrical change, deferring delivery within a batch. */
  void notifyElectricalChanged() const { notify(Change::Electrical); }

  void clear();

private:
  // Parallel edges represent distinct routed pieces between the same contacts.
  using TopologyGraph = boost::adjacency_list<boost::vecS, boost::vecS,
                                             boost::undirectedS, uint64_t, uint64_t>;
  using VertexDescriptor = boost::graph_traits<TopologyGraph>::vertex_descriptor;
  TopologyGraph topologyGraph;
  std::map<uint64_t, VertexDescriptor> vertexDescriptors;

  std::vector<std::shared_ptr<GraphicalWire>> managedWires;
  std::vector<GraphicalWireSegment*>          allSegments;

  /** @brief Callback invoked when wire topology changes (splits, merges, drags) */
  ChangeCallback onChanged;
  unsigned int batchDepth = 0;
  bool rebuildPending = false;
  mutable uint8_t pendingChanges = 0;
  std::vector<uint64_t> pendingEdits;
  TopologyVertexMap vertices;
  TopologyEdgeMap edges;
  std::map<uint64_t, SegmentTopology> segmentTopology;
  uint64_t nextVertexId = 1;
  uint64_t nextEdgeId = 1;
  PortAnchorProvider portAnchors;

  /** @brief Updates endpoint junction flags for the supplied registered segments. */
  void calculateJunctionsFor(std::span<GraphicalWireSegment* const> segments) const;
  /** @brief Finishes one batch level and flushes pending edits and changes at the outermost level. */
  void finishBatch();
  /** @brief Drops one batch level and clears pending work when no batch remains. */
  void discardBatch() noexcept;
  /** @brief Delivers change flags to the callback or accumulates them in an active batch. */
  void notify(Change change) const;
  /** @brief Returns the registered segment with the given identifier, or nullptr. */
  GraphicalWireSegment* registeredSegment(uint64_t id) const;
  /** @brief Creates a topology vertex and attaches matching port anchors. */
  uint64_t createVertex(QPointF position, VertexKind kind = VertexKind::FreeEndpoint);
  /** @brief Rebuilds graph edges by splitting each segment route at its electrical vertices. */
  void rebuildTopologyEdges();
};

}  // namespace SILICON::ui
