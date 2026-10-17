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

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "interactiveWireRouter.hpp"

namespace SILICON::ui {

/** Applies predictable, grid-aligned edits to an existing wire polyline. */
class LocalWireEditor {
public:
  /**
   * @brief Borrows the original geometry for local wire edits.
   * @param components Component obstacles and ports in scene coordinates.
   * @param segments Wire routes and electrical contacts in scene coordinates.
   * @param gridSize Grid spacing; nonpositive values disable snapping.
   * @note The referenced component and segment storage must outlive this editor.
   */
  LocalWireEditor(std::span<const RoutingComponent> components,
                  std::span<const RoutingSegment> segments, qreal gridSize);

  /**
   * @brief Adjusts wire endpoints attached to moved component ports.
   * @param moves Component identifiers and destination bounds in scene coordinates.
   * @return Complete snapshot routes, or no value if the local edit is invalid.
   */
  [[nodiscard]] RoutePreview
  moveComponents(std::span<const ComponentRouteMove> moves) const;

  /**
   * @brief Moves an interior bend and adjusts electrically attached branches.
   * @param segmentId Identifier of the segment to edit.
   * @param index Interior point index in the original route.
   * @param position Destination in scene coordinates, snapped to the grid.
   * @return Complete snapshot routes, or no value if the local edit is invalid.
   */
  [[nodiscard]] RoutePreview
  moveBend(uint64_t segmentId, std::size_t index, QPointF position) const;

  /**
   * @brief Offsets an edge perpendicular to its direction, preserving endpoints.
   * @param segmentId Identifier of the segment to edit.
   * @param index Index of the edge's first point.
   * @param displacement Offset in scene coordinates, snapped to the grid.
   * @return Complete snapshot routes, or no value if the local edit is invalid.
   */
  [[nodiscard]] RoutePreview
  moveEdge(uint64_t segmentId, std::size_t index, QPointF displacement) const;

  /**
   * @brief Checks route geometry, obstacles, and preservation of electrical contacts.
   * @param routes Candidate routes; omitted segments retain their original geometry.
   * @param editedSegment Edited segment identifier, or zero for component moves.
   * @return Whether the candidates satisfy the shared local and automatic routing checks.
   */
  [[nodiscard]] bool valid(std::span<const RoutedSegment> routes,
                           uint64_t editedSegment) const;

private:
  /** @brief Snaps a scene point to the grid when the grid spacing is positive. */
  [[nodiscard]] QPointF snap(QPointF point) const;
  /** @brief Returns the borrowed segment with the given identifier, or nullptr. */
  [[nodiscard]] const RoutingSegment* find(uint64_t id) const;
  /**
   * @brief Adjusts one endpoint while keeping its route orthogonal.
   * @param segment Original route to edit.
   * @param first Whether to move the first endpoint instead of the last.
   * @param destination New endpoint position in scene coordinates.
   * @return Adjusted points, or no value if the original route has fewer than two points.
   */
  [[nodiscard]] static std::optional<std::vector<QPointF>>
  moveAttachedEndpoint(const RoutingSegment& segment, bool first,
                       QPointF destination);
  /**
   * @brief Canonicalizes and validates edits, then fills in unchanged snapshot routes.
   * @param editedId Edited segment identifier, or zero for component moves.
   * @param routes Candidate route changes.
   * @return Complete snapshot routes, or no value if validation fails.
   */
  [[nodiscard]] RoutePreview
  finish(uint64_t editedId, std::vector<RoutedSegment> routes) const;

  std::span<const RoutingComponent> components;
  std::span<const RoutingSegment> segments;
  qreal gridSize;
};

}  // namespace SILICON::ui
