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

#include <span>
#include <vector>

#include <QPointF>
#include <QRectF>

namespace Avoid {
class ConnRef;
class Router;
}  // namespace Avoid

namespace SILICON::core {

/**
 * @brief Applies SILICON's shared orthogonal-routing policy to a libavoid router.
 *
 * Both interactive wire previews and full-circuit autoplacement use this policy so
 * segment costs, shared-path avoidance, and nudging remain consistent.
 */
void configureOrthogonalRouter(Avoid::Router& router, qreal gridSize);

/** @brief Extracts, grid-snaps, and canonicalizes a libavoid display route. */
[[nodiscard]] std::vector<QPointF> extractOrthogonalRoute(Avoid::ConnRef& connector,
                                                          qreal           gridSize);

/** @brief Returns whether every consecutive route segment is axis-aligned. */
[[nodiscard]] bool isOrthogonalRoute(std::span<const QPointF> points);

/** @brief Returns whether @p point lies on an orthogonal polyline. */
[[nodiscard]] bool pointOnOrthogonalRoute(QPointF point, std::span<const QPointF> route);

/** @brief Returns whether an endpoint of either route lies on the other route. */
[[nodiscard]] bool orthogonalRoutesTouch(std::span<const QPointF> first,
                                         std::span<const QPointF> second);

/** @brief Returns true when two orthogonal polylines share a positive-length segment. */
[[nodiscard]] bool orthogonalRoutesShareSegment(std::span<const QPointF> first,
                                                std::span<const QPointF> second);

/** @brief Returns true when two routes overlap or form an ambiguous endpoint junction. */
[[nodiscard]] bool orthogonalRoutesIntersect(std::span<const QPointF> first,
                                             std::span<const QPointF> second);

/** @brief Counts strict, perpendicular interior crossings between two routes. */
[[nodiscard]] std::size_t orthogonalRouteCrossingCount(std::span<const QPointF> first,
                                                       std::span<const QPointF> second);

/** @brief Counts the distinct orthogonal arms incident to @p point. */
[[nodiscard]] std::size_t
orthogonalRouteIncidentArmCount(QPointF                                  point,
                                const std::vector<std::vector<QPointF>>& routes);

/** @brief Removes consecutive duplicates and redundant collinear interior points. */
[[nodiscard]] std::vector<QPointF>
canonicalizeOrthogonalRoute(std::vector<QPointF> points);

/**
 * @brief Unions several routes of one net into maximal terminal-to-junction paths.
 *
 * Shared and intersecting segments are split into a graph, deduplicated, then joined
 * through degree-two vertices. Returned path endpoints are terminals or junctions.
 */
[[nodiscard]] std::vector<std::vector<QPointF>>
mergeOrthogonalRoutes(const std::vector<std::vector<QPointF>>& routes);

[[nodiscard]] std::vector<QPointF>
routeOrthogonalWire(QPointF start, QPointF end, const std::vector<QRectF>& obstacleRects,
                    qreal gridSize);

}  // namespace SILICON::core
