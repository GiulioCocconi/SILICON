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
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <utility>
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

}  // namespace

void configureOrthogonalRouter(Avoid::Router& router, const qreal gridSize)
{
  Q_ASSERT(gridSize > 0);

  // A shared run between different logical nets is ambiguous. Give it a prohibitive
  // cost and nudge overlapping connectors by at least one complete grid lane so
  // snapping cannot collapse them back onto the same coordinates.
  router.setRoutingPenalty(Avoid::segmentPenalty, 500.0);
  router.setRoutingPenalty(Avoid::fixedSharedPathPenalty, 1'000'000.0);
  router.setRoutingPenalty(Avoid::idealNudgingDistance, gridSize);

  router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, true);
  router.setRoutingOption(Avoid::improveHyperedgeRoutesMovingJunctions, false);
  router.setRoutingOption(Avoid::penaliseOrthogonalSharedPathsAtConnEnds, true);
  router.setRoutingOption(Avoid::nudgeOrthogonalTouchingColinearSegments, true);
  router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
}

std::vector<QPointF> extractOrthogonalRoute(Avoid::ConnRef& connector,
                                            const qreal     gridSize)
{
  Q_ASSERT(gridSize > 0);

  // Vendored libavoid guarantees that its post-processed orthogonal display route is
  // still terminal-complete and orthogonal. Quantize it to the editor grid and remove
  // redundant vertices in one place for every routing workflow.
  const auto& route = connector.displayRoute();
  if (route.size() < 2)
    return {};

  auto snapCoordinate = [gridSize](const qreal value) {
    return std::round(value / gridSize) * gridSize;
  };

  std::vector<QPointF> points;
  points.reserve(route.size());
  for (std::size_t i = 0; i < route.size(); ++i) {
    const QPointF point = Avoid::toQPointF(route.at(i));
    points.emplace_back(snapCoordinate(point.x()), snapCoordinate(point.y()));
  }

  points = canonicalizeOrthogonalRoute(std::move(points));
  Q_ASSERT(isOrthogonalRoute(points));
  return points;
}

bool isOrthogonalRoute(const std::span<const QPointF> points)
{
  for (std::size_t i = 1; i < points.size(); ++i) {
    if (points[i - 1].x() != points[i].x() && points[i - 1].y() != points[i].y())
      return false;
  }
  return true;
}

bool orthogonalRoutesShareSegment(const std::span<const QPointF> first,
                                  const std::span<const QPointF> second)
{
  auto segmentsOverlap = [](const QPointF& a, const QPointF& b, const QPointF& c,
                            const QPointF& d) {
    const bool firstHorizontal  = a.y() == b.y();
    const bool secondHorizontal = c.y() == d.y();
    if (firstHorizontal != secondHorizontal)
      return false;

    if (firstHorizontal) {
      if (a.y() != c.y())
        return false;
      const auto firstMin  = std::min(a.x(), b.x());
      const auto firstMax  = std::max(a.x(), b.x());
      const auto secondMin = std::min(c.x(), d.x());
      const auto secondMax = std::max(c.x(), d.x());
      return std::max(firstMin, secondMin) < std::min(firstMax, secondMax);
    }

    if (a.x() != c.x())
      return false;
    const auto firstMin  = std::min(a.y(), b.y());
    const auto firstMax  = std::max(a.y(), b.y());
    const auto secondMin = std::min(c.y(), d.y());
    const auto secondMax = std::max(c.y(), d.y());
    return std::max(firstMin, secondMin) < std::min(firstMax, secondMax);
  };

  for (std::size_t i = 1; i < first.size(); ++i) {
    for (std::size_t j = 1; j < second.size(); ++j) {
      if (segmentsOverlap(first[i - 1], first[i], second[j - 1], second[j]))
        return true;
    }
  }
  return false;
}

bool orthogonalRoutesIntersect(const std::span<const QPointF> first,
                               const std::span<const QPointF> second)
{
  auto segmentsCreateJunction = [&](const QPointF& a, const QPointF& b, const QPointF& c,
                                    const QPointF& d) {
    const bool firstHorizontal  = a.y() == b.y();
    const bool secondHorizontal = c.y() == d.y();
    if (firstHorizontal && secondHorizontal) {
      return a.y() == c.y()
             && std::max(std::min(a.x(), b.x()), std::min(c.x(), d.x()))
                    <= std::min(std::max(a.x(), b.x()), std::max(c.x(), d.x()));
    }
    if (!firstHorizontal && !secondHorizontal) {
      return a.x() == c.x()
             && std::max(std::min(a.y(), b.y()), std::min(c.y(), d.y()))
                    <= std::min(std::max(a.y(), b.y()), std::max(c.y(), d.y()));
    }

    const QPointF& horizontalStart = firstHorizontal ? a : c;
    const QPointF& horizontalEnd   = firstHorizontal ? b : d;
    const QPointF& verticalStart   = firstHorizontal ? c : a;
    const QPointF& verticalEnd     = firstHorizontal ? d : b;
    const QPointF  intersection(verticalStart.x(), horizontalStart.y());
    const bool     liesOnBoth =
        intersection.x() >= std::min(horizontalStart.x(), horizontalEnd.x())
        && intersection.x() <= std::max(horizontalStart.x(), horizontalEnd.x())
        && intersection.y() >= std::min(verticalStart.y(), verticalEnd.y())
        && intersection.y() <= std::max(verticalStart.y(), verticalEnd.y());
    if (!liesOnBoth)
      return false;
    return intersection == first.front() || intersection == first.back()
           || intersection == second.front() || intersection == second.back();
  };

  for (std::size_t i = 1; i < first.size(); ++i) {
    for (std::size_t j = 1; j < second.size(); ++j) {
      if (segmentsCreateJunction(first[i - 1], first[i], second[j - 1], second[j]))
        return true;
    }
  }
  return false;
}

std::vector<QPointF> canonicalizeOrthogonalRoute(std::vector<QPointF> points)
{
  points.erase(std::ranges::unique(points).begin(), points.end());
  for (std::size_t i = 1; i + 1 < points.size();) {
    const QPointF& previous   = points[i - 1];
    const QPointF& current    = points[i];
    const QPointF& next       = points[i + 1];
    const bool     vertical   = previous.x() == current.x() && current.x() == next.x();
    const bool     horizontal = previous.y() == current.y() && current.y() == next.y();
    if (vertical || horizontal) {
      points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
  return points;
}

std::vector<std::vector<QPointF>>
mergeOrthogonalRoutes(const std::vector<std::vector<QPointF>>& routes)
{
  using Point = std::pair<int, int>;
  using Edge  = std::pair<Point, Point>;

  auto pointKey = [](const QPointF& point) {
    return Point{static_cast<int>(point.x()), static_cast<int>(point.y())};
  };
  auto edgeKey = [](Point a, Point b) {
    if (b < a)
      std::swap(a, b);
    return Edge{a, b};
  };
  auto pointOnSegment = [](const Point& point, const Edge& segment) {
    const auto& [a, b] = segment;
    if (a.first == b.first)
      return point.first == a.first && point.second >= std::min(a.second, b.second)
             && point.second <= std::max(a.second, b.second);
    return point.second == a.second && point.first >= std::min(a.first, b.first)
           && point.first <= std::max(a.first, b.first);
  };

  std::vector<Edge> sourceSegments;
  for (const auto& route : routes) {
    for (std::size_t i = 1; i < route.size(); ++i) {
      const Point a = pointKey(route[i - 1]);
      const Point b = pointKey(route[i]);
      if (a != b)
        sourceSegments.push_back(edgeKey(a, b));
    }
  }

  std::set<Edge> edges;
  for (std::size_t i = 0; i < sourceSegments.size(); ++i) {
    const Edge&     segment = sourceSegments[i];
    std::set<Point> cuts    = {segment.first, segment.second};

    for (std::size_t j = 0; j < sourceSegments.size(); ++j) {
      if (i == j)
        continue;
      const Edge& other = sourceSegments[j];
      if (pointOnSegment(other.first, segment))
        cuts.insert(other.first);
      if (pointOnSegment(other.second, segment))
        cuts.insert(other.second);

      const bool vertical      = segment.first.first == segment.second.first;
      const bool otherVertical = other.first.first == other.second.first;
      if (vertical != otherVertical) {
        const Point intersection = vertical
                                       ? Point{segment.first.first, other.first.second}
                                       : Point{other.first.first, segment.first.second};
        if (pointOnSegment(intersection, segment) && pointOnSegment(intersection, other))
          cuts.insert(intersection);
      }
    }

    std::vector<Point> ordered(cuts.begin(), cuts.end());
    if (segment.first.first == segment.second.first) {
      std::ranges::sort(ordered, {}, [](const Point& point) { return point.second; });
    }
    for (std::size_t j = 1; j < ordered.size(); ++j)
      edges.insert(edgeKey(ordered[j - 1], ordered[j]));
  }

  std::map<Point, std::set<Point>> adjacency;
  for (const auto& [a, b] : edges) {
    adjacency[a].insert(b);
    adjacency[b].insert(a);
  }

  std::set<Edge>                    visited;
  std::vector<std::vector<QPointF>> result;
  auto appendPath = [&](const Point& start, const Point& firstNeighbor) {
    std::vector<QPointF> path     = {QPointF(start.first, start.second)};
    Point                previous = start;
    Point                current  = firstNeighbor;
    visited.insert(edgeKey(previous, current));

    while (true) {
      path.emplace_back(current.first, current.second);
      if (adjacency.at(current).size() != 2)
        break;
      const auto& neighbors = adjacency.at(current);
      const Point next      = *std::ranges::find_if(
          neighbors, [&](const Point& candidate) { return candidate != previous; });
      const Edge nextEdge = edgeKey(current, next);
      if (visited.contains(nextEdge))
        break;
      previous = current;
      current  = next;
      visited.insert(nextEdge);
    }
    result.push_back(canonicalizeOrthogonalRoute(std::move(path)));
  };

  for (const auto& [point, neighbors] : adjacency) {
    if (neighbors.size() == 2)
      continue;
    for (const Point& neighbor : neighbors) {
      if (!visited.contains(edgeKey(point, neighbor)))
        appendPath(point, neighbor);
    }
  }
  // A closed same-net loop has no non-degree-two start. Preserve it deterministically.
  for (const Edge& edge : edges) {
    if (!visited.contains(edge))
      appendPath(edge.first, edge.second);
  }
  return result;
}

std::vector<QPointF> routeOrthogonalWire(const QPointF start, const QPointF end,
                                         const std::vector<QRectF>& obstacleRects,
                                         const qreal                gridSize)
{
  if (start == end)
    return {start};

  Avoid::Router router(Avoid::OrthogonalRouting);
  configureOrthogonalRouter(router, gridSize);

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

  auto routedPoints = extractOrthogonalRoute(*connector, gridSize);

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

  return canonicalizeOrthogonalRoute(std::move(routedPoints));
}

}  // namespace SILICON::core
