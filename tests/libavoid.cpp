#include <gtest/gtest.h>

#include "libavoid/libavoid.h"
#include "libavoid/qtgeomtypes.h"
#include "ui/common/wireRouting.hpp"

#include <algorithm>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <vector>

namespace {

Avoid::Polygon rectangle(double left, double top, double right, double bottom)
{
  Avoid::Polygon poly(4);
  poly.setPoint(0, Avoid::Point(left, top));
  poly.setPoint(1, Avoid::Point(right, top));
  poly.setPoint(2, Avoid::Point(right, bottom));
  poly.setPoint(3, Avoid::Point(left, bottom));
  return poly;
}

bool segmentCrossesRectInterior(const QPointF& a, const QPointF& b, const QRectF& rect)
{
  const QRectF normalized = rect.normalized();

  if (a.y() == b.y()) {
    const double y = a.y();
    if (y <= normalized.top() || y >= normalized.bottom())
      return false;

    const double minX = std::min(a.x(), b.x());
    const double maxX = std::max(a.x(), b.x());
    return std::max(minX, normalized.left()) < std::min(maxX, normalized.right());
  }

  if (a.x() == b.x()) {
    const double x = a.x();
    if (x <= normalized.left() || x >= normalized.right())
      return false;

    const double minY = std::min(a.y(), b.y());
    const double maxY = std::max(a.y(), b.y());
    return std::max(minY, normalized.top()) < std::min(maxY, normalized.bottom());
  }

  return normalized.contains(a) || normalized.contains(b);
}

void expectNoConsecutiveDuplicates(const std::vector<QPointF>& route)
{
  for (size_t i = 1; i < route.size(); ++i)
    EXPECT_NE(route[i - 1], route[i]);
}

}  // namespace

TEST(LibavoidTest, RoutesAroundObstacle)
{
  Avoid::Router router(Avoid::PolyLineRouting);

  auto obstacle_polygon = rectangle(40.0, -10.0, 60.0, 10.0);
  auto* obstacle = new Avoid::ShapeRef(&router, obstacle_polygon, 1);
  auto* connector =
      new Avoid::ConnRef(&router, Avoid::ConnEnd(Avoid::Point(0.0, 0.0)),
                         Avoid::ConnEnd(Avoid::Point(100.0, 0.0)), 2);

  ASSERT_TRUE(router.processTransaction());
  ASSERT_TRUE(connector->needsRepaint());

  Avoid::PolyLine& route = connector->displayRoute();

  ASSERT_GE(route.size(), 3U);
  EXPECT_TRUE(route.ps.front().equals(Avoid::Point(0.0, 0.0)));
  EXPECT_TRUE(route.ps.back().equals(Avoid::Point(100.0, 0.0)));

  bool deviates_from_blocked_line = false;
  for (const auto& point : route.ps) {
    if (point.y != 0.0) {
      deviates_from_blocked_line = true;
      break;
    }
  }
  EXPECT_TRUE(deviates_from_blocked_line);

  router.deleteConnector(connector);
  router.deleteShape(obstacle);
}

TEST(WireRoutingTest, RoutesAroundBufferedObstacle)
{
  const QPointF start(0.0, 0.0);
  const QPointF end(100.0, 0.0);
  const QRectF  obstacle(QPointF(40.0, -10.0), QPointF(60.0, 10.0));
  const qreal   clearance = 10.0;

  const QRectF bufferedObstacle =
      obstacle.normalized().adjusted(-clearance, -clearance, clearance, clearance);
  const auto route = silicon::routeOrthogonalWire(start, end, {bufferedObstacle});

  ASSERT_GE(route.size(), 3U);
  EXPECT_EQ(route.front(), start);
  EXPECT_EQ(route.back(), end);
  expectNoConsecutiveDuplicates(route);

  for (size_t i = 1; i < route.size(); ++i)
    EXPECT_FALSE(segmentCrossesRectInterior(route[i - 1], route[i], bufferedObstacle));
}

TEST(WireRoutingTest, RoutesVerticalPairsAroundBufferedObstacle)
{
  const QPointF start(0.0, -50.0);
  const QPointF end(0.0, 50.0);
  const QRectF  obstacle(QPointF(-10.0, -10.0), QPointF(10.0, 10.0));
  const qreal   clearance = 10.0;

  const QRectF bufferedObstacle =
      obstacle.normalized().adjusted(-clearance, -clearance, clearance, clearance);
  const auto route = silicon::routeOrthogonalWire(start, end, {bufferedObstacle});

  ASSERT_GE(route.size(), 3U);
  EXPECT_EQ(route.front(), start);
  EXPECT_EQ(route.back(), end);
  expectNoConsecutiveDuplicates(route);
  for (size_t i = 1; i < route.size(); ++i)
    EXPECT_FALSE(segmentCrossesRectInterior(route[i - 1], route[i], bufferedObstacle));
}

TEST(LibavoidQtGeomTypesTest, ConvertsPoint)
{
  const QPointF qt_point(12.5, -7.25);
  const Avoid::Point avoid_point = Avoid::pointFromQPointF(qt_point);

  EXPECT_DOUBLE_EQ(avoid_point.x, qt_point.x());
  EXPECT_DOUBLE_EQ(avoid_point.y, qt_point.y());
  EXPECT_EQ(Avoid::toQPointF(avoid_point), qt_point);
}

TEST(LibavoidQtGeomTypesTest, ConvertsRectangleAndBox)
{
  const QRectF qt_rect(QPointF(10.0, 20.0), QPointF(30.0, 50.0));
  const Avoid::Rectangle avoid_rect = Avoid::rectangleFromQRectF(qt_rect);
  const Avoid::Box avoid_box = avoid_rect.offsetBoundingBox(0.0);

  EXPECT_EQ(Avoid::toQRectF(avoid_box), qt_rect);
}

TEST(LibavoidQtGeomTypesTest, ConvertsPolygon)
{
  const QPolygonF qt_polygon(
      {QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 15.0), QPointF(0.0, 15.0)});

  const Avoid::Polygon avoid_polygon = Avoid::polygonFromQPolygonF(qt_polygon);
  const QPolygonF round_tripped_polygon = Avoid::toQPolygonF(avoid_polygon);

  ASSERT_EQ(avoid_polygon.size(), static_cast<size_t>(qt_polygon.size()));
  EXPECT_EQ(round_tripped_polygon, qt_polygon);
}
