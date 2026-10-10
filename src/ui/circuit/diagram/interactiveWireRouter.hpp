/*
  Copyright (c) 2026. Giulio Cocconi
  ... (license omitted for brevity) ...
*/

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <QPointF>
#include <QRectF>

namespace SILICON::ui {

struct RoutingComponent {
  uint64_t             id;
  QRectF               bounds;
  std::vector<QPointF> ports;
};

struct RoutingSegment {
  uint64_t                                  id;
  uint64_t                                  net;
  std::vector<QPointF>                      points;
  std::vector<std::pair<uint64_t, QPointF>> electricalVertices{};
};

struct RoutedSegment {
  uint64_t             id;
  std::vector<QPointF> points;
};

using RoutePreview = std::optional<std::vector<RoutedSegment>>;

struct ComponentRouteMove {
  uint64_t id;
  QRectF   bounds;
};

/** A gesture-local snapshot. Each preview is routed from the same original geometry. */
class InteractiveWireRouter {
public:
  /**
   * @brief Owns a snapshot used as the starting geometry for every routing preview.
   * @param components Component obstacles and ports in scene coordinates.
   * @param segments Original wire routes and electrical contacts in scene coordinates.
   * @param gridSize Grid spacing used to align routed geometry.
   */
  InteractiveWireRouter(std::vector<RoutingComponent> components,
                        std::vector<RoutingSegment>   segments, 
                        qreal                         gridSize);

  /**
   * @brief Routes wires around moved components while preserving their connections.
   * @param moves Component identifiers and destination bounds in scene coordinates.
   * @return Snapshot routes for the preview, or no value if routing fails.
   */
  [[nodiscard]] RoutePreview
  routeComponents(std::span<const ComponentRouteMove> moves) const;

  /**
   * @brief Moves an interior bend and routes electrically attached branches.
   * @param segmentId Identifier of the segment to edit.
   * @param bendIndex Interior point index in the original route.
   * @param position Destination in scene coordinates.
   * @return Snapshot routes for the preview, or no value if routing fails.
   */
  [[nodiscard]] RoutePreview
  routeWireBend(uint64_t segmentId, std::size_t bendIndex, QPointF position) const;

  /**
   * @brief Routes a wire with one endpoint moved to a new position.
   * @param segmentId Identifier of the segment to edit.
   * @param endpointIndex Zero for the first point, or the last point index.
   * @param position Destination in scene coordinates.
   * @return Snapshot routes for the preview, or no value if routing fails.
   */
  [[nodiscard]] RoutePreview
  routeWireEndpoint(uint64_t segmentId, std::size_t endpointIndex, QPointF position) const;

  /**
   * @brief Moves an edge perpendicular to its original direction.
   * @param segmentId Identifier of the segment to edit.
   * @param edgeIndex Index of the edge's first point in the original route.
   * @param displacement Offset in scene coordinates; the parallel component is ignored.
   * @return Snapshot routes for the preview, or no value if routing fails.
   */
  [[nodiscard]] RoutePreview
  routeWireEdge(uint64_t segmentId, std::size_t edgeIndex, QPointF displacement) const;

private:
  enum class Handle { None, Bend, Edge, Endpoint };

  struct Request {
    Handle                              handle    = Handle::None;
    uint64_t                            segmentId = 0;
    std::size_t                         index     = 0;
    QPointF                             value;
    std::span<const ComponentRouteMove> moves;
  };

  /**
   * @brief Routes a validated request with libavoid and checks the resulting geometry.
   * @param request Component moves or a wire handle edit relative to the snapshot.
   * @return Preview routes, or no value for an invalid request or route.
   */
  [[nodiscard]] RoutePreview routeWithLibavoid(Request request) const;

  std::vector<RoutingComponent> components;
  std::vector<RoutingSegment>   segments;
  qreal                         gridSize;
};

}  // namespace SILICON::ui
