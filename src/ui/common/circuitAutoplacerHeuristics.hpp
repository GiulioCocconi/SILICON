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

#include <algorithm>
#include <compare>
#include <cstddef>
#include <span>
#include <utility>

#include <QPointF>
#include <QRectF>

#include <ui/common/portGeometry.hpp>

namespace SILICON::ui::detail {

/** Lexicographic quality used to select a complete autoplacement candidate. */
struct AutoplacementQuality {
  std::size_t overlaps            = 0;
  std::size_t directionViolations = 0;
  std::size_t sharedSegments      = 0;
  std::size_t crossings           = 0;
  double      routingCost         = 0.0;
  double      area                = 0.0;

  auto operator<=>(const AutoplacementQuality&) const = default;
};

/** Distance by which the peer terminal lies behind the outward-facing port. */
inline qreal portDirectionViolationDistance(const PortSide side, const QPointF& endpoint,
                                            const QPointF& peer)
{
  qreal outwardDistance = 0.0;
  switch (side) {
    case PortSide::LEFT: outwardDistance = endpoint.x() - peer.x(); break;
    case PortSide::RIGHT: outwardDistance = peer.x() - endpoint.x(); break;
    case PortSide::UP: outwardDistance = endpoint.y() - peer.y(); break;
    case PortSide::DOWN: outwardDistance = peer.y() - endpoint.y(); break;
  }
  return std::max<qreal>(0.0, -outwardDistance);
}

/** True when a candidate bound retains the requested edge-to-edge clearance. */
inline bool hasBoundsClearance(const QRectF&                 candidate,
                               const std::span<const QRectF> accepted,
                               const qreal                   clearance)
{
  const qreal  halfClearance = clearance / 2.0;
  const QRectF paddedCandidate =
      candidate.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance);
  return std::ranges::none_of(accepted, [&](const QRectF& bounds) {
    return paddedCandidate.intersects(
        bounds.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance));
  });
}

/** True when an item is wholly outside the target on the named target-port side. */
inline bool boundsLieOutsideTarget(const PortSide side, const QRectF& item,
                                   const QRectF& target, const qreal clearance)
{
  switch (side) {
    case PortSide::LEFT: return item.right() <= target.left() - clearance;
    case PortSide::RIGHT: return item.left() >= target.right() + clearance;
    case PortSide::UP: return item.bottom() <= target.top() - clearance;
    case PortSide::DOWN: return item.top() >= target.bottom() + clearance;
  }
  std::unreachable();
}

}  // namespace SILICON::ui::detail
