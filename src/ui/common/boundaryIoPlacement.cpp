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

#include "boundaryIoPlacement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>

namespace SILICON::ui::detail {
namespace {

  bool hasClearance(const QRectF& candidate, std::span<const QRectF> accepted,
                    const qreal clearance)
  {
    const qreal  halfClearance = clearance / 2.0;
    const QRectF paddedCandidate =
        candidate.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance);
    return std::ranges::none_of(accepted, [&](const QRectF& bounds) {
      return paddedCandidate.intersects(
          bounds.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance));
    });
  }

  QPointF outwardStep(const PortSide side, const qreal grid)
  {
    switch (side) {
      case PortSide::LEFT: return {grid, 0.0};
      case PortSide::RIGHT: return {-grid, 0.0};
      case PortSide::UP: return {0.0, grid};
      case PortSide::DOWN: return {0.0, -grid};
    }
    std::unreachable();
  }

  qreal tangentCoordinate(const QPointF& point, const PortSide side)
  {
    return side == PortSide::UP || side == PortSide::DOWN ? point.x() : point.y();
  }

  qreal tangentStart(const QRectF& bounds, const PortSide side)
  {
    return side == PortSide::UP || side == PortSide::DOWN ? bounds.left() : bounds.top();
  }

  qreal tangentSize(const QRectF& bounds, const PortSide side)
  {
    return side == PortSide::UP || side == PortSide::DOWN ? bounds.width()
                                                          : bounds.height();
  }

  QPointF tangentStep(const PortSide side, const qreal distance)
  {
    return side == PortSide::UP || side == PortSide::DOWN ? QPointF{distance, 0.0}
                                                          : QPointF{0.0, distance};
  }

}  // namespace

BoundaryIoLaneResult placeBoundaryIoInLanes(const PortSide                      side,
                                            std::span<const BoundaryIoLaneItem> items,
                                            std::span<const QRectF> fixedObstacles,
                                            const qreal             componentClearance,
                                            const qreal obstaclePadding, const qreal grid)
{
  BoundaryIoLaneResult result{.positions = std::vector<QPointF>(items.size()),
                              .bounds    = std::vector<QRectF>(items.size())};
  if (items.empty())
    return result;

  std::vector<std::size_t> order(items.size());
  std::iota(order.begin(), order.end(), 0);
  std::ranges::sort(order, [&](const std::size_t lhs, const std::size_t rhs) {
    const auto lhsKey = std::pair{tangentCoordinate(items[lhs].preferredPort, side),
                                  items[lhs].stableOrder};
    const auto rhsKey = std::pair{tangentCoordinate(items[rhs].preferredPort, side),
                                  items[rhs].stableOrder};
    return lhsKey < rhsKey;
  });

  // Find the closest non-overlapping positions along the common boundary lane. This is
  // an isotonic regression: after subtracting the mandatory preceding widths and gaps,
  // the remaining coordinates only need to be nondecreasing. Pooling adjacent
  // violations spreads a collision around its preferred port instead of pushing every
  // later symbol in one direction.
  struct Block {
    std::size_t begin;
    std::size_t end;
    qreal       sum;
  };
  std::vector<qreal> offsets(items.size());
  std::vector<qreal> desired(items.size());
  std::vector<Block> blocks;
  for (std::size_t orderedIndex = 0; orderedIndex < order.size(); ++orderedIndex) {
    const std::size_t itemIndex = order[orderedIndex];
    if (orderedIndex != 0) {
      const std::size_t previous = order[orderedIndex - 1];
      offsets[orderedIndex]      = offsets[orderedIndex - 1]
                              + tangentSize(items[previous].preferredBounds, side)
                              + componentClearance;
    }
    desired[orderedIndex] =
        tangentStart(items[itemIndex].preferredBounds, side) - offsets[orderedIndex];
    blocks.push_back({orderedIndex, orderedIndex + 1, desired[orderedIndex]});
    while (blocks.size() >= 2) {
      const Block& previous = blocks[blocks.size() - 2];
      const Block& current  = blocks.back();
      const qreal  previousMean =
          previous.sum / static_cast<qreal>(previous.end - previous.begin);
      const qreal currentMean =
          current.sum / static_cast<qreal>(current.end - current.begin);
      if (previousMean <= currentMean)
        break;
      Block merged{previous.begin, current.end, previous.sum + current.sum};
      blocks.pop_back();
      blocks.back() = merged;
    }
  }

  std::vector<qreal> tangentDeltas(items.size());
  for (const Block& block : blocks) {
    const qreal mean = block.sum / static_cast<qreal>(block.end - block.begin);
    for (std::size_t orderedIndex = block.begin; orderedIndex < block.end;
         ++orderedIndex) {
      const std::size_t itemIndex = order[orderedIndex];
      const qreal       continuousDelta =
          mean + offsets[orderedIndex]
          - tangentStart(items[itemIndex].preferredBounds, side);
      tangentDeltas[itemIndex] = std::round(continuousDelta / grid) * grid;
    }
  }

  // Rounding onto the scene grid can reduce a gap by one grid step. Repair it in the
  // stable tangential order while keeping every component in the same normal lane.
  qreal previousEnd = -std::numeric_limits<qreal>::infinity();
  for (const std::size_t itemIndex : order) {
    const qreal start =
        tangentStart(items[itemIndex].preferredBounds, side) + tangentDeltas[itemIndex];
    if (start < previousEnd + componentClearance) {
      const qreal correction =
          std::ceil((previousEnd + componentClearance - start) / grid) * grid;
      tangentDeltas[itemIndex] += correction;
    }
    previousEnd = tangentStart(items[itemIndex].preferredBounds, side)
                  + tangentDeltas[itemIndex]
                  + tangentSize(items[itemIndex].preferredBounds, side);
  }

  // If another side occupies a corner, move this whole row/column outward together.
  // A uniform normal translation retains the aligned boundary edge.
  const QPointF step      = outwardStep(side, grid);
  const qreal   clearance = std::max(componentClearance, obstaclePadding);
  for (std::size_t lane = 0;; ++lane) {
    const QPointF normalDelta = static_cast<qreal>(lane) * step;
    bool          clear       = true;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const QPointF delta = normalDelta + tangentStep(side, tangentDeltas[i]);
      if (!hasClearance(items[i].preferredBounds.translated(delta), fixedObstacles,
                        clearance)) {
        clear = false;
        break;
      }
    }
    if (!clear)
      continue;

    for (std::size_t i = 0; i < items.size(); ++i) {
      const QPointF delta = normalDelta + tangentStep(side, tangentDeltas[i]);
      result.positions[i] = items[i].preferredPosition + delta;
      result.bounds[i]    = items[i].preferredBounds.translated(delta);
    }
    break;
  }

  return result;
}

}  // namespace SILICON::ui::detail
