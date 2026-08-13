#include <gtest/gtest.h>

#include "ui/common/boundaryIoPlacement.hpp"

#include <QPointF>
#include <QRectF>
#include <array>

using namespace SILICON::ui;

namespace {

detail::BoundaryIoLaneItem laneItem(const QPointF position, const QRectF bounds,
                                    const QPointF port, const std::uint64_t order)
{
  return {.preferredPosition = position,
          .preferredBounds   = bounds,
          .preferredPort     = port,
          .stableOrder       = order};
}

}  // namespace

TEST(BoundaryIoPlacementTest, KeepsNonOverlappingSymbolsInPreferredLane)
{
  const std::array items = {
      laneItem({0, 0}, {0, 0, 40, 40}, {20, 40}, 1),
      laneItem({80, 0}, {80, 0, 40, 40}, {100, 40}, 2),
  };

  const auto result =
      detail::placeBoundaryIoInLanes(PortSide::DOWN, items, {}, 20.0, 20.0, 10.0);

  EXPECT_EQ(result.positions[0], items[0].preferredPosition);
  EXPECT_EQ(result.positions[1], items[1].preferredPosition);
}

TEST(BoundaryIoPlacementTest, SpreadsOverlapsAlongOneAlignedLane)
{
  const std::array items = {
      laneItem({0, 0}, {-60, 0, 120, 40}, {0, 40}, 1),
      laneItem({110, 0}, {50, 0, 120, 40}, {110, 40}, 2),
  };

  const auto result =
      detail::placeBoundaryIoInLanes(PortSide::DOWN, items, {}, 20.0, 20.0, 10.0);

  EXPECT_EQ(result.positions[0].y(), items[0].preferredPosition.y());
  EXPECT_EQ(result.positions[1].y(), items[1].preferredPosition.y());
  EXPECT_LT(result.positions[0].x(), items[0].preferredPosition.x());
  EXPECT_GT(result.positions[1].x(), items[1].preferredPosition.x());
  EXPECT_FALSE(result.bounds[0]
                   .adjusted(-10, -10, 10, 10)
                   .intersects(result.bounds[1].adjusted(-10, -10, 10, 10)));
}

TEST(BoundaryIoPlacementTest, KeepsACommonNormalCoordinateForClosePorts)
{
  const std::array items = {
      laneItem({0, 0}, {-60, 0, 120, 40}, {0, 40}, 1),
      laneItem({40, 0}, {-20, 0, 120, 40}, {40, 40}, 2),
  };

  const auto result =
      detail::placeBoundaryIoInLanes(PortSide::DOWN, items, {}, 20.0, 20.0, 10.0);

  EXPECT_EQ(result.positions[0].y(), items[0].preferredPosition.y());
  EXPECT_EQ(result.positions[1].y(), items[1].preferredPosition.y());
  EXPECT_LT(result.positions[0].x(), items[0].preferredPosition.x());
  EXPECT_GT(result.positions[1].x(), items[1].preferredPosition.x());
  EXPECT_FALSE(result.bounds[0]
                   .adjusted(-10, -10, 10, 10)
                   .intersects(result.bounds[1].adjusted(-10, -10, 10, 10)));
}

TEST(BoundaryIoPlacementTest, AlignsBothHorizontalAndVerticalSides)
{
  const std::array horizontalItems = {
      laneItem({0, 0}, {0, -60, 40, 120}, {0, 0}, 1),
      laneItem({0, 110}, {0, 50, 40, 120}, {0, 110}, 2),
  };
  const std::array verticalItems = {
      laneItem({0, 0}, {-60, 0, 120, 40}, {0, 0}, 1),
      laneItem({110, 0}, {50, 0, 120, 40}, {110, 0}, 2),
  };

  const auto left  = detail::placeBoundaryIoInLanes(PortSide::LEFT, horizontalItems, {},
                                                    20.0, 20.0, 10.0);
  const auto right = detail::placeBoundaryIoInLanes(PortSide::RIGHT, horizontalItems, {},
                                                    20.0, 20.0, 10.0);
  const auto up =
      detail::placeBoundaryIoInLanes(PortSide::UP, verticalItems, {}, 20.0, 20.0, 10.0);
  const auto down =
      detail::placeBoundaryIoInLanes(PortSide::DOWN, verticalItems, {}, 20.0, 20.0, 10.0);

  EXPECT_EQ(left.positions[0].x(), left.positions[1].x());
  EXPECT_EQ(right.positions[0].x(), right.positions[1].x());
  EXPECT_EQ(up.positions[0].y(), up.positions[1].y());
  EXPECT_EQ(down.positions[0].y(), down.positions[1].y());
  EXPECT_LT(left.positions[0].y(), horizontalItems[0].preferredPosition.y());
  EXPECT_GT(left.positions[1].y(), horizontalItems[1].preferredPosition.y());
  EXPECT_LT(up.positions[0].x(), verticalItems[0].preferredPosition.x());
  EXPECT_GT(up.positions[1].x(), verticalItems[1].preferredPosition.x());
}
