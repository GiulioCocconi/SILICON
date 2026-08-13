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
#include <span>
#include <vector>

#include <QPointF>
#include <QRectF>

#include <ui/common/portGeometry.hpp>

namespace SILICON::ui::detail {

/** Geometry needed to place one boundary I/O near its connected port. */
struct BoundaryIoLaneItem {
  QPointF       preferredPosition;
  QRectF        preferredBounds;
  QPointF       preferredPort;
  std::uint64_t stableOrder = 0;
};

/** Grid-aligned positions and bounds, indexed like the input items. */
struct BoundaryIoLaneResult {
  std::vector<QPointF> positions;
  std::vector<QRectF>  bounds;
};

/**
 * Places same-side boundary symbols in one aligned collision-free boundary lane.
 *
 * Symbols keep the normal coordinate selected from the circuit boundary. Tangential
 * collisions are resolved with the smallest grid-aligned displacement around their
 * preferred connected ports. If a corner is occupied, the complete row or column is
 * moved outward together so its boundary edge remains aligned.
 */
[[nodiscard]] BoundaryIoLaneResult
placeBoundaryIoInLanes(const PortSide side, std::span<const BoundaryIoLaneItem> items,
                       std::span<const QRectF> fixedObstacles,
                       const qreal componentClearance, const qreal obstaclePadding,
                       const qreal grid);

}  // namespace SILICON::ui::detail
