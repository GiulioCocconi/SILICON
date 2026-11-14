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

#include <cmath>

#include <QPoint>
#include <QPointF>
#include <QSize>

namespace SILICON::ui {
inline constexpr int GRID_SIZE = 10;

[[nodiscard]] inline int gridToPixels(int value)
{
  return value * GRID_SIZE;
}
[[nodiscard]] inline int pixelsToGrid(int value)
{
  return value / GRID_SIZE;
}
[[nodiscard]] inline QPoint gridToPixels(const QPoint& point)
{
  return {gridToPixels(point.x()), gridToPixels(point.y())};
}
[[nodiscard]] inline QPoint pixelsToGrid(const QPoint& point)
{
  return {pixelsToGrid(point.x()), pixelsToGrid(point.y())};
}
[[nodiscard]] inline QSize pixelsToGrid(const QSize& size)
{
  return {pixelsToGrid(size.width()), pixelsToGrid(size.height())};
}
[[nodiscard]] inline int pixelsToNearestGrid(qreal value)
{
  return static_cast<int>(std::lround(value / GRID_SIZE));
}
[[nodiscard]] inline QPoint pixelsToNearestGrid(const QPointF& point)
{
  return {pixelsToNearestGrid(point.x()), pixelsToNearestGrid(point.y())};
}
}  // namespace SILICON::ui
