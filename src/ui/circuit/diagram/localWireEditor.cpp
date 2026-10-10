#include "localWireEditor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "wireRouting.hpp"

namespace SILICON::ui {
namespace {
bool onSpan(QPointF point, QPointF first, QPointF last)
{
  const std::array span{first, last};
  return core::pointOnOrthogonalRoute(point, span);
}

bool portAt(std::span<const RoutingComponent> components, QPointF point)
{
  for (const auto& component : components)
    if (std::ranges::find(component.ports, point) != component.ports.end())
      return true;
  return false;
}

bool attachedAt(const RoutingSegment& selected, const RoutingSegment& other,
                const QPointF point, const QPointF otherTip)
{
  if (selected.electricalVertices.empty() || other.electricalVertices.empty())
    return point == otherTip;
  for (const auto& [vertex, location] : selected.electricalVertices)
    if (location == point && vertex)
      for (const auto& [otherVertex, otherLocation] : other.electricalVertices)
        if (vertex == otherVertex && otherLocation == otherTip)
          return true;
  return false;
}
}  // namespace

LocalWireEditor::LocalWireEditor(std::span<const RoutingComponent> components,
                                 std::span<const RoutingSegment> segments,
                                 const qreal gridSize)
  : components(components), segments(segments), gridSize(gridSize)
{}

QPointF LocalWireEditor::snap(QPointF point) const
{
  if (gridSize > 0) {
    point.setX(std::round(point.x() / gridSize) * gridSize);
    point.setY(std::round(point.y() / gridSize) * gridSize);
  }
  return point;
}

const RoutingSegment* LocalWireEditor::find(const uint64_t id) const
{
  const auto it = std::ranges::find(segments, id, &RoutingSegment::id);
  return it == segments.end() ? nullptr : &*it;
}

RoutePreview LocalWireEditor::moveComponents(
    const std::span<const ComponentRouteMove> moves) const
{
  using PointKey = std::pair<qreal, qreal>;
  std::map<PointKey, QPointF> movedPorts;
  std::vector<QRectF> movedBounds;
  std::vector<RoutingComponent> movedComponents(components.begin(), components.end());
  for (const auto& move : moves) {
    const auto component = std::ranges::find(components, move.id,
                                             &RoutingComponent::id);
    if (component == components.end())
      return std::nullopt;
    const QRectF bounds = move.bounds.normalized();
    const QPointF delta = bounds.topLeft() - component->bounds.normalized().topLeft();
    movedBounds.push_back(bounds);
    auto& movedComponent  = movedComponents[component - components.begin()];
    movedComponent.bounds = bounds;
    for (auto& port : movedComponent.ports)
      port += delta;
    for (const QPointF port : component->ports) {
      const PointKey key{port.x(), port.y()};
      if (const auto existing = movedPorts.find(key); existing != movedPorts.end()
          && existing->second != port + delta)
        return std::nullopt;
      movedPorts[key] = port + delta;
    }
  }

  std::vector<RoutedSegment> changes;
  for (const auto& segment : segments) {
    if (segment.points.size() < 2)
      return std::nullopt;
    const auto first = movedPorts.find({segment.points.front().x(),
                                        segment.points.front().y()});
    const auto last = movedPorts.find({segment.points.back().x(),
                                       segment.points.back().y()});
    const bool moveFirst = first != movedPorts.end()
                           && first->second != segment.points.front();
    const bool moveLast = last != movedPorts.end()
                          && last->second != segment.points.back();
    if (moveFirst && moveLast)
      return std::nullopt;
    if (!moveFirst && !moveLast)
      continue;
    auto route = moveAttachedEndpoint(segment, moveFirst,
                                      moveFirst ? first->second : last->second);
    if (!route)
      return std::nullopt;
    changes.push_back({segment.id, std::move(*route)});
  }

  // Validate against destination obstacles; the original component has vacated
  // its snapshot bounds, which the new wire may legitimately cross.
  auto result =
      LocalWireEditor(movedComponents, segments, gridSize).finish(0, std::move(changes));
  if (!result)
    return std::nullopt;
  for (const auto& route : *result)
    for (const QRectF bounds : movedBounds)
      if (core::orthogonalRouteCrossesObstacleInterior(route.points,
                                                       std::array{bounds}))
        return std::nullopt;
  return result;
}

std::optional<std::vector<QPointF>> LocalWireEditor::moveAttachedEndpoint(
    const RoutingSegment& segment, const bool first, const QPointF destination)
{
  auto points = segment.points;
  if (points.size() < 2)
    return std::nullopt;
  if (!first)
    std::ranges::reverse(points);
  const QPointF old = points.front();
  if (old == destination)
    return segment.points;
  if (points[1].x() == old.x()) {
    if (destination.x() != old.x())
      points.insert(points.begin() + 1, {destination.x(), points[1].y()});
  } else if (destination.y() != old.y()) {
    points.insert(points.begin() + 1, {points[1].x(), destination.y()});
  }
  points.front() = destination;
  points = core::canonicalizeOrthogonalRoute(std::move(points));
  if (!first)
    std::ranges::reverse(points);
  return points;
}

RoutePreview LocalWireEditor::moveBend(
    const uint64_t segmentId, const std::size_t index, QPointF position) const
{
  const auto* selected = find(segmentId);
  if (!selected || index == 0 || index + 1 >= selected->points.size())
    return std::nullopt;
  const auto& old = selected->points;
  position = snap(position);
  std::vector<QPointF> points;
  points.insert(points.end(), old.begin(), old.begin() + static_cast<std::ptrdiff_t>(index));
  const bool enteringHorizontal = old[index - 1].y() == old[index].y();
  points.push_back(enteringHorizontal ? QPointF(position.x(), old[index - 1].y())
                                      : QPointF(old[index - 1].x(), position.y()));
  points.push_back(position);
  points.push_back(enteringHorizontal ? QPointF(old[index + 1].x(), position.y())
                                      : QPointF(position.x(), old[index + 1].y()));
  points.insert(points.end(), old.begin() + static_cast<std::ptrdiff_t>(index + 1), old.end());
  std::vector<RoutedSegment> routes{{segmentId, std::move(points)}};
  const QPointF oldJunction = old[index];
  for (const auto& other : segments) {
    if (other.id == segmentId || other.net != selected->net
        || other.points.size() < 2)
      continue;
    for (const bool first : {true, false}) {
      const QPointF tip = first ? other.points.front() : other.points.back();
      if (!attachedAt(*selected, other, oldJunction, tip))
        continue;
      if (portAt(components, tip))
        return std::nullopt;
      auto adjusted = moveAttachedEndpoint(other, first, position);
      if (!adjusted)
        return std::nullopt;
      routes.push_back({other.id, std::move(*adjusted)});
      break;
    }
  }
  return finish(segmentId, std::move(routes));
}

RoutePreview LocalWireEditor::moveEdge(
    const uint64_t segmentId, const std::size_t index, QPointF displacement) const
{
  const auto* selected = find(segmentId);
  if (!selected || index + 1 >= selected->points.size())
    return std::nullopt;
  const auto& old = selected->points;
  const bool horizontal = old[index].y() == old[index + 1].y();
  displacement = snap(displacement);
  if (horizontal)
    displacement.setX(0);
  else
    displacement.setY(0);
  if (displacement.isNull())
    return std::nullopt;

  std::vector<QPointF> points = old;
  const QPointF movedFirst = old[index] + displacement;
  const QPointF movedLast = old[index + 1] + displacement;
  if (index == 0)
    points.insert(points.begin() + 1, movedFirst);
  else
    points[index] = movedFirst;
  const std::size_t lastIndex = index + 1 + (index == 0 ? 1 : 0);
  if (index + 2 == old.size())
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(lastIndex), movedLast);
  else
    points[lastIndex] = movedLast;

  std::vector<RoutedSegment> routes{{segmentId, std::move(points)}};
  for (const auto& other : segments) {
    if (other.id == segmentId || other.net != selected->net
        || other.points.size() < 2)
      continue;
    for (const bool first : {true, false}) {
      const QPointF tip = first ? other.points.front() : other.points.back();
      if (!onSpan(tip, old[index], old[index + 1]))
        continue;
      if (!attachedAt(*selected, other, tip, tip))
        continue;
      if ((index == 0 && tip == old.front())
          || (index + 2 == old.size() && tip == old.back()))
        continue;
      if (portAt(components, tip))
        return std::nullopt;
      auto adjusted = moveAttachedEndpoint(other, first, tip + displacement);
      if (!adjusted)
        return std::nullopt;
      routes.push_back({other.id, std::move(*adjusted)});
      break;
    }
  }
  return finish(segmentId, std::move(routes));
}

RoutePreview LocalWireEditor::finish(
    const uint64_t editedId, std::vector<RoutedSegment> routes) const
{
  for (auto& route : routes)
    route.points = core::canonicalizeOrthogonalRoute(std::move(route.points));
  if (!valid(routes, editedId))
    return std::nullopt;
  std::vector<RoutedSegment> complete;
  complete.reserve(segments.size());
  for (const auto& segment : segments) {
    const auto found = std::ranges::find(routes, segment.id, &RoutedSegment::id);
    complete.push_back(found == routes.end() ? RoutedSegment{segment.id, segment.points}
                                               : *found);
  }
  return complete;
}

bool LocalWireEditor::valid(std::span<const RoutedSegment> routes,
                            const uint64_t editedSegment) const
{
  std::map<uint64_t, std::span<const QPointF>> candidate;
  for (const auto& route : routes) {
    if (route.points.size() < 2 || !core::isOrthogonalRoute(route.points)
        || core::orthogonalRouteSelfIntersects(route.points))
      return false;
    candidate[route.id] = route.points;
  }
  for (const auto& segment : segments) {
    const auto points = candidate.contains(segment.id) ? candidate.at(segment.id)
                                                       : std::span<const QPointF>(segment.points);
    if (!candidate.contains(segment.id))
      continue;
    for (const auto& component : components) {
      const std::array obstacle{component.bounds};
      if (core::orthogonalRouteCrossesObstacleInterior(points, obstacle)
          && !core::orthogonalRouteCrossesObstacleInterior(segment.points, obstacle))
        return false;
    }
    for (const auto& other : segments) {
      if (other.id == segment.id)
        continue;
      const auto otherPoints = candidate.contains(other.id) ? candidate.at(other.id)
                                                             : std::span<const QPointF>(other.points);
      if (other.net != segment.net) {
        if (core::orthogonalRoutesShareSegment(points, otherPoints))
          return false;
        // A strict crossing is disconnected. New endpoint contact would be ambiguous.
        for (const QPointF tip : {points.front(), points.back()})
          if (core::pointOnOrthogonalRoute(tip, otherPoints)
              && !core::pointOnOrthogonalRoute(tip, other.points))
            return false;
        for (const QPointF tip : {otherPoints.front(), otherPoints.back()})
          if (core::pointOnOrthogonalRoute(tip, points)
              && !core::pointOnOrthogonalRoute(tip, segment.points))
            return false;
      } else {
        if (!segment.electricalVertices.empty() && !other.electricalVertices.empty()) {
          for (const auto& [vertex, oldPosition] : segment.electricalVertices)
            for (const auto& [otherVertex, otherPosition] : other.electricalVertices) {
              Q_UNUSED(otherPosition);
              if (!vertex || vertex != otherVertex)
                continue;
              std::optional<QPointF> position;
              const auto endpointPosition = [&](const RoutingSegment& member,
                                                std::span<const QPointF> route) {
                if (member.electricalVertices.front().first == vertex)
                  return std::optional<QPointF>{route.front()};
                if (member.electricalVertices.size() > 1
                    && member.electricalVertices[1].first == vertex)
                  return std::optional<QPointF>{route.back()};
                return std::optional<QPointF>{};
              };
              position = endpointPosition(segment, points);
              const auto secondPosition = endpointPosition(other, otherPoints);
              if (position && secondPosition && *position != *secondPosition)
                return false;
              if (!position)
                position = secondPosition.value_or(oldPosition);
              if (!core::pointOnOrthogonalRoute(*position, points)
                  || !core::pointOnOrthogonalRoute(*position, otherPoints))
                return false;
            }
        } else {
          const bool hadContact = core::orthogonalRoutesTouch(segment.points, other.points);
          if (segment.id != editedSegment && other.id != editedSegment
              && hadContact && !core::orthogonalRoutesTouch(points, otherPoints))
            return false;
        }
      }
    }
  }
  return true;
}

}  // namespace SILICON::ui
