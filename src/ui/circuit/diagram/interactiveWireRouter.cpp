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

#include "interactiveWireRouter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <utility>

#include <libavoid/libavoid.h>
#include <libavoid/qtgeomtypes.h>

#include <ui/circuit/diagram/localWireEditor.hpp>
#include <ui/circuit/diagram/wireRouting.hpp>
#include <utils/ranges_wrapper.hpp>

namespace SILICON::ui {
namespace {

  using PointKey    = std::pair<qreal, qreal>;
  using JunctionKey = std::pair<uint64_t, PointKey>;

  inline PointKey key(const QPointF point)
  {
    return {point.x(), point.y()};
  }

  inline QPointF point(const PointKey& value)
  {
    return {value.first, value.second};
  }

  inline bool onEdge(const QPointF p, const QPointF a, const QPointF b)
  {
    return core::pointOnOrthogonalRoute(p, std::array{a, b});
  }

  // A branch endpoint can lie inside a trunk edge. Split there so both connectors
  // share a libavoid junction, retaining the point at the end of each adjacent piece.
  std::vector<std::vector<QPointF>> splitAtJunctions(const std::span<const QPointF> route,
                                                     const std::set<PointKey>& junctions)
  {
    if (route.empty())
      return {};

    std::vector<std::vector<QPointF>> result;
    std::vector<QPointF>              current{route.front()};
    std::vector<QPointF>              between;
    const auto                        append = [&](const QPointF p) {
      current.push_back(p);
      if (junctions.contains(key(p)))
        result.push_back(std::exchange(current, {p}));
    };

    for (auto [a, b] : route | SILICON::views::adjacent<2>) {
      between.clear();
      for (const auto& candidate : junctions) {
        if (const QPointF p = point(candidate); p != a && p != b && onEdge(p, a, b))
          between.push_back(p);
      }

      // Visit junctions in route order, including edges that run left or upward.
      std::ranges::sort(between, std::less{}, [a](const QPointF p) {
        return std::abs(p.x() - a.x()) + std::abs(p.y() - a.y());
      });

      for (const QPointF p : between)
        append(p);
      append(b);
    }
    if (current.size() >= 2)
      result.push_back(std::move(current));
    return result;
  }

  bool electricallyAttached(const RoutingSegment& selected, const RoutingSegment& other,
                            const QPointF tip)
  {
    // Callers already check geometric contact. Use shared vertex IDs when available;
    // snapshots without electrical metadata fall back to that geometric contact.
    if (selected.electricalVertices.empty() || other.electricalVertices.empty())
      return true;
    return std::ranges::any_of(selected.electricalVertices, [&](const auto& vertex) {
      return vertex.first && vertex.second == tip
             && std::ranges::find(other.electricalVertices, vertex)
                    != other.electricalVertices.end();
    });
  }

  void setFixedRoute(Avoid::ConnRef& connector, const std::span<const QPointF> points)
  {
    Avoid::PolyLine fixed;
    fixed.ps.reserve(points.size());
    for (const QPointF position : points)
      fixed.ps.push_back(Avoid::pointFromQPointF(position));
    connector.setFixedRoute(fixed);
  }

  Avoid::JunctionRef* fixedJunction(Avoid::Router& router, const QPointF position)
  {
    auto* junction = new Avoid::JunctionRef(&router, Avoid::pointFromQPointF(position));
    junction->setPositionFixed(true);
    return junction;
  }

  void addComponentPins(Avoid::Router& router, const RoutingComponent& component,
                        const QPointF                       displacement,
                        std::map<PointKey, Avoid::ConnEnd>& portEnds, unsigned int& pinId)
  {
    const QRectF original = component.bounds.normalized();
    qreal        left = original.left(), top = original.top();
    qreal        right = original.right(), bottom = original.bottom();
    for (const QPointF port : component.ports) {
      left   = std::min(left, port.x());
      top    = std::min(top, port.y());
      right  = std::max(right, port.x());
      bottom = std::max(bottom, port.y());
    }

    // Include ports outside the component's collision bounds in the pin shape.
    const QRectF pinBounds(QPointF(left, top), QPointF(right, bottom));
    auto*        shape = new Avoid::ShapeRef(
        &router, Avoid::rectangleFromQRectF(pinBounds.translated(displacement)));
    for (const QPointF port : component.ports) {
      const QPointF offset = port - pinBounds.topLeft();
      new Avoid::ShapeConnectionPin(shape, pinId, offset.x(), offset.y(), false, 0.0,
                                    Avoid::ConnDirAll);
      // Lookup uses snapshot coordinates; the pin follows the translated shape.
      portEnds.emplace(key(port), Avoid::ConnEnd(shape, pinId++));
    }
  }

  std::optional<std::vector<QPointF>>
  extractRoute(const std::span<Avoid::ConnRef* const> connectors, const qreal gridSize)
  {
    std::vector<QPointF> points;
    for (auto* connector : connectors) {
      if (!connector->hasValidRoute() || connector->hasCrossingObstacles())
        return std::nullopt;

      const auto routed = core::extractOrthogonalRoute(*connector, gridSize);
      const auto [source, destination] = connector->endpointConnEnds();
      // Grid snapping must preserve the exact connector endpoints.
      if (routed.size() < 2 || routed.front() != Avoid::toQPointF(source.position())
          || routed.back() != Avoid::toQPointF(destination.position()))
        return std::nullopt;
      // Adjacent pieces share one junction; append it only once.
      points.append_range(routed | std::views::drop(points.empty() ? 0 : 1));
    }

    // LocalWireEditor::valid() checks the assembled geometry for changed routes.
    return core::canonicalizeOrthogonalRoute(std::move(points));
  }

  bool straightSpanPresent(const std::span<const QPointF> route, QPointF a, QPointF b)
  {
    if (a == b)
      return core::pointOnOrthogonalRoute(a, route);

    // Check if any line segment contains both 'a' and 'b'
    return std::ranges::any_of(route | SILICON::views::adjacent<2>, [&](const auto& seg) {
      auto [p1, p2] = seg;
      return onEdge(a, p1, p2) && onEdge(b, p1, p2);
    });
  }

}  // namespace

InteractiveWireRouter::InteractiveWireRouter(std::vector<RoutingComponent> components,
                                             std::vector<RoutingSegment>   segments,
                                             const qreal                   gridSize)
  : components(std::move(components)), segments(std::move(segments)), gridSize(gridSize)
{
}

RoutePreview InteractiveWireRouter::routeComponents(
    const std::span<const ComponentRouteMove> moves) const
{
  if (auto local = LocalWireEditor(components, segments, gridSize).moveComponents(moves))
    return local;
  Request request;
  request.moves = moves;
  return routeWithLibavoid(request);
}

RoutePreview InteractiveWireRouter::routeWireBend(
    const uint64_t segmentId, const std::size_t bendIndex, const QPointF position) const
{
  if (auto local = LocalWireEditor(components, segments, gridSize)
                       .moveBend(segmentId, bendIndex, position))
    return local;
  Request request;
  request.handle    = Handle::Bend;
  request.segmentId = segmentId;
  request.index     = bendIndex;
  request.value     = position;
  return routeWithLibavoid(request);
}

RoutePreview
InteractiveWireRouter::routeWireEndpoint(const uint64_t    segmentId,
                                         const std::size_t endpointIndex,
                                         const QPointF     position) const
{
  Request request;
  request.handle    = Handle::Endpoint;
  request.segmentId = segmentId;
  request.index     = endpointIndex;
  request.value     = position;
  return routeWithLibavoid(request);
}

RoutePreview
InteractiveWireRouter::routeWireEdge(const uint64_t    segmentId,
                                     const std::size_t edgeIndex,
                                     const QPointF     displacement) const
{
  if (auto local = LocalWireEditor(components, segments, gridSize)
                       .moveEdge(segmentId, edgeIndex, displacement))
    return local;
  Request request;
  request.handle    = Handle::Edge;
  request.segmentId = segmentId;
  request.index     = edgeIndex;
  request.value     = displacement;
  return routeWithLibavoid(request);
}

RoutePreview
InteractiveWireRouter::routeWithLibavoid(const Request request) const
{
  // Validate before inspecting endpoints or allocating libavoid objects.
  // Collect endpoints per net so coincident points on different nets stay separate.
  std::map<uint64_t, std::set<PointKey>> netJunctions;
  for (const auto& segment : segments) {
    if (segment.points.size() < 2 || !core::isOrthogonalRoute(segment.points))
      return std::nullopt;
    auto& junctions = netJunctions[segment.net];
    junctions.insert(key(segment.points.front()));
    junctions.insert(key(segment.points.back()));
  }

  const bool hasHandle = request.handle != Handle::None;
  const auto selected =
      hasHandle ? std::ranges::find(segments, request.segmentId, &RoutingSegment::id)
                : segments.end();
  if (hasHandle) {
    if (selected == segments.end())
      return std::nullopt;
    const auto last = selected->points.size() - 1;
    switch (request.handle) {
      case Handle::Bend:
        if (request.index == 0 || request.index >= last)
          return std::nullopt;
        break;
      case Handle::Endpoint:
        if (request.index != 0 && request.index != last)
          return std::nullopt;
        break;
      case Handle::Edge:
        if (request.index >= last)
          return std::nullopt;
        break;
      case Handle::None: break;
    }
  }

  QPointF edgeA, edgeB, edgeMove;
  if (request.handle == Handle::Edge) {
    edgeA    = selected->points[request.index];
    edgeB    = selected->points[request.index + 1];
    edgeMove = request.value;

    // Edge dragging changes only the coordinate perpendicular to the edge.
    if (edgeA.y() == edgeB.y())
      edgeMove.setX(0);
    else
      edgeMove.setY(0);
  }

  auto movedBounds = request.moves | std::views::transform([](const auto& m) {
                       return std::pair{m.id, m.bounds.normalized()};
                     })
                     | std::ranges::to<std::map>();

  std::set<uint64_t> unlockedNets;
  std::set<uint64_t> unlockedSegments;

  const auto touchesHandle = [&](const QPointF tip) {
    return request.handle == Handle::Bend ? tip == selected->points[request.index]
                                          : onEdge(tip, edgeA, edgeB);
  };

  // Bend and edge drags also move attached branches. Endpoint drags detach just
  // the selected endpoint, leaving the original junction and its other wires fixed.
  if (hasHandle) {
    unlockedSegments.insert(selected->id);
    if (request.handle == Handle::Bend || request.handle == Handle::Edge) {
      for (const auto& segment : segments) {
        if (segment.id == selected->id || segment.net != selected->net)
          continue;

        const auto attached = [&](const QPointF tip) {
          return touchesHandle(tip) && electricallyAttached(*selected, segment, tip);
        };
        if (attached(segment.points.front()) || attached(segment.points.back())) {
          unlockedSegments.insert(segment.id);
        }
      }
    }
  }

  // Component moves can affect an entire attached net, including its fanout.
  // Index original port positions once instead of scanning every port per segment.
  std::set<PointKey> movedPorts;
  for (const auto& component : components) {
    if (!movedBounds.contains(component.id))
      continue;
    for (const QPointF port : component.ports)
      movedPorts.insert(key(port));
  }
  for (const auto& segment : segments) {
    if (movedPorts.contains(key(segment.points.front()))
        || movedPorts.contains(key(segment.points.back())))
      unlockedNets.insert(segment.net);
  }

  // Cache eligibility in snapshot order for obstacle checks and connector routing.
  const auto unlocked = segments
                        | std::views::transform([&](const RoutingSegment& segment) {
                            return unlockedNets.contains(segment.net)
                                   || unlockedSegments.contains(segment.id);
                          })
                        | std::ranges::to<std::vector<bool>>();

  // A moved component cannot push an unrelated net out of its way.
  for (const auto& [segment, canRoute] : std::views::zip(segments, unlocked)) {
    if (canRoute)
      continue;
    for (const auto& moveBounds : movedBounds | std::views::values) {
      if (core::orthogonalRouteCrossesObstacleInterior(segment.points,
                                                       std::array{moveBounds}))
        return std::nullopt;
    }
  }

  Avoid::Router router(Avoid::OrthogonalRouting);
  core::configureOrthogonalRouter(router, gridSize);

  std::map<PointKey, Avoid::ConnEnd> portEnds;
  unsigned int                       pinId = 1;

  for (const auto& component : components) {
    QPointF displacement{};
    if (const auto it = movedBounds.find(component.id); it != movedBounds.end())
      displacement = it->second.topLeft() - component.bounds.normalized().topLeft();
    addComponentPins(router, component, displacement, portEnds, pinId);
  }

  // Keep original coordinates as lookup keys even when a drag moves the junction.
  // Component ports already have shape pins and do not need standalone junctions.
  std::map<JunctionKey, Avoid::JunctionRef*> junctionRefs;
  for (const auto& [net, positions] : netJunctions) {
    for (const PointKey& position : positions) {
      if (portEnds.contains(position))
        continue;

      QPointF destination = point(position);
      if (request.handle == Handle::Bend && net == selected->net
          && destination == selected->points[request.index])
        destination = request.value;

      if (request.handle == Handle::Edge && net == selected->net
          && onEdge(destination, edgeA, edgeB) && destination != selected->points.front()
          && destination != selected->points.back())
        destination += edgeMove;

      junctionRefs.emplace(JunctionKey{net, position},
                           fixedJunction(router, destination));
    }
  }

  // A separate junction lets one endpoint leave a shared junction or component pin
  // without relocating the other connectors attached to it.
  Avoid::JunctionRef* movedEndpoint = nullptr;
  if (request.handle == Handle::Endpoint)
    movedEndpoint = fixedJunction(router, request.value);

  auto endFor = [&](const uint64_t net, const QPointF position) {
    if (auto port = portEnds.find(key(position)); port != portEnds.end())
      return port->second;
    return Avoid::ConnEnd(junctionRefs.at(JunctionKey{net, key(position)}));
  };

  // Keep connector groups in snapshot order; the router owns the connectors.
  // Include fixed pieces so libavoid considers them when routing movable wires.
  std::vector<std::vector<Avoid::ConnRef*>> connectorsBySegment(segments.size());

  for (const auto& [segment, canRoute, connectors] :
       std::views::zip(segments, unlocked, connectorsBySegment)) {
    const bool isSelected       = hasHandle && segment.id == request.segmentId;
    const bool selectedEndpoint = isSelected && request.handle == Handle::Endpoint;
    const auto pieces = splitAtJunctions(segment.points, netJunctions.at(segment.net));
    connectors.reserve(pieces.size());
    for (const auto& piece : pieces) {
      const bool moveFirst = selectedEndpoint && request.index == 0
                             && piece.front() == segment.points.front();
      const bool moveLast = selectedEndpoint && request.index + 1 == segment.points.size()
                            && piece.back() == segment.points.back();

      auto* connector = new Avoid::ConnRef(
          &router,
          moveFirst ? Avoid::ConnEnd(movedEndpoint) : endFor(segment.net, piece.front()),
          moveLast ? Avoid::ConnEnd(movedEndpoint) : endFor(segment.net, piece.back()));
      connector->setRoutingType(Avoid::ConnType_Orthogonal);

      const bool selectedPiece = isSelected && [&]() {
        switch (request.handle) {
          case Handle::Bend:
            return core::pointOnOrthogonalRoute(selected->points[request.index], piece);
          case Handle::Edge: return std::ranges::any_of(piece, touchesHandle);
          case Handle::Endpoint:
            return piece.front() == selected->points[request.index]
                   || piece.back() == selected->points[request.index];
          case Handle::None: return false;
        }
        return false;
      }();

      if (!canRoute || (isSelected && !selectedPiece)) {
        // A handle edit may affect only part of a segment split by junctions.
        setFixedRoute(*connector, piece);
      } else if (selectedPiece && request.handle == Handle::Bend
                 && piece.front() != segment.points[request.index]
                 && piece.back() != segment.points[request.index]) {
        // A bend at a piece endpoint was handled by moving its junction above.
        // An interior bend needs a checkpoint to force the route through the target.
        connector->setRoutingCheckpoints(
            {Avoid::Checkpoint(Avoid::pointFromQPointF(request.value))});
      } else if (selectedPiece && request.handle == Handle::Edge) {
        // Constrain the translated edge while leaving terminals in place; libavoid
        // can add doglegs between those terminals and the displaced span.
        std::vector<Avoid::Checkpoint> checkpoints;

        for (auto [p1, p2] : piece | SILICON::views::adjacent<2>) {
          if (!onEdge(p1, edgeA, edgeB) || !onEdge(p2, edgeA, edgeB))
            continue;

          for (const QPointF cp : {p1 + edgeMove, p2 + edgeMove}) {
            if (cp != piece.front() && cp != piece.back())
              checkpoints.emplace_back(Avoid::pointFromQPointF(cp));
          }
        }
        if (!checkpoints.empty())
          connector->setRoutingCheckpoints(checkpoints);
      }
      connectors.push_back(connector);
    }
  }

  // Route all connectors together so obstacle avoidance and nudging see every wire.
  router.processTransaction();
  std::vector<RoutedSegment> result;
  std::vector<RoutedSegment> changes;
  result.reserve(segments.size());

  for (const auto& [segment, canRoute, connectors] :
       std::views::zip(segments, unlocked, connectorsBySegment)) {
    if (!canRoute) {
      // Preserve the original representation of wires outside the edit's scope.
      result.push_back({segment.id, segment.points});
      continue;
    }

    auto routePoints = extractRoute(connectors, gridSize);
    if (!routePoints)
      return std::nullopt;
    if (*routePoints != segment.points)
      changes.push_back({segment.id, *routePoints});
    result.push_back({segment.id, std::move(*routePoints)});
  }

  // Check the requested geometry after snapping and canonicalization, which can
  // otherwise remove a checkpoint or shorten the dragged edge.
  if (request.handle == Handle::Bend || request.handle == Handle::Edge) {
    const auto& routePoints = result[selected - segments.begin()].points;
    if (request.handle == Handle::Bend) {
      if (!core::pointOnOrthogonalRoute(request.value, routePoints))
        return std::nullopt;
    } else if (!straightSpanPresent(routePoints, edgeA + edgeMove, edgeB + edgeMove)) {
      return std::nullopt;
    }
  }

  // Validate only changed geometry against the full snapshot. Untouched segments
  // remain available to valid() without triggering checks against each other.
  // Use destination component geometry so vacated bounds are no longer obstacles.
  auto movedComponents = components;
  for (auto& component : movedComponents) {
    if (const auto it = movedBounds.find(component.id); it != movedBounds.end()) {
      const QPointF delta =
          it->second.topLeft() - component.bounds.normalized().topLeft();
      component.bounds = it->second;
      for (auto& port : component.ports)
        port += delta;
    }
  }
  if (!LocalWireEditor(movedComponents, segments, gridSize)
           .valid(changes, request.segmentId))
    return std::nullopt;

  return result;
}

}  // namespace SILICON::ui
