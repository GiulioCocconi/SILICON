#include <gtest/gtest.h>

#include "libavoid/libavoid.h"
#include "libavoid/qtgeomtypes.h"
#include "ui/common/wireRouting.hpp"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace SILICON::core;

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

bool isNativeOrthogonalRoute(const Avoid::Polygon& route)
{
  for (std::size_t i = 1; i < route.size(); ++i) {
    if (route.at(i - 1).x != route.at(i).x && route.at(i - 1).y != route.at(i).y)
      return false;
  }
  return true;
}

void expectNoConsecutiveDuplicates(const std::vector<QPointF>& route)
{
  for (size_t i = 1; i < route.size(); ++i)
    EXPECT_NE(route[i - 1], route[i]);
}

void configureHyperedgeRouter(Avoid::Router& router)
{
  constexpr qreal gridSize = 10.0;
  router.setRoutingPenalty(Avoid::segmentPenalty, 500.0);
  router.setRoutingPenalty(Avoid::fixedSharedPathPenalty, 1'000'000.0);
  router.setRoutingPenalty(Avoid::idealNudgingDistance, gridSize);
  router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, true);
  router.setRoutingOption(Avoid::penaliseOrthogonalSharedPathsAtConnEnds, true);
  router.setRoutingOption(Avoid::nudgeOrthogonalTouchingColinearSegments, true);
  router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
  router.setRoutingOption(Avoid::improveHyperedgeRoutesMovingJunctions, true);
}

std::vector<QPointF> snappedDisplayRoute(Avoid::ConnRef& connector)
{
  constexpr qreal      gridSize = 10.0;
  std::vector<QPointF> points;
  const auto&          route = connector.displayRoute();
  points.reserve(route.size());
  for (std::size_t i = 0; i < route.size(); ++i) {
    const QPointF point = Avoid::toQPointF(route.at(i));
    points.emplace_back(std::round(point.x() / gridSize) * gridSize,
                        std::round(point.y() / gridSize) * gridSize);
  }
  return canonicalizeOrthogonalRoute(std::move(points));
}

Avoid::ConnEnd pinnedTerminal(Avoid::Router& router, const QPointF position,
                              const Avoid::ConnDirFlags direction)
{
  constexpr double size = 10.0;
  QRectF bounds(position.x() - size / 2.0, position.y() - size / 2.0, size, size);
  if (direction == Avoid::ConnDirRight)
    bounds.moveRight(position.x());
  else if (direction == Avoid::ConnDirLeft)
    bounds.moveLeft(position.x());
  else if (direction == Avoid::ConnDirDown)
    bounds.moveBottom(position.y());
  else if (direction == Avoid::ConnDirUp)
    bounds.moveTop(position.y());

  auto                   polygon    = Avoid::rectangleFromQRectF(bounds);
  auto*                  shape      = new Avoid::ShapeRef(&router, polygon);
  constexpr unsigned int pinClassId = 1;
  new Avoid::ShapeConnectionPin(shape, pinClassId, position.x() - bounds.left(),
                                position.y() - bounds.top(), false, 0.0, direction);
  return Avoid::ConnEnd(shape, pinClassId);
}

Avoid::ConnEnd pinnedTerminal(Avoid::ShapeRef* shape, const QRectF& bounds,
                              const QPointF position, const Avoid::ConnDirFlags direction,
                              const unsigned int pinClassId)
{
  new Avoid::ShapeConnectionPin(shape, pinClassId, position.x() - bounds.left(),
                                position.y() - bounds.top(), false, 0.0, direction);
  return Avoid::ConnEnd(shape, pinClassId);
}

}  // namespace

TEST(LibavoidTest, RoutesAroundObstacle)
{
  Avoid::Router router(Avoid::PolyLineRouting);

  auto  obstacle_polygon = rectangle(40.0, -10.0, 60.0, 10.0);
  auto* obstacle         = new Avoid::ShapeRef(&router, obstacle_polygon, 1);
  auto* connector = new Avoid::ConnRef(&router, Avoid::ConnEnd(Avoid::Point(0.0, 0.0)),
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
  const auto route = SILICON::core::routeOrthogonalWire(start, end, {bufferedObstacle});

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
  const auto route = SILICON::core::routeOrthogonalWire(start, end, {bufferedObstacle});

  ASSERT_GE(route.size(), 3U);
  EXPECT_EQ(route.front(), start);
  EXPECT_EQ(route.back(), end);
  expectNoConsecutiveDuplicates(route);
  for (size_t i = 1; i < route.size(); ++i)
    EXPECT_FALSE(segmentCrossesRectInterior(route[i - 1], route[i], bufferedObstacle));
}

TEST(WireRoutingTest, CanonicalizesDuplicateAndCollinearPoints)
{
  const std::vector<QPointF> route = {{0, 0},   {0, 0},   {10, 0}, {20, 0},
                                      {20, 10}, {20, 20}, {30, 20}};

  const auto canonical = SILICON::core::canonicalizeOrthogonalRoute(route);

  const std::vector<QPointF> expected = {{0, 0}, {20, 0}, {20, 20}, {30, 20}};
  EXPECT_EQ(canonical, expected);
  EXPECT_TRUE(SILICON::core::isOrthogonalRoute(canonical));
}

TEST(WireRoutingTest, DetectsOnlyPositiveLengthSharedSegments)
{
  using SILICON::core::orthogonalRoutesShareSegment;

  EXPECT_TRUE(orthogonalRoutesShareSegment(std::vector<QPointF>{{0, 0}, {30, 0}},
                                           std::vector<QPointF>{{10, 0}, {40, 0}}));
  EXPECT_FALSE(orthogonalRoutesShareSegment(std::vector<QPointF>{{0, 0}, {30, 0}},
                                            std::vector<QPointF>{{30, 0}, {50, 0}}));
  EXPECT_FALSE(orthogonalRoutesShareSegment(std::vector<QPointF>{{0, 0}, {30, 0}},
                                            std::vector<QPointF>{{10, -10}, {10, 10}}));
  EXPECT_FALSE(orthogonalRoutesShareSegment(std::vector<QPointF>{{0, 0}, {30, 0}},
                                            std::vector<QPointF>{{0, 10}, {30, 10}}));
}

TEST(WireRoutingTest, DetectsAmbiguousEndpointJunctionsButAllowsCrossovers)
{
  using SILICON::core::orthogonalRoutesIntersect;

  EXPECT_TRUE(orthogonalRoutesIntersect(std::vector<QPointF>{{0, 0}, {30, 0}},
                                        std::vector<QPointF>{{30, 0}, {50, 0}}));
  EXPECT_FALSE(orthogonalRoutesIntersect(std::vector<QPointF>{{0, 0}, {30, 0}},
                                         std::vector<QPointF>{{10, -10}, {10, 10}}));
  EXPECT_TRUE(orthogonalRoutesIntersect(std::vector<QPointF>{{0, 0}, {30, 0}},
                                        std::vector<QPointF>{{30, -10}, {30, 10}}));
  EXPECT_FALSE(orthogonalRoutesIntersect(std::vector<QPointF>{{0, 0}, {30, 0}},
                                         std::vector<QPointF>{{0, 10}, {30, 10}}));
}

TEST(LibavoidHyperedgeTest, RoutesFanoutAsJunctionTree)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  Avoid::ConnEndList terminals = {
      pinnedTerminal(router, QPointF(0, 0), Avoid::ConnDirRight),
      pinnedTerminal(router, QPointF(100, -40), Avoid::ConnDirLeft),
      pinnedTerminal(router, QPointF(100, 40), Avoid::ConnDirLeft),
  };
  const auto index = router.hyperedgeRerouter()->registerHyperedgeForRerouting(terminals);

  ASSERT_TRUE(router.processTransaction());
  const auto changes = router.hyperedgeRerouter()->newAndDeletedObjectLists(index);
  EXPECT_FALSE(changes.newJunctionList.empty());
  EXPECT_GE(changes.newConnectorList.size(), terminals.size());

  std::vector<std::vector<QPointF>> routes;
  for (Avoid::ConnRef* connector : changes.newConnectorList) {
    routes.push_back(snappedDisplayRoute(*connector));
    EXPECT_TRUE(isOrthogonalRoute(routes.back()));
    expectNoConsecutiveDuplicates(routes.back());
  }
  for (std::size_t i = 0; i < routes.size(); ++i) {
    for (std::size_t j = i + 1; j < routes.size(); ++j)
      EXPECT_FALSE(orthogonalRoutesShareSegment(routes[i], routes[j]));
  }
}

TEST(LibavoidHyperedgeTest, ReportsEveryRegisteredNetAfterRouting)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  auto registerFanout = [&](const qreal y) {
    Avoid::ConnEndList terminals = {
        pinnedTerminal(router, QPointF(0, y), Avoid::ConnDirRight),
        pinnedTerminal(router, QPointF(100, y - 20), Avoid::ConnDirLeft),
        pinnedTerminal(router, QPointF(100, y + 20), Avoid::ConnDirLeft),
    };
    return router.hyperedgeRerouter()->registerHyperedgeForRerouting(terminals);
  };

  const auto first  = registerFanout(0);
  const auto second = registerFanout(100);
  ASSERT_TRUE(router.processTransaction());

  const auto firstChanges  = router.hyperedgeRerouter()->newAndDeletedObjectLists(first);
  const auto secondChanges = router.hyperedgeRerouter()->newAndDeletedObjectLists(second);
  EXPECT_FALSE(firstChanges.newConnectorList.empty());
  EXPECT_FALSE(secondChanges.newConnectorList.empty());
}

TEST(LibavoidHyperedgeTest, NudgesDistinctNetsApartAfterGridSnapping)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  const auto firstSource = pinnedTerminal(router, QPointF(0, 0), Avoid::ConnDirRight);
  const auto firstTarget = pinnedTerminal(router, QPointF(100, 0), Avoid::ConnDirLeft);
  auto*      first       = new Avoid::ConnRef(&router, firstSource, firstTarget);
  first->setRoutingType(Avoid::ConnType_Orthogonal);

  const auto secondSource = pinnedTerminal(router, QPointF(20, 0), Avoid::ConnDirRight);
  const auto secondTarget = pinnedTerminal(router, QPointF(80, 0), Avoid::ConnDirLeft);
  auto*      second       = new Avoid::ConnRef(&router, secondSource, secondTarget);
  second->setRoutingType(Avoid::ConnType_Orthogonal);

  ASSERT_TRUE(router.processTransaction());
  Avoid::PolyLine firstSharedRoute;
  firstSharedRoute.ps = {Avoid::Point(0, 0), Avoid::Point(100, 0)};
  first->set_route(firstSharedRoute);
  Avoid::PolyLine secondSharedRoute;
  secondSharedRoute.ps = {Avoid::Point(20, 0), Avoid::Point(80, 0)};
  second->set_route(secondSharedRoute);
  Avoid::ConnRefListVector groups(2);
  groups[0].push_back(first);
  groups[1].push_back(second);
  router.separateOrthogonalRouteGroups(groups, 10.0);
  const auto firstRoute  = snappedDisplayRoute(*first);
  const auto secondRoute = snappedDisplayRoute(*second);
  EXPECT_TRUE(isOrthogonalRoute(firstRoute));
  EXPECT_TRUE(isOrthogonalRoute(secondRoute));
  EXPECT_FALSE(orthogonalRoutesIntersect(firstRoute, secondRoute));
  EXPECT_EQ(firstRoute.front(), QPointF(0, 0));
  EXPECT_EQ(firstRoute.back(), QPointF(100, 0));
  EXPECT_EQ(firstRoute[1].y(), firstRoute.front().y());
  EXPECT_EQ(firstRoute[firstRoute.size() - 2].y(), firstRoute.back().y());
  EXPECT_EQ(secondRoute.front(), QPointF(20, 0));
  EXPECT_EQ(secondRoute.back(), QPointF(80, 0));
}

TEST(LibavoidTest, DirectionalPointEndpointsRemainAtGraphicalPorts)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  const QRectF inputBounds(160, -40, 100, 80);
  const QRectF logicBounds(200, 40, 140, 180);
  auto         inputPolygon = Avoid::rectangleFromQRectF(inputBounds);
  auto         logicPolygon = Avoid::rectangleFromQRectF(logicBounds);
  new Avoid::ShapeRef(&router, inputPolygon);
  new Avoid::ShapeRef(&router, logicPolygon);

  const QPointF source(210, 20);
  const QPointF target(240, 60);
  auto*         connector = new Avoid::ConnRef(
      &router, Avoid::ConnEnd(Avoid::pointFromQPointF(source), Avoid::ConnDirDown),
      Avoid::ConnEnd(Avoid::pointFromQPointF(target), Avoid::ConnDirUp));
  connector->setRoutingType(Avoid::ConnType_Orthogonal);

  ASSERT_TRUE(router.processTransaction());
  const auto route = snappedDisplayRoute(*connector);
  ASSERT_GE(route.size(), 2U);
  EXPECT_TRUE(isNativeOrthogonalRoute(connector->displayRoute()));
  EXPECT_TRUE(isOrthogonalRoute(route));
  EXPECT_EQ(route.front(), source);
  EXPECT_EQ(route.back(), target);
  for (const QPointF& point : route) {
    EXPECT_GE(point.y(), source.y());
    EXPECT_LE(point.y(), target.y());
  }
}

TEST(LibavoidTest, ShapePinRoutesRetainExactPinCoordinates)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  const QRectF sourceBounds(160, -40, 100, 80);
  const QRectF targetBounds(200, 40, 140, 180);
  auto         sourcePolygon = Avoid::rectangleFromQRectF(sourceBounds);
  auto         targetPolygon = Avoid::rectangleFromQRectF(targetBounds);
  auto*        sourceShape   = new Avoid::ShapeRef(&router, sourcePolygon);
  auto*        targetShape   = new Avoid::ShapeRef(&router, targetPolygon);

  const QPointF source(210, 20);
  const QPointF target(240, 60);
  const auto    sourceEnd =
      pinnedTerminal(sourceShape, sourceBounds, source, Avoid::ConnDirDown, 1);
  const auto targetEnd =
      pinnedTerminal(targetShape, targetBounds, target, Avoid::ConnDirUp, 2);
  EXPECT_EQ(Avoid::toQPointF(sourceEnd.position()), source);
  EXPECT_EQ(Avoid::toQPointF(targetEnd.position()), target);
  EXPECT_EQ(sourceEnd.directions(), Avoid::ConnDirDown);
  EXPECT_EQ(targetEnd.directions(), Avoid::ConnDirUp);
  auto* connector = new Avoid::ConnRef(&router, sourceEnd, targetEnd);
  connector->setRoutingType(Avoid::ConnType_Orthogonal);

  ASSERT_TRUE(router.processTransaction());
  ASSERT_GE(connector->route().size(), 2U);
  EXPECT_EQ(Avoid::toQPointF(connector->route().at(0)), source);
  EXPECT_EQ(Avoid::toQPointF(connector->route().at(connector->route().size() - 1)),
            target);

  const auto& displayRoute = connector->displayRoute();
  ASSERT_GE(displayRoute.size(), 2U);
  EXPECT_EQ(Avoid::toQPointF(displayRoute.at(0)), source);
  EXPECT_EQ(Avoid::toQPointF(displayRoute.at(displayRoute.size() - 1)), target);
  EXPECT_TRUE(isNativeOrthogonalRoute(displayRoute));
  for (const Avoid::Point& point : displayRoute.ps) {
    EXPECT_GE(point.y, source.y());
    EXPECT_LE(point.y, target.y());
  }
}

TEST(LibavoidHyperedgeTest, RoutesCrossCoupledLatchWithoutSharedNetSegments)
{
  Avoid::Router router(Avoid::OrthogonalRouting);
  configureHyperedgeRouter(router);

  const QRectF topBounds(20, -20, 100, 40);
  const QRectF bottomBounds(20, 40, 100, 40);
  const QRectF topOutBounds(160, -30, 40, 60);
  const QRectF bottomOutBounds(160, 30, 40, 60);
  auto         topPolygon       = Avoid::rectangleFromQRectF(topBounds);
  auto         bottomPolygon    = Avoid::rectangleFromQRectF(bottomBounds);
  auto         topOutPolygon    = Avoid::rectangleFromQRectF(topOutBounds);
  auto         bottomOutPolygon = Avoid::rectangleFromQRectF(bottomOutBounds);
  auto*        topShape         = new Avoid::ShapeRef(&router, topPolygon);
  auto*        bottomShape      = new Avoid::ShapeRef(&router, bottomPolygon);
  auto*        topOutShape      = new Avoid::ShapeRef(&router, topOutPolygon);
  auto*        bottomOutShape   = new Avoid::ShapeRef(&router, bottomOutPolygon);

  Avoid::ConnEndList firstNet = {
      pinnedTerminal(topShape, topBounds, QPointF(100, 0), Avoid::ConnDirRight, 1),
      pinnedTerminal(bottomShape, bottomBounds, QPointF(40, 50), Avoid::ConnDirLeft, 2),
      pinnedTerminal(topOutShape, topOutBounds, QPointF(180, 0), Avoid::ConnDirLeft, 3),
  };
  Avoid::ConnEndList secondNet = {
      pinnedTerminal(bottomShape, bottomBounds, QPointF(100, 60), Avoid::ConnDirRight, 4),
      pinnedTerminal(topShape, topBounds, QPointF(40, 10), Avoid::ConnDirLeft, 5),
      pinnedTerminal(bottomOutShape, bottomOutBounds, QPointF(180, 60),
                     Avoid::ConnDirLeft, 6),
  };
  const auto first = router.hyperedgeRerouter()->registerHyperedgeForRerouting(firstNet);
  ASSERT_TRUE(router.processTransaction());
  const auto firstChanges = router.hyperedgeRerouter()->newAndDeletedObjectLists(first);
  for (Avoid::JunctionRef* junction : firstChanges.newJunctionList)
    junction->setPositionFixed(true);
  std::vector<std::vector<QPointF>> firstRoutes;
  for (Avoid::ConnRef* connector : firstChanges.newConnectorList) {
    auto route = snappedDisplayRoute(*connector);
    if (route.size() < 2)
      continue;
    if (connector->route().size() >= 2)
      connector->setFixedExistingRoute();
    firstRoutes.push_back(std::move(route));
  }

  const auto second =
      router.hyperedgeRerouter()->registerHyperedgeForRerouting(secondNet);
  ASSERT_TRUE(router.processTransaction());
  const auto secondChanges = router.hyperedgeRerouter()->newAndDeletedObjectLists(second);
  for (Avoid::JunctionRef* junction : secondChanges.newJunctionList)
    junction->setPositionFixed(true);
  std::vector<std::vector<QPointF>> secondRoutes;
  for (Avoid::ConnRef* connector : secondChanges.newConnectorList) {
    auto route = snappedDisplayRoute(*connector);
    if (route.size() < 2)
      continue;
    if (connector->route().size() >= 2)
      connector->setFixedExistingRoute();
    secondRoutes.push_back(std::move(route));
  }

  const QRectF inputBounds(-40, 40, 20, 60);
  auto         inputPolygon = Avoid::rectangleFromQRectF(inputBounds);
  auto*        inputShape   = new Avoid::ShapeRef(&router, inputPolygon);
  const auto   inputSource =
      pinnedTerminal(inputShape, inputBounds, QPointF(-20, 70), Avoid::ConnDirRight, 7);
  const auto inputTarget =
      pinnedTerminal(bottomShape, bottomBounds, QPointF(40, 70), Avoid::ConnDirLeft, 8);
  auto* inputConnector = new Avoid::ConnRef(&router, inputSource, inputTarget);
  inputConnector->setRoutingType(Avoid::ConnType_Orthogonal);
  ASSERT_TRUE(router.processTransaction());

  Avoid::ConnRefListVector routeGroups = {
      firstChanges.newConnectorList,
      secondChanges.newConnectorList,
      Avoid::ConnRefList{inputConnector},
  };
  router.separateOrthogonalRouteGroups(routeGroups, 10.0);
  firstRoutes.clear();
  secondRoutes.clear();
  for (Avoid::ConnRef* connector : firstChanges.newConnectorList)
    firstRoutes.push_back(snappedDisplayRoute(*connector));
  for (Avoid::ConnRef* connector : secondChanges.newConnectorList)
    secondRoutes.push_back(snappedDisplayRoute(*connector));

  for (const auto& firstRoute : firstRoutes) {
    for (const auto& secondRoute : secondRoutes)
      EXPECT_FALSE(orthogonalRoutesShareSegment(firstRoute, secondRoute));
  }

  const auto inputRoute = snappedDisplayRoute(*inputConnector);
  ASSERT_GE(inputRoute.size(), 2U);
  EXPECT_TRUE(
      (inputRoute.front() == QPointF(-20, 70) && inputRoute.back() == QPointF(40, 70))
      || (inputRoute.front() == QPointF(40, 70)
          && inputRoute.back() == QPointF(-20, 70)));
  for (const auto& route : firstRoutes)
    EXPECT_FALSE(orthogonalRoutesShareSegment(route, inputRoute));
  for (const auto& route : secondRoutes)
    EXPECT_FALSE(orthogonalRoutesShareSegment(route, inputRoute));

  const std::array expectedTerminals = {
      QPointF(100, 0),  QPointF(40, 50), QPointF(180, 0),
      QPointF(100, 60), QPointF(40, 10), QPointF(180, 60),
  };
  for (const QPointF terminal : expectedTerminals) {
    const auto routeHasTerminal = [&](const auto& route) {
      return !route.empty() && (route.front() == terminal || route.back() == terminal);
    };
    EXPECT_TRUE(std::ranges::any_of(firstRoutes, routeHasTerminal)
                || std::ranges::any_of(secondRoutes, routeHasTerminal));
  }
}

TEST(WireRoutingTest, MergesFanoutIntoTerminalToJunctionSegments)
{
  const std::vector<std::vector<QPointF>> routes = {
      {{0, 0}, {40, 0}, {40, -20}, {100, -20}},
      {{0, 0}, {70, 0}, {100, 0}},
      {{0, 0}, {70, 0}, {70, 60}, {100, 60}}};

  const auto merged = SILICON::core::mergeOrthogonalRoutes(routes);

  EXPECT_EQ(merged.size(), 5U);
  for (const auto& path : merged)
    EXPECT_TRUE(SILICON::core::isOrthogonalRoute(path));
  EXPECT_TRUE(std::ranges::any_of(merged, [](const auto& path) {
    return path.front() == QPointF(0, 0) && path.back() == QPointF(40, 0);
  }));
  EXPECT_TRUE(std::ranges::any_of(merged, [](const auto& path) {
    return path.front() == QPointF(40, 0) && path.back() == QPointF(70, 0);
  }));
}

TEST(LibavoidQtGeomTypesTest, ConvertsPoint)
{
  const QPointF      qt_point(12.5, -7.25);
  const Avoid::Point avoid_point = Avoid::pointFromQPointF(qt_point);

  EXPECT_DOUBLE_EQ(avoid_point.x, qt_point.x());
  EXPECT_DOUBLE_EQ(avoid_point.y, qt_point.y());
  EXPECT_EQ(Avoid::toQPointF(avoid_point), qt_point);
}

TEST(LibavoidQtGeomTypesTest, ConvertsRectangleAndBox)
{
  const QRectF           qt_rect(QPointF(10.0, 20.0), QPointF(30.0, 50.0));
  const Avoid::Rectangle avoid_rect = Avoid::rectangleFromQRectF(qt_rect);
  const Avoid::Box       avoid_box  = avoid_rect.offsetBoundingBox(0.0);

  EXPECT_EQ(Avoid::toQRectF(avoid_box), qt_rect);
}

TEST(LibavoidQtGeomTypesTest, ConvertsPolygon)
{
  const QPolygonF qt_polygon(
      {QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 15.0), QPointF(0.0, 15.0)});

  const Avoid::Polygon avoid_polygon         = Avoid::polygonFromQPolygonF(qt_polygon);
  const QPolygonF      round_tripped_polygon = Avoid::toQPolygonF(avoid_polygon);

  ASSERT_EQ(avoid_polygon.size(), static_cast<size_t>(qt_polygon.size()));
  EXPECT_EQ(round_tripped_polygon, qt_polygon);
}
