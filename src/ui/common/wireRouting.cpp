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

#include "wireRouting.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "libavoid/libavoid.h"
#include "libavoid/qtgeomtypes.h"

namespace SILICON::core {

namespace {

  QPointF nearestPointOutside(const QPointF point, const QRectF& rect)
  {
    const QRectF normalized = rect.normalized();
    if (!normalized.contains(point))
      return point;

    struct Candidate {
      QPointF point;
      qreal   distanceSquared;
    };

    const std::array candidates = {
        Candidate{QPointF(normalized.left(), point.y()),
                  (point.x() - normalized.left()) * (point.x() - normalized.left())},
        Candidate{QPointF(normalized.right(), point.y()),
                  (point.x() - normalized.right()) * (point.x() - normalized.right())},
        Candidate{QPointF(point.x(), normalized.top()),
                  (point.y() - normalized.top()) * (point.y() - normalized.top())},
        Candidate{QPointF(point.x(), normalized.bottom()),
                  (point.y() - normalized.bottom()) * (point.y() - normalized.bottom())},
    };

    const auto closest =
        std::ranges::min_element(candidates, {}, &Candidate::distanceSquared);
    return closest->point;
  }

  std::vector<QPointF> removeConsecutiveDuplicates(std::vector<QPointF> points)
  {
    points.erase(std::ranges::unique(points).begin(), points.end());
    return points;
  }

}  // namespace

std::vector<QPointF> routeOrthogonalWire(const QPointF start, const QPointF end,
                                         const std::vector<QRectF>& obstacleRects)
{
  if (start == end)
    return {start};

  Avoid::Router router(Avoid::OrthogonalRouting);

  QPointF routedStart = start;
  QPointF routedEnd   = end;

  for (const QRectF& obstacleRect : obstacleRects) {
    const QRectF normalized = obstacleRect.normalized();
    routedStart             = nearestPointOutside(routedStart, normalized);
    routedEnd               = nearestPointOutside(routedEnd, normalized);
  }

  std::vector<Avoid::Rectangle> obstacles;
  obstacles.reserve(obstacleRects.size());

  for (const QRectF& obstacleRect : obstacleRects) {
    const QRectF normalized = obstacleRect.normalized();
    if (normalized.isEmpty())
      continue;

    obstacles.push_back(Avoid::rectangleFromQRectF(normalized));
  }

  std::vector<Avoid::ShapeRef*> shapeRefs;
  shapeRefs.reserve(obstacles.size());
  unsigned int nextId = 1;

  for (auto& obstacle : obstacles) {
    shapeRefs.push_back(new Avoid::ShapeRef(&router, obstacle, nextId++));
  }

  auto* connector =
      new Avoid::ConnRef(&router, Avoid::ConnEnd(Avoid::pointFromQPointF(routedStart)),
                         Avoid::ConnEnd(Avoid::pointFromQPointF(routedEnd)), nextId);
  connector->setRoutingType(Avoid::ConnType_Orthogonal);

  if (!router.processTransaction())
    return {};

  const Avoid::PolyLine& route = connector->displayRoute();
  if (route.size() < 2)
    return {};

  std::vector<QPointF> routedPoints;
  routedPoints.reserve(route.size());

  for (size_t i = 0; i < route.size(); ++i) {
    const QPointF point = Avoid::toQPointF(route.at(i));
    if (routedPoints.empty() || routedPoints.back() != point)
      routedPoints.push_back(point);
  }

  if (routedPoints.empty())
    return {};

  if (routedPoints.front() != routedStart)
    routedPoints.insert(routedPoints.begin(), routedStart);

  if (routedStart != start)
    routedPoints.insert(routedPoints.begin(), start);

  if (routedPoints.back() != routedEnd)
    routedPoints.push_back(routedEnd);

  if (routedEnd != end)
    routedPoints.push_back(end);

  return removeConsecutiveDuplicates(std::move(routedPoints));
}

}  // namespace SILICON::core
