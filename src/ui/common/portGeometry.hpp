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
#include <utility>

#include <QPoint>
#include <QRect>


namespace SILICON {
namespace ui {
enum class PortSide {
  LEFT,
  RIGHT,
  UP,
  DOWN,
};

namespace portGeometryDetail {

struct RectEdges {
  int left;
  int top;
  int right;
  int bottom;
};

[[nodiscard]] inline RectEdges rectEdges(const QRect& rect)
{
  const QRect normalized = rect.normalized();

  // QRect::right()/bottom() are inclusive pixel coordinates, so an 80-pixel
  // rectangle reports 79. Port geometry uses the QGraphicsRectItem boundary
  // instead: x + width and y + height.
  return {.left   = normalized.x(),
          .top    = normalized.y(),
          .right  = normalized.x() + normalized.width(),
          .bottom = normalized.y() + normalized.height()};
}

}  // namespace portGeometryDetail

/**
 * @brief Returns whether a position lies strictly beyond a rectangle boundary.
 */
[[nodiscard]] inline bool isPortPositionOutside(const QPoint& pt, const QRect& rect)
{
  const auto edges = SILICON::ui::portGeometryDetail::rectEdges(rect);
  return pt.x() < edges.left || pt.x() > edges.right || pt.y() < edges.top
         || pt.y() > edges.bottom;
}

/**
 * @brief Finds the nearest rectangle side for an interior or exterior point.
 * @return The side and the point's projection onto that side.
 */
[[nodiscard]] inline std::pair<PortSide, QPoint> nearestPortSide(const QPoint& pt,
                                                                 const QRect&  rect)
{
  const auto edges = SILICON::ui::portGeometryDetail::rectEdges(rect);

  const int leftOverflow   = std::max(0, edges.left - pt.x());
  const int rightOverflow  = std::max(0, pt.x() - edges.right);
  const int topOverflow    = std::max(0, edges.top - pt.y());
  const int bottomOverflow = std::max(0, pt.y() - edges.bottom);

  const int horizontalOverflow = std::max(leftOverflow, rightOverflow);
  const int verticalOverflow   = std::max(topOverflow, bottomOverflow);
  if (horizontalOverflow > 0 || verticalOverflow > 0) {
    if (horizontalOverflow >= verticalOverflow) {
      const auto side = leftOverflow >= rightOverflow ? PortSide::LEFT : PortSide::RIGHT;
      // At a corner, clamp the other coordinate to keep the projection on the
      // rectangle. Otherwise the resulting port line would remain outside it.
      return {side, QPoint(side == PortSide::LEFT ? edges.left : edges.right,
                           std::clamp(pt.y(), edges.top, edges.bottom))};
    }

    const auto side = topOverflow >= bottomOverflow ? PortSide::UP : PortSide::DOWN;
    return {side, QPoint(std::clamp(pt.x(), edges.left, edges.right),
                         side == PortSide::UP ? edges.top : edges.bottom)};
  }

  const int leftDistance   = pt.x() - edges.left;
  const int rightDistance  = edges.right - pt.x();
  const int topDistance    = pt.y() - edges.top;
  const int bottomDistance = edges.bottom - pt.y();
  const int nearest =
      std::min({leftDistance, rightDistance, topDistance, bottomDistance});

  if (nearest == leftDistance)
    return {PortSide::LEFT, QPoint(edges.left, pt.y())};
  if (nearest == rightDistance)
    return {PortSide::RIGHT, QPoint(edges.right, pt.y())};
  if (nearest == topDistance)
    return {PortSide::UP, QPoint(pt.x(), edges.top)};
  return {PortSide::DOWN, QPoint(pt.x(), edges.bottom)};
}

}  // namespace ui
}  // namespace SILICON
