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

#include <utility>

#include <ui/circuit/diagram/grid.hpp>
#include <ui/circuit/diagram/portGeometry.hpp>

namespace SILICON::ui {
inline constexpr int EXTERNAL_PORT_MARGIN_GRID = 2;

[[nodiscard]] inline QRect shapeRect(const QSize& size)
{
  return {QPoint(0, 0), size};
}

/** Constrains a port to a grid-aligned, two-grid-unit extension from the rectangle. */
[[nodiscard]] inline QPoint sanitizePortPosition(const QPointF& position,
                                                 const QRect&   rect)
{
  const auto [side, projection] =
      nearestPortSide(gridToPixels(pixelsToNearestGrid(position)), rect);
  const int extension = gridToPixels(EXTERNAL_PORT_MARGIN_GRID);
  switch (side) {
    case PortSide::LEFT: return projection + QPoint(-extension, 0);
    case PortSide::RIGHT: return projection + QPoint(extension, 0);
    case PortSide::UP: return projection + QPoint(0, -extension);
    case PortSide::DOWN: return projection + QPoint(0, extension);
  }
  std::unreachable();
}
}  // namespace SILICON::ui
