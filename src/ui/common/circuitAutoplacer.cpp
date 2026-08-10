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

#include "circuitAutoplacer.hpp"

#include "utils/ranges_wrapper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QRectF>

#include <libavoid/libavoid.h>
#include <libavoid/qtgeomtypes.h>

#include <core/circuit.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/common/wireRouting.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  using ComponentMap   = std::unordered_map<const Component*, GraphicalLogicComponent*>;
  using OrientationMap = IoOrientationMap;
  constexpr double ComponentClearance = 2.0 * DiagramScene::GRID_SIZE;

  QRectF movedComponentObstacle(GraphicalLogicComponent* component,
                                const QPointF& newPosition, const int padding,
                                const OrientationMap* orientations = nullptr)
  {
    const QPointF offset      = newPosition - component->pos();
    GraphicalIO*  ioComponent = category_cast<GraphicalIO>(component, ItemCategory::IO);
    QRectF        localBounds =
        (orientations && orientations->contains(ioComponent))
                   ? ioComponent->collisionRectForPortSide(orientations->at(ioComponent))
                   : component->collisionRectForWires();
    return component->mapToScene(localBounds)
        .boundingRect()
        .translated(offset)
        .adjusted(-padding, -padding, padding, padding);
  }

  QPointF movedPortPosition(GraphicalLogicComponent* component, const Port* port,
                            const QPointF&        newPosition,
                            const OrientationMap* orientations = nullptr)
  {
    GraphicalIO* ioComponent = category_cast<GraphicalIO>(component, ItemCategory::IO);
    QPoint       localPosition =
        (orientations && orientations->contains(ioComponent))
                  ? ioComponent->portPositionFor(orientations->at(ioComponent))
                  : port->getPosition();

    return component->mapToScene(localPosition) + (newPosition - component->pos());
  }

  bool hasComponentClearance(const QRectF& candidate, std::span<const QRectF> accepted)
  {
    const double halfClearance = ComponentClearance / 2.0;
    const QRectF paddedCandidate =
        candidate.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance);
    return std::ranges::none_of(accepted, [&](const QRectF& bounds) {
      return paddedCandidate.intersects(
          bounds.adjusted(-halfClearance, -halfClearance, halfClearance, halfClearance));
    });
  }

  std::vector<GraphicalLogicComponent*>
  orderedComponents(const GraphLayout::PlacementMap& placements, const bool ios)
  {
    std::vector<GraphicalLogicComponent*> result;
    for (const auto& component : placements | std::views::keys) {
      if (hasCategory(component, ItemCategory::IO) == ios)
        result.push_back(component);
    }
    std::ranges::sort(result, [](const auto* lhs, const auto* rhs) {
      if (lhs->getUiId() != rhs->getUiId())
        return lhs->getUiId() < rhs->getUiId();
      return lhs < rhs;
    });
    return result;
  }

  QPointF nearestClearPosition(GraphicalLogicComponent* component,
                               const QPointF&           preferredPosition,
                               std::span<const QRectF>  accepted)
  {
    const int grid = DiagramScene::GRID_SIZE;
    for (int ring = 0;; ++ring) {
      std::optional<std::pair<double, QPointF>> best;
      for (int dx = -ring; dx <= ring; ++dx) {
        const int        dyMagnitude = ring - std::abs(dx);
        const std::array dys =
            dyMagnitude == 0 ? std::array{0, 0} : std::array{dyMagnitude, -dyMagnitude};
        for (const int dy : dys) {
          const QPointF position = preferredPosition + QPointF(dx * grid, dy * grid);
          const QRectF  bounds   = movedComponentObstacle(component, position, 0);
          if (!hasComponentClearance(bounds, accepted))
            continue;

          QRectF occupied = bounds;
          for (const QRectF& other : accepted)
            occupied = occupied.united(other);
          const double area = occupied.width() * occupied.height();
          if (!best || area < best->first
              || (area == best->first
                  && std::pair{position.y(), position.x()}
                         < std::pair{best->second.y(), best->second.x()}))
            best = std::pair{area, position};
        }
      }
      if (best)
        return DiagramScene::snapToGrid(best->second);
    }
  }

  void separateLogicComponents(GraphLayout::PlacementMap& placements)
  {
    std::vector<QRectF> accepted;
    for (GraphicalLogicComponent* component : orderedComponents(placements, false)) {
      QPointF& position = placements.at(component);
      position          = nearestClearPosition(component, position, accepted);
      accepted.push_back(movedComponentObstacle(component, position, 0));
    }
  }

  void separateBoundaryIos(GraphLayout::PlacementMap& placements,
                           const OrientationMap&      orientations)
  {
    std::vector<QRectF> accepted;
    for (GraphicalLogicComponent* component : orderedComponents(placements, false))
      accepted.push_back(movedComponentObstacle(component, placements.at(component), 0));

    const int                                     grid = DiagramScene::GRID_SIZE;
    std::map<PortSide, std::vector<GraphicalIO*>> grouped;
    for (GraphicalLogicComponent* component : orderedComponents(placements, true)) {
      GraphicalIO* ioComponent = category_cast<GraphicalIO>(component, ItemCategory::IO);

      if (orientations.contains(ioComponent)) {
        grouped[orientations.at(ioComponent)].push_back(ioComponent);
        continue;
      }

      placements.at(ioComponent) =
          nearestClearPosition(ioComponent, placements.at(ioComponent), accepted);
      accepted.push_back(
          movedComponentObstacle(ioComponent, placements.at(ioComponent), 0));
    }

    for (auto& [side, ios] : grouped) {
      const bool horizontal = side == PortSide::UP || side == PortSide::DOWN;
      std::ranges::sort(ios, [&](GraphicalIO* first, GraphicalIO* second) {
        const QPointF& firstPosition  = placements.at(first);
        const QPointF& secondPosition = placements.at(second);
        const qreal    firstAxis  = horizontal ? firstPosition.x() : firstPosition.y();
        const qreal    secondAxis = horizontal ? secondPosition.x() : secondPosition.y();
        return std::pair{firstAxis, first->getUiId()}
               < std::pair{secondAxis, second->getUiId()};
      });

      QRectF               preferredBounds;
      bool                 hasPreferredBounds = false;
      std::vector<QPointF> candidates;
      candidates.reserve(ios.size());
      QRectF previousBounds;
      bool   hasPreviousBounds = false;
      for (GraphicalIO* io : ios) {
        const QPointF preferred = placements.at(io);
        const QRectF  preferredIoBounds =
            movedComponentObstacle(io, preferred, 0, &orientations);
        preferredBounds = hasPreferredBounds ? preferredBounds.united(preferredIoBounds)
                                             : preferredIoBounds;
        hasPreferredBounds = true;

        QPointF candidate = preferred;
        QRectF  bounds    = movedComponentObstacle(io, candidate, 0, &orientations);
        if (hasPreviousBounds) {
          const qreal overlap =
              horizontal ? previousBounds.right() + ComponentClearance - bounds.left()
                         : previousBounds.bottom() + ComponentClearance - bounds.top();
          if (overlap > 0.0) {
            const qreal displacement =
                std::ceil(overlap / static_cast<qreal>(grid)) * grid;
            if (horizontal)
              candidate.rx() += displacement;
            else
              candidate.ry() += displacement;
            bounds = movedComponentObstacle(io, candidate, 0, &orientations);
          }
        }
        candidates.push_back(DiagramScene::snapToGrid(candidate));
        previousBounds    = bounds;
        hasPreviousBounds = true;
      }

      QRectF packedBounds;
      bool   hasPackedBounds = false;
      for (std::size_t i = 0; i < ios.size(); ++i) {
        const QRectF bounds =
            movedComponentObstacle(ios[i], candidates[i], 0, &orientations);
        packedBounds    = hasPackedBounds ? packedBounds.united(bounds) : bounds;
        hasPackedBounds = true;
      }
      const qreal recenter = DiagramScene::snapToGrid(
          horizontal ? preferredBounds.center().x() - packedBounds.center().x()
                     : preferredBounds.center().y() - packedBounds.center().y());
      for (QPointF& candidate : candidates) {
        if (horizontal)
          candidate.rx() += recenter;
        else
          candidate.ry() += recenter;
      }

      QPointF outwardStep;
      switch (side) {
        case PortSide::LEFT: outwardStep = QPointF(grid, 0); break;
        case PortSide::RIGHT: outwardStep = QPointF(-grid, 0); break;
        case PortSide::UP: outwardStep = QPointF(0, grid); break;
        case PortSide::DOWN: outwardStep = QPointF(0, -grid); break;
      }

      for (int distance = 0;; ++distance) {
        std::vector<QRectF> groupBounds;
        groupBounds.reserve(ios.size());
        bool clear = true;
        for (std::size_t i = 0; i < ios.size(); ++i) {
          const QPointF candidate = candidates[i] + distance * outwardStep;
          const QRectF  bounds =
              movedComponentObstacle(ios[i], candidate, 0, &orientations);
          if (!hasComponentClearance(bounds, accepted)) {
            clear = false;
            break;
          }
          groupBounds.push_back(bounds);
        }
        if (!clear)
          continue;
        for (std::size_t i = 0; i < ios.size(); ++i) {
          placements.at(ios[i]) =
              DiagramScene::snapToGrid(candidates[i] + distance * outwardStep);
          accepted.push_back(groupBounds[i]);
        }
        break;
      }
    }
  }

  bool componentsHaveClearance(const GraphLayout::PlacementMap& placements,
                               const OrientationMap&            orientations)
  {
    std::vector<QRectF> accepted;
    accepted.reserve(placements.size());
    for (const auto& [component, position] : placements) {
      const QRectF bounds = movedComponentObstacle(component, position, 0, &orientations);
      if (!hasComponentClearance(bounds, accepted))
        return false;
      accepted.push_back(bounds);
    }
    return true;
  }

  // --- Logic / Graphical Port Mapping Helpers ------------------------------------------

  struct RoutableConnection {
    GraphicalLogicComponent* source;
    GraphicalLogicComponent* target;
    const Port*              sourcePort;
    const Port*              targetPort;
    Bus                      bus;
  };

  std::optional<RoutableConnection>
  resolveConnection(const CircuitConnection& connection,
                    const ComponentMap&      componentToGraphics)
  {
    // Skip incomplete logical edges early; every downstream helper assumes both ends
    // exist and are represented in the placement result.
    if (!connection.source || !connection.target)
      return std::nullopt;

    auto sourceGraphicsIt = componentToGraphics.find(connection.source.get());
    auto targetGraphicsIt = componentToGraphics.find(connection.target.get());

    if (sourceGraphicsIt == componentToGraphics.end()
        || targetGraphicsIt == componentToGraphics.end())
      return std::nullopt;

    GraphicalLogicComponent* sourceGraphics = sourceGraphicsIt->second;
    GraphicalLogicComponent* targetGraphics = targetGraphicsIt->second;

    const auto sourcePorts = sourceGraphics->getOutputPorts();
    const auto targetPorts = targetGraphics->getInputPorts();

    // The circuit edge stores bus indices; resolve them back into actual graphical ports
    // only if both components still expose those indices.
    if (connection.sourceBusIndex >= sourcePorts.size()
        || connection.targetBusIndex >= targetPorts.size())
      return std::nullopt;

    return RoutableConnection{.source     = sourceGraphics,
                              .target     = targetGraphics,
                              .sourcePort = sourcePorts[connection.sourceBusIndex],
                              .targetPort = targetPorts[connection.targetBusIndex],
                              .bus        = connection.bus};
  }

  ComponentMap buildComponentMap(const GraphLayout::PlacementMap& placements)
  {
    ComponentMap result;
    result.reserve(placements.size());
    for (const auto& placement : placements) {
      auto* component = placement.first;
      if (component && component->getComponent())
        result.emplace(component->getComponent().get(), component);
    }
    return result;
  }

  ComponentMap buildComponentMap(std::span<GraphicalLogicComponent* const> components)
  {
    ComponentMap result;
    result.reserve(components.size());
    for (GraphicalLogicComponent* component : components) {
      if (component && component->getComponent())
        result.emplace(component->getComponent().get(), component);
    }
    return result;
  }

  std::vector<RoutableConnection>
  resolveConnections(const Circuit& circuit, const ComponentMap& componentToGraphics)
  {
    std::vector<RoutableConnection> result;
    const auto                      connections = circuit.getConnections();
    result.reserve(connections.size());

    for (const CircuitConnection& connection : connections) {
      if (auto resolved = resolveConnection(connection, componentToGraphics))
        result.push_back(std::move(*resolved));
    }
    return result;
  }

  void stackSymmetricFeedbackPairs(std::span<const RoutableConnection> connections,
                                   GraphLayout::PlacementMap&          placements)
  {
    using Pair = std::pair<GraphicalLogicComponent*, GraphicalLogicComponent*>;
    std::vector<Pair> alignedPairs;

    for (const RoutableConnection& connection : connections) {
      auto* first  = connection.source;
      auto* second = connection.target;
      if (!first || !second || first == second || !placements.contains(first)
          || !placements.contains(second) || !first->getComponent()
          || !second->getComponent()
          || first->getComponent()->typeName() != second->getComponent()->typeName())
        continue;

      const bool hasReverseConnection =
          std::ranges::any_of(connections, [&](const RoutableConnection& candidate) {
            return candidate.source == second && candidate.target == first;
          });
      if (!hasReverseConnection)
        continue;

      Pair pair = first->getUiId() < second->getUiId() ? Pair{first, second}
                                                       : Pair{second, first};
      if (std::ranges::find(alignedPairs, pair) != alignedPairs.end())
        continue;
      alignedPairs.push_back(pair);

      const QRectF firstBounds =
          movedComponentObstacle(pair.first, placements.at(pair.first), 0);
      const QRectF secondBounds =
          movedComponentObstacle(pair.second, placements.at(pair.second), 0);
      const qreal centerX = DiagramScene::snapToGrid(
          (firstBounds.center().x() + secondBounds.center().x()) / 2.0);
      const qreal pairCenterY = DiagramScene::snapToGrid(
          (firstBounds.center().y() + secondBounds.center().y()) / 2.0);
      const qreal requiredCenterDistance =
          firstBounds.height() / 2.0 + secondBounds.height() / 2.0 + ComponentClearance;
      const qreal pairGrid = 2.0 * DiagramScene::GRID_SIZE;
      const qreal centerDistance =
          std::ceil(requiredCenterDistance / pairGrid) * pairGrid;

      // A symmetric two-gate latch is conventionally drawn as a vertical pair. Keep
      // both gate centers on the same x coordinate and separate their centers enough to
      // preserve the mandatory two-grid component clearance.
      const qreal firstCenterY  = pairCenterY - centerDistance / 2.0;
      const qreal secondCenterY = pairCenterY + centerDistance / 2.0;
      placements.at(pair.first).rx() += centerX - firstBounds.center().x();
      placements.at(pair.second).rx() += centerX - secondBounds.center().x();
      placements.at(pair.first).ry() += firstCenterY - firstBounds.center().y();
      placements.at(pair.second).ry() += secondCenterY - secondBounds.center().y();
      placements.at(pair.first)  = DiagramScene::snapToGrid(placements.at(pair.first));
      placements.at(pair.second) = DiagramScene::snapToGrid(placements.at(pair.second));
    }
  }

  PortSide oppositeSide(const PortSide side)
  {
    switch (side) {
      case PortSide::LEFT: return PortSide::RIGHT;
      case PortSide::RIGHT: return PortSide::LEFT;
      case PortSide::UP: return PortSide::DOWN;
      case PortSide::DOWN: return PortSide::UP;
    }
    std::unreachable();
  }

  OrientationMap
  preferredBoundaryIoOrientations(std::span<const RoutableConnection> connections)
  {
    constexpr std::array sidePreference = {PortSide::RIGHT, PortSide::LEFT,
                                           PortSide::DOWN, PortSide::UP};
    std::unordered_map<GraphicalIO*, std::array<std::size_t, sidePreference.size()>>
        votes;

    auto addVote = [&](GraphicalIO* io, const Port* connectedPort) {
      const PortSide preferredSide = oppositeSide(connectedPort->getDirection());
      const auto     vote          = std::ranges::find(sidePreference, preferredSide);
      ++votes[io][static_cast<std::size_t>(std::distance(sidePreference.begin(), vote))];
    };

    for (const auto& connection : connections) {
      GraphicalIO* sourceIO =
          category_cast<GraphicalIO>(connection.source, ItemCategory::IO);
      GraphicalIO* targetIO =
          category_cast<GraphicalIO>(connection.target, ItemCategory::IO);
      if (sourceIO)
        addVote(sourceIO, connection.targetPort);
      if (targetIO)
        addVote(targetIO, connection.sourcePort);
    }

    OrientationMap result;
    for (const auto& [io, sideVotes] : votes) {
      const auto winningVote = std::ranges::max_element(sideVotes);
      result.emplace(io, sidePreference[static_cast<std::size_t>(
                             std::distance(sideVotes.begin(), winningVote))]);
    }
    return result;
  }

  OrientationMap placeBoundaryIos(std::span<const RoutableConnection> connections,
                                  GraphLayout::PlacementMap&          placements)
  {
    struct IoNeighbor {
      GraphicalLogicComponent* component;
      const Port*              port;
    };
    std::unordered_map<GraphicalIO*, std::vector<IoNeighbor>> neighbors;
    for (const auto& connection : connections) {
      GraphicalIO* sourceIO =
          category_cast<GraphicalIO>(connection.source, ItemCategory::IO);
      GraphicalIO* targetIO =
          category_cast<GraphicalIO>(connection.target, ItemCategory::IO);
      if (sourceIO)
        neighbors[sourceIO].push_back({connection.target, connection.targetPort});
      if (targetIO)
        neighbors[targetIO].push_back({connection.source, connection.sourcePort});
    }

    OrientationMap   orientations = preferredBoundaryIoOrientations(connections);
    constexpr double gap          = 4.0 * DiagramScene::GRID_SIZE;

    QRectF logicBounds;
    bool   hasLogicBounds = false;
    for (const auto& [component, position] : placements) {
      if (hasCategory(component, ItemCategory::IO))
        continue;
      const QRectF bounds = movedComponentObstacle(component, position, 0);
      logicBounds         = hasLogicBounds ? logicBounds.united(bounds) : bounds;
      hasLogicBounds      = true;
    }

    for (const auto& [io, connected] : neighbors) {
      if (connected.empty() || !placements.contains(io))
        continue;

      QPointF     connectedPortCenter;
      QRectF      connectedBounds;
      bool        hasBounds      = false;
      std::size_t connectedCount = 0;
      for (const auto& [neighbor, neighborPort] : connected) {
        if (!placements.contains(neighbor))
          continue;
        const QRectF rect = movedComponentObstacle(neighbor, placements.at(neighbor), 0);
        connectedPortCenter +=
            movedPortPosition(neighbor, neighborPort, placements.at(neighbor));
        connectedBounds = hasBounds ? connectedBounds.united(rect) : rect;
        hasBounds       = true;
        ++connectedCount;
      }
      if (!hasBounds)
        continue;

      connectedPortCenter /= static_cast<qreal>(connectedCount);
      QPointF        ioPosition = placements.at(io);
      QRectF         ioBounds = movedComponentObstacle(io, ioPosition, 0, &orientations);
      const PortSide side     = orientations.at(io);
      const QRectF&  placementBoundary = hasLogicBounds ? logicBounds : connectedBounds;

      // Boundary symbols belong outside the complete logic block, not merely outside
      // their directly connected gate. This prevents an input or output from being
      // inserted between mutually connected latch gates and blocking their signal lanes.
      switch (side) {
        case PortSide::RIGHT:
          ioPosition.rx() += placementBoundary.left() - gap - ioBounds.right();
          break;
        case PortSide::LEFT:
          ioPosition.rx() += placementBoundary.right() + gap - ioBounds.left();
          break;
        case PortSide::DOWN:
          ioPosition.ry() += placementBoundary.top() - gap - ioBounds.bottom();
          break;
        case PortSide::UP:
          ioPosition.ry() += placementBoundary.bottom() + gap - ioBounds.top();
          break;
      }

      // Align the proposed I/O port with the port(s) it connects to. This turns the
      // common leaf-I/O case into a straight wire while fanout I/O aligns to the center
      // of its targets instead of inheriting an arbitrary force-layout offset.
      const QPointF proposedPort =
          io->mapToScene(io->portPositionFor(side)) + (ioPosition - io->pos());
      if (side == PortSide::LEFT || side == PortSide::RIGHT)
        ioPosition.ry() += connectedPortCenter.y() - proposedPort.y();
      else
        ioPosition.rx() += connectedPortCenter.x() - proposedPort.x();

      placements.at(io) = DiagramScene::snapToGrid(ioPosition);
      orientations.emplace(io, side);
    }
    return orientations;
  }

  Avoid::ConnDirFlags portConnDir(const PortSide side)
  {
    switch (side) {
      case PortSide::LEFT: return Avoid::ConnDirLeft;
      case PortSide::RIGHT: return Avoid::ConnDirRight;
      case PortSide::UP: return Avoid::ConnDirUp;
      case PortSide::DOWN: return Avoid::ConnDirDown;
    }
    std::unreachable();
  }

  PortSide effectivePortSide(GraphicalLogicComponent* component, const Port* port,
                             const OrientationMap& orientations)
  {
    GraphicalIO* ioComponent = category_cast<GraphicalIO>(component, ItemCategory::IO);
    return orientations.contains(ioComponent) ? orientations.at(ioComponent)
                                              : port->getDirection();
  }

  struct RoutingObstacle {
    Avoid::ShapeRef* shape;
    QRectF           bounds;
  };

  using RoutingObstacleMap =
      std::unordered_map<GraphicalLogicComponent*, RoutingObstacle>;

  RoutingObstacleMap registerObstacles(Avoid::Router&                   router,
                                       const GraphLayout::PlacementMap& placements,
                                       const int                        padding,
                                       const OrientationMap&            orientations)
  {
    RoutingObstacleMap result;
    result.reserve(placements.size());
    for (const auto& [component, position] : placements) {
      const QRectF rect =
          movedComponentObstacle(component, position, padding, &orientations)
              .normalized();
      if (rect.isEmpty())
        continue;

      auto obstacle = Avoid::rectangleFromQRectF(rect);
      // Router owns registered shapes, pins, junctions, and connectors.
      auto* shape = new Avoid::ShapeRef(&router, obstacle);
      result.emplace(component, RoutingObstacle{.shape = shape, .bounds = rect});
    }
    return result;
  }

  using NetKey = std::vector<std::uint64_t>;

  NetKey netKey(const Bus& bus)
  {
    NetKey key;
    key.reserve(bus.size());
    for (const auto& wire : bus)
      key.push_back(wire ? wire->getId() : 0);
    return key;
  }

  struct PendingNet {
    Bus                               bus;
    std::vector<QPointF>              terminals;
    std::vector<std::vector<QPointF>> routes;
  };

  bool pointOnOrthogonalRoute(const QPointF& point, const std::span<const QPointF> route)
  {
    for (std::size_t i = 1; i < route.size(); ++i) {
      const QPointF& first  = route[i - 1];
      const QPointF& second = route[i];
      if (first.y() == second.y() && point.y() == first.y()
          && point.x() >= std::min(first.x(), second.x())
          && point.x() <= std::max(first.x(), second.x()))
        return true;
      if (first.x() == second.x() && point.x() == first.x()
          && point.y() >= std::min(first.y(), second.y())
          && point.y() <= std::max(first.y(), second.y()))
        return true;
    }
    return false;
  }

  bool routesTouch(const std::span<const QPointF> first,
                   const std::span<const QPointF> second)
  {
    return (!first.empty()
            && (pointOnOrthogonalRoute(first.front(), second)
                || pointOnOrthogonalRoute(first.back(), second)))
           || (!second.empty()
               && (pointOnOrthogonalRoute(second.front(), first)
                   || pointOnOrthogonalRoute(second.back(), first)));
  }

  bool netRoutesAreComplete(const PendingNet& net)
  {
    if (net.routes.empty())
      return false;

    const bool containsEveryTerminal =
        std::ranges::all_of(net.terminals, [&](const QPointF& terminal) {
          return std::ranges::any_of(net.routes, [&](const auto& route) {
            return pointOnOrthogonalRoute(terminal, route);
          });
        });
    if (!containsEveryTerminal)
      return false;

    std::vector<bool> reached(net.routes.size());
    reached.front() = true;
    for (std::size_t pass = 1; pass < net.routes.size(); ++pass) {
      bool changed = false;
      for (std::size_t i = 0; i < net.routes.size(); ++i) {
        if (reached[i])
          continue;
        for (std::size_t j = 0; j < net.routes.size(); ++j) {
          if (reached[j] && routesTouch(net.routes[i], net.routes[j])) {
            reached[i] = true;
            changed    = true;
            break;
          }
        }
      }
      if (!changed)
        break;
    }
    return std::ranges::all_of(reached, std::identity{});
  }

  std::optional<std::vector<PendingNet>>
  registerNets(Avoid::Router& router, std::span<const RoutableConnection> connections,
               const GraphLayout::PlacementMap& placements,
               const OrientationMap& orientations, const RoutingObstacleMap& obstacles,
               const std::size_t routingOrder)
  {
    std::map<NetKey, std::vector<RoutableConnection>> groupedByKey;
    for (const auto& connection : connections)
      groupedByKey[netKey(connection.bus)].push_back(connection);

    std::vector<std::vector<RoutableConnection>> grouped;
    grouped.reserve(groupedByKey.size());
    for (auto& netConnections : groupedByKey | std::views::values)
      grouped.push_back(std::move(netConnections));
    if (!grouped.empty()) {
      const std::size_t offset = routingOrder % grouped.size();
      std::ranges::rotate(grouped, grouped.begin() + static_cast<std::ptrdiff_t>(offset));
      if ((routingOrder / grouped.size()) % 2 != 0)
        std::ranges::reverse(grouped);
    }

    std::vector<PendingNet> result;
    result.reserve(grouped.size());
    unsigned int nextPinClassId = 1;

    struct RegisteredNet {
      PendingNet                 net;
      Avoid::ConnRefList         connectors;
      std::optional<std::size_t> hyperedgeIndex;
    };
    std::vector<RegisteredNet> registered;
    registered.reserve(grouped.size());

    for (auto& netConnections : grouped) {
      using Terminal = std::pair<GraphicalLogicComponent*, const Port*>;
      std::vector<Terminal> uniqueTerminals;
      uniqueTerminals.reserve(netConnections.size() + 1);

      auto appendTerminal = [&](GraphicalLogicComponent* component, const Port* port) {
        const Terminal terminal{component, port};
        if (std::ranges::find(uniqueTerminals, terminal) == uniqueTerminals.end())
          uniqueTerminals.push_back(terminal);
      };

      for (const RoutableConnection& connection : netConnections) {
        appendTerminal(connection.source, connection.sourcePort);
        appendTerminal(connection.target, connection.targetPort);
      }
      if (uniqueTerminals.size() < 2)
        continue;

      Avoid::ConnEndList terminals;
      PendingNet net{.bus = netConnections.front().bus, .terminals = {}, .routes = {}};
      net.terminals.reserve(uniqueTerminals.size());
      for (const auto& [component, port] : uniqueTerminals) {
        const auto obstacle = obstacles.find(component);
        if (obstacle == obstacles.end())
          return std::nullopt;
        const QPointF position =
            movedPortPosition(component, port, placements.at(component), &orientations);
        const PortSide     side       = effectivePortSide(component, port, orientations);
        const unsigned int pinClassId = nextPinClassId++;
        new Avoid::ShapeConnectionPin(obstacle->second.shape, pinClassId,
                                      position.x() - obstacle->second.bounds.left(),
                                      position.y() - obstacle->second.bounds.top(), false,
                                      0.0, portConnDir(side));
        terminals.emplace_back(obstacle->second.shape, pinClassId);
        net.terminals.push_back(DiagramScene::snapToGrid(position));
      }
      if (terminals.size() < 2)
        return std::nullopt;

      const bool    direct = terminals.size() == 2;
      RegisteredNet registration{
          .net = std::move(net), .connectors = {}, .hyperedgeIndex = std::nullopt};
      if (direct) {
        auto* connector =
            new Avoid::ConnRef(&router, terminals.front(), terminals.back());
        connector->setRoutingType(Avoid::ConnType_Orthogonal);
        registration.connectors.push_back(connector);
        registered.push_back(std::move(registration));
        continue;
      }
      registration.hyperedgeIndex =
          router.hyperedgeRerouter()->registerHyperedgeForRerouting(terminals);
      registered.push_back(std::move(registration));
    }

    // A single transaction is essential: libavoid's nudger must see all distinct
    // logical nets together to allocate separate collinear channels.
    router.processTransaction();

    for (RegisteredNet& registration : registered) {
      if (registration.hyperedgeIndex) {
        const auto changes = router.hyperedgeRerouter()->newAndDeletedObjectLists(
            *registration.hyperedgeIndex);
        registration.connectors = changes.newConnectorList;
      }
    }

    Avoid::ConnRefListVector routeGroups;
    routeGroups.reserve(registered.size());
    for (const RegisteredNet& registration : registered)
      routeGroups.push_back(registration.connectors);
    router.separateOrthogonalRouteGroups(routeGroups, DiagramScene::GRID_SIZE);

    for (RegisteredNet& registration : registered) {
      registration.net.routes.reserve(registration.connectors.size());
      for (Avoid::ConnRef* connector : registration.connectors) {
        auto points = extractOrthogonalRoute(*connector, DiagramScene::GRID_SIZE);
        if (points.size() < 2)
          continue;
        Q_ASSERT(isOrthogonalRoute(points));
        registration.net.routes.push_back(std::move(points));
      }

      if (!netRoutesAreComplete(registration.net))
        return std::nullopt;
      result.push_back(std::move(registration.net));
    }
    return result;
  }

  std::vector<RoutedWire> buildWires(std::vector<PendingNet> pendingNets)
  {
    std::vector<RoutedWire> wires;
    for (PendingNet& net : pendingNets) {
      for (auto& points : net.routes)
        wires.push_back(RoutedWire{.bus = net.bus, .points = std::move(points)});
    }
    return wires;
  }

  bool distinctWiresIntersect(const std::span<const RoutedWire> wires)
  {
    for (std::size_t i = 0; i < wires.size(); ++i) {
      for (std::size_t j = i + 1; j < wires.size(); ++j) {
        if (wires[i].bus != wires[j].bus
            && orthogonalRoutesIntersect(wires[i].points, wires[j].points))
          return true;
      }
    }
    return false;
  }

  std::optional<CircuitAutoplacement>
  routeCandidate(std::span<const RoutableConnection> connections,
                 GraphLayout::PlacementMap           placements,
                 const CircuitAutoplacerOptions&     options,
                 const std::size_t                   routingOrder = 0)
  {
    CircuitAutoplacement result;
    result.components = std::move(placements);
    stackSymmetricFeedbackPairs(connections, result.components);
    separateLogicComponents(result.components);
    result.ioPortOrientations = placeBoundaryIos(connections, result.components);
    separateBoundaryIos(result.components, result.ioPortOrientations);
    Q_ASSERT(componentsHaveClearance(result.components, result.ioPortOrientations));

    Avoid::Router router(Avoid::OrthogonalRouting);
    configureOrthogonalRouter(router, DiagramScene::GRID_SIZE);
    const auto obstacles = registerObstacles(
        router, result.components, options.obstaclePadding, result.ioPortOrientations);
    // Commit the obstacle visibility graph before adding the complete batch of nets.
    router.processTransaction();
    auto pendingNets = registerNets(router, connections, result.components,
                                    result.ioPortOrientations, obstacles, routingOrder);
    if (!pendingNets)
      return std::nullopt;
    result.wires = buildWires(std::move(*pendingNets));
    if (distinctWiresIntersect(result.wires))
      return std::nullopt;
    return result;
  }

  void expandPlacements(GraphLayout::PlacementMap& placements, const int expansionLevel)
  {
    if (expansionLevel <= 0 || placements.empty())
      return;

    QPointF center;
    for (const auto& position : placements | std::views::values)
      center += position;
    center /= static_cast<qreal>(placements.size());
    center = DiagramScene::snapToGrid(center);

    const qreal factor = 1.0 + static_cast<qreal>(expansionLevel);
    for (auto& [_, position] : placements)
      position = DiagramScene::snapToGrid(center + (position - center) * factor);
  }

  bool segmentsCross(const QPointF& a, const QPointF& b, const QPointF& c,
                     const QPointF& d)
  {
    const bool firstHorizontal  = a.y() == b.y();
    const bool secondHorizontal = c.y() == d.y();
    if (firstHorizontal == secondHorizontal)
      return false;

    const QPointF& h1    = firstHorizontal ? a : c;
    const QPointF& h2    = firstHorizontal ? b : d;
    const QPointF& v1    = firstHorizontal ? c : a;
    const QPointF& v2    = firstHorizontal ? d : b;
    const double   minHX = std::min(h1.x(), h2.x());
    const double   maxHX = std::max(h1.x(), h2.x());
    const double   minVY = std::min(v1.y(), v2.y());
    const double   maxVY = std::max(v1.y(), v2.y());
    return v1.x() > minHX && v1.x() < maxHX && h1.y() > minVY && h1.y() < maxVY;
  }

  using PlacementScore =
      std::tuple<std::size_t, std::size_t, std::size_t, double, double>;

  PlacementScore scorePlacement(const CircuitAutoplacement& placement)
  {
    // Component Bounds Calculation
    std::vector<QRectF> componentBounds;
    componentBounds.reserve(placement.components.size());
    std::optional<QRectF> layoutBounds;

    for (const auto& [component, position] : placement.components) {
      const QRectF bounds =
          movedComponentObstacle(component, position, 0, &placement.ioPortOrientations);

      componentBounds.push_back(bounds);
      layoutBounds = layoutBounds ? layoutBounds->united(bounds) : bounds;
    }

    // Overlaps Calculation
    std::size_t overlaps = 0;
    for (const auto& [i, bound_a] : SILICON::views::enumerate(componentBounds)) {
      for (const auto& bound_b : componentBounds | std::views::drop(i + 1)) {
        if (!hasComponentClearance(bound_a, std::span(&bound_b, 1))) {
          ++overlaps;
        }
      }
    }

    // Shared Segments and Crossing
    std::size_t sharedSegments = 0;
    std::size_t crossings      = 0;

    for (const auto& [i, wire_a] : SILICON::views::enumerate(placement.wires)) {
      for (const auto& [bus, points] : placement.wires | std::views::drop(i + 1)) {
        if (wire_a.bus == bus)
          continue;

        if (orthogonalRoutesShareSegment(wire_a.points, points))
          ++sharedSegments;

        // Extract sliding pairs [a-1, a] and [b-1, b]
        for (const auto& [pA1, pA2] : wire_a.points | std::views::adjacent<2>) {
          for (const auto& [pB1, pB2] : points | std::views::adjacent<2>) {
            if (segmentsCross(pA1, pA2, pB1, pB2)) {
              ++crossings;
            }
          }
        }
      }
    }

    // Point calculation and Wire length
    std::size_t intermediatePoints = 0;
    double      wireLength         = 0.0;

    for (const auto& [bus, points] : placement.wires) {
      if (points.size() > 2)
        intermediatePoints += points.size() - 2;

      for (const QPointF& point : points) {
        const QRectF pointBounds(point, QSizeF(1.0, 1.0));
        layoutBounds = layoutBounds ? layoutBounds->united(pointBounds) : pointBounds;
      }

      // Calculate Manhattan distance between adjacent pairs
      for (const auto& pair : points | SILICON::views::slide(2)) {
        const QPointF& p1 = pair[0];
        const QPointF& p2 = pair[1];
        wireLength += std::abs(p2.x() - p1.x()) + std::abs(p2.y() - p1.y());
      }
    }

    // A bend is worth several grid steps, but never an arbitrarily long detour. This
    // keeps routes simple without allowing one fewer point to flatten a feedback circuit
    // into a huge loop around the whole diagram.
    const double routingCost =
        wireLength + intermediatePoints * 4.0 * DiagramScene::GRID_SIZE;

    const double area =
        layoutBounds ? layoutBounds->width() * layoutBounds->height() : 0.0;

    return {overlaps, sharedSegments, crossings, routingCost, area};
  }
}  // namespace

IoOrientationMap CircuitAutoplacer::boundaryIoOrientations(
    const Circuit& circuit, std::span<GraphicalLogicComponent* const> components)
{
  const auto componentToGraphics = buildComponentMap(components);
  const auto connections         = resolveConnections(circuit, componentToGraphics);
  return preferredBoundaryIoOrientations(connections);
}

CircuitAutoplacement
CircuitAutoplacer::compute(const Circuit&                            circuit,
                           std::span<GraphicalLogicComponent* const> components,
                           const CircuitAutoplacerOptions&           options)
{
  const int candidateCount = std::max(1, options.candidateCount);
  auto initialPlacements = GraphLayout::compute(circuit, components, options.graphLayout);
  const auto componentToGraphics = buildComponentMap(initialPlacements);
  const auto resolvedConnections = resolveConnections(circuit, componentToGraphics);

  CircuitAutoplacement          best;
  std::optional<PlacementScore> bestScore;

  for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
    if (options.isCancelled && options.isCancelled())
      break;

    GraphLayout::PlacementMap placements;
    if (candidateIndex == 0) {
      placements = std::move(initialPlacements);
    } else {
      GraphLayoutOptions candidateOptions = options.graphLayout;
      candidateOptions.algorithm          = GraphLayoutAlgorithm::ForceDirected;
      candidateOptions.randomSeed         = candidateIndex;
      placements = GraphLayout::compute(circuit, components, candidateOptions);
    }

    auto candidate = routeCandidate(resolvedConnections, std::move(placements), options,
                                    static_cast<std::size_t>(candidateIndex));
    if (!candidate) {
      if (options.progress)
        options.progress(candidateIndex + 1, candidateCount);
      continue;
    }
    const auto score = scorePlacement(*candidate);
    if (!bestScore || score < *bestScore) {
      best      = std::move(*candidate);
      bestScore = score;
    }

    if (options.progress)
      options.progress(candidateIndex + 1, candidateCount);
  }

  // Constraint failures are search feedback, not a valid final result. If the graph
  // candidates all choose the same congested corridors, keep widening a deterministic
  // base placement and rotate the order in which libavoid fixes complete nets. The
  // drawing plane is unbounded, so this eventually supplies a separate lane for every
  // finite set of nets.
  for (int routingAttempt = 0; !bestScore; ++routingAttempt) {
    if (options.isCancelled && options.isCancelled())
      break;

    auto placements = GraphLayout::compute(circuit, components, options.graphLayout);
    expandPlacements(placements, 1 + routingAttempt / 4);
    auto candidate =
        routeCandidate(resolvedConnections, std::move(placements), options,
                       static_cast<std::size_t>(candidateCount + routingAttempt));
    if (candidate) {
      best      = std::move(*candidate);
      bestScore = scorePlacement(best);
    }

    if (options.progress)
      options.progress(candidateCount, candidateCount);
  }

  return best;
}

}  // namespace SILICON::ui
