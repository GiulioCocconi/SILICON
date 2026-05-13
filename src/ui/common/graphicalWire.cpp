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

#include "graphicalWire.hpp"

#include <QDebug>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

#include <utils/ranges_wrapper.hpp>

#include <core/wire.hpp>  // Gives access to State enum for coloring
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/wireManager.hpp>
#include <ui/logiFlow/logiFlowWindow.hpp>

// --- Graphical Wire --------------------------------------------------------------------

GraphicalWire::GraphicalWire(unsigned int busSize)
{
  setBusSize(busSize);
}

void GraphicalWire::addSegment(GraphicalWireSegment* segment)
{
  if (!segment || segments.contains(segment))
    return;

  segments.insert(segment);
}

void GraphicalWire::removeSegment(GraphicalWireSegment* segment)
{
  segments.erase(segment);
}

void GraphicalWire::setBusSize(unsigned int size)
{
  bus.setSize(size);
}

void GraphicalWire::clearBusState()
{
  for (const auto& wire : bus)
    if (wire)
      wire->forceSetCurrentState(State::UNKNOWN);
}

QColor GraphicalWire::getColor() const
{
  if (bus.isInErrorState())
    return Qt::red;

  // Show simulation state visually for single wires
  if (bus.size() == 1 && bus[0]) {
    switch (bus[0]->getCurrentState()) {
      case State::HIGH: return ThemeEngine::getColor("SILICON_ORANGE");
      case State::LOW: return ThemeEngine::getColor("SILICON_LORANGE");
      case State::UNKNOWN: return ThemeEngine::getColor("SILICON_VIOLET");
      default: qWarning() << "Unhandled wire status in getColor()"; return Qt::magenta;
    }
  }
  return ThemeEngine::getColor("SILICON_GREEN");
}

QColor GraphicalWire::getColor(const GraphicalWire* w)
{
  if (!w)
    return ThemeEngine::getColor("SILICON_BLUE");
  return w->getColor();
}

std::vector<QPointF> GraphicalWire::getVertices() const
{
  std::vector<QPointF> vertices;

  for (const auto segment : segments) {
    const auto sceneFirst = segment->mapToScene(segment->firstPoint());
    const auto sceneLast  = segment->mapToScene(segment->lastPoint());

    if (!segment->isFirstPointJunction())
      vertices.push_back(sceneFirst);
    if (!segment->isLastPointJunction())
      vertices.push_back(sceneLast);
  }

  return vertices;
}

QPainterPath GraphicalWire::shape() const
{
  QPainterPath combinedPath;

  for (const auto segment : segments)
    combinedPath.addPath(segment->mapToScene(segment->shape()).simplified());

  return combinedPath;
}

// --- GraphicalWireSegment --------------------------------------------------------------

GraphicalWireSegment::GraphicalWireSegment(QPointF firstPoint, QGraphicsItem* parent)
  : GraphicalItem(ItemCategory::WireSegment, parent)
{
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setAcceptHoverEvents(true);

  // Convert scene coordinate to local coordinate if we have a parent
  if (parent)
    firstPoint = mapFromScene(firstPoint);

  points.push_back(firstPoint);
  updatePath();
}

void GraphicalWireSegment::updateTopology()
{
  if (graphicalWire && graphicalWire->getManager()) {
    graphicalWire->getManager()->updateSegmentTopology(this);
  }
}

void GraphicalWireSegment::setGraphicalWire(GraphicalWire* newWire)
{
  if (graphicalWire == newWire)
    return;

  // Keep a reference to the old wire before we overwrite it
  GraphicalWire* oldWire = graphicalWire;

  if (oldWire)
    oldWire->removeSegment(this);

  // Assign the new wire, or create a brand new one using the old wire's manager
  if (newWire) {
    graphicalWire = newWire;
  } else {
    // Because of the early return at the top, oldWire is guaranteed to be non-null here
    if (!oldWire)
      throw std::logic_error("setGraphicalWire: oldWire is null unexpectedly");

    auto* oldManager = oldWire->getManager();
    if (!oldManager)
      throw std::logic_error("setGraphicalWire: old wire has no manager");

    graphicalWire = oldManager->createWire(1).get();
  }

  if (!graphicalWire)
    throw std::logic_error("setGraphicalWire: failed to create wire");
  graphicalWire->addSegment(this);

  // Propagate the new wire to all touching sibling segments
  for (auto* sibling : WireManager::segmentNeighbors(this)) {
    if (sibling != this) {
      sibling->setGraphicalWire(graphicalWire);
    }
  }
}

void GraphicalWireSegment::setShowPoints(const std::vector<QPointF>& scenePoints)
{
  if (scenePoints.size() > 2)
    throw std::invalid_argument("setShowPoints: at most 2 points allowed");

  std::vector<QPointF> localPoints;
  localPoints.reserve(scenePoints.size());
  for (const auto& pt : scenePoints)
    localPoints.push_back(mapFromScene(pt));

  this->showPoints = localPoints;
  updatePath();
}

void GraphicalWireSegment::addPoints()
{
  if (points.empty()) {
    points           = std::move(showPoints);
    this->showPoints = {};
    updatePath();
    return;
  }

  // Check for self intersection
  const QPainterPathStroker stroker;
  const auto                showStroke   = stroker.createStroke(showPath);
  auto                      intersection = shape().intersected(showStroke);

  // Exclude the last point of path
  QPainterPath exclusionZone;
  exclusionZone.addEllipse(lastPoint(), 1, 1);
  intersection = intersection.subtracted(exclusionZone);

  if (intersection.isEmpty()) {
    for (auto pt : this->showPoints)
      this->points.push_back(pt);
    this->showPoints = {};
    updatePath();
  } else {
    qDebug() << "[GraphicalWireSegment] addPoints: self-intersecting wire detected, "
                "points NOT added";
  }
}

void GraphicalWireSegment::updatePath()
{
  prepareGeometryChange();
  path.clear();
  showPath.clear();

  path.moveTo(this->points[0]);

  for (unsigned int i = 1; i < this->points.size(); i++)
    path.lineTo(this->points[i]);

  if (!this->showPoints.empty()) {
    showPath.moveTo(this->lastPoint());
    for (auto showPoint : this->showPoints)
      showPath.lineTo(showPoint);
  }

  optimize();
}

void GraphicalWireSegment::optimize()
{
  // Need at least 3 points to have a "middle" one to remove
  if (points.size() < 3)
    return;

  size_t write_idx = 1;  // We always keep points[0]

  for (size_t read_idx = 1; read_idx < points.size() - 1; ++read_idx) {
    const QPointF& prev = points[write_idx - 1];
    const QPointF& curr = points[read_idx];
    const QPointF& next = points[read_idx + 1];

    bool isHorizontal = (prev.y() == curr.y() && curr.y() == next.y());
    bool isVertical   = (prev.x() == curr.x() && curr.x() == next.x());

    // If the point is NOT redundant, keep it.
    // If it IS redundant, do nothing and it gets overwritten later.
    if (!isHorizontal && !isVertical) {
      points[write_idx++] = curr;
    }
  }

  // We always keep the very last point
  points[write_idx++] = points.back();

  if (write_idx < points.size()) {
    points.resize(write_idx);
  }
}

// --- Painting --------------------------------------------------------------------------

void GraphicalWireSegment::paint(QPainter*                       painter,
                                 const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  const int    size  = graphicalWire ? graphicalWire->getBus().size() : 1;
  const QColor color = GraphicalWire::getColor(graphicalWire);

  // Draw main path and showPath
  painter->setPen(QPen(color, 3));
  painter->drawPath(path);

  painter->setPen(QPen(Qt::red, 3));
  painter->drawPath(showPath);

  // Draw junction ellipses at endpoints
  if (firstJunction || lastJunction) {
    painter->setPen(QPen(color, 3));
    const auto junctionPoint = firstJunction ? points.front() : points.back();
    painter->drawEllipse(junctionPoint, 3, 3);
  }

  // Draw hovered / selected points
  if (isSelected()) {
    painter->setPen(Qt::NoPen);
    for (size_t i = 0; i < points.size(); ++i) {
      // Compare size_t to safely avoid signed/unsigned compiler warnings
      const bool hovered = (i == static_cast<size_t>(hoveredPointIndex));
      painter->setBrush(hovered ? QColor(255, 80, 80) : QColor(200, 60, 60, 160));
      painter->drawEllipse(points[i], pointRadius, pointRadius);
    }
  }

  /* Bus decorations (slashes and size boxes) *
   *       0   1   2   3   4   5   6          *
   *       |  [ ]  |       |  [ ]  |          */

  const qreal totalLength     = path.length();
  const bool  drawDecorations = (size > 1) && (totalLength >= 2 * interval);

  if (!drawDecorations)
    return;

  painter->setPen(QPen(color, 2.0));
  painter->setFont(QFont("NovaMono", painter->font().pointSize() * 0.8));

  const QString   sizeText = QString::number(size);
  const QRectF    boxRect(-boxWidth / 2.0, -boxHeight / 2.0, boxWidth, boxHeight);
  constexpr qreal halfLen = slashLength / 2.0;

  int counter = 0;
  for (qreal dist = interval; dist < totalLength; dist += interval, ++counter) {
    const bool drawSlash = (counter % 2 == 0);
    const bool drawBox   = ((counter - 1) % 4 == 0);

    // Skip expensive path math if we aren't drawing anything this iteration
    if (!drawSlash && !drawBox)
      continue;

    const qreal percent   = path.percentAtLength(dist);
    const qreal pathAngle = path.angleAtPercent(percent);

    painter->save();

    // Move the coordinate system's origin to the center of our slash
    painter->translate(path.pointAtPercent(percent));

    // Rotate the coordinate system. `angleAtPercent()` is counter-clockwise whilst
    // `rotate()` is clockwise, we use a negative angle to align.
    painter->rotate(-pathAngle);

    if (drawSlash) {
      // Now that the coordinate system is aligned with the path we can draw a simple
      // rotated line.
      painter->rotate(slashAngle);
      painter->drawLine(QPointF(-halfLen, 0), QPointF(halfLen, 0));
    } else if (drawBox) {
      // Rotate the coordinate system back to the original position in order not to
      // write the size upside down
      if (pathAngle == 180.0)
        painter->rotate(180.0);

      painter->setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
      painter->drawRoundedRect(boxRect, 5, 5);
      painter->setBrush(Qt::black);
      painter->drawText(boxRect, sizeText, QTextOption(Qt::AlignCenter));
    }

    painter->restore();
  }
}

QRectF GraphicalWireSegment::boundingRect() const
{
  return this->path.boundingRect()
      .united(this->showPath.boundingRect())
      .adjusted(-5, -5, 5, 5)
      .adjusted(-boxHeight / 2, -boxHeight / 2, boxHeight / 2, boxHeight / 2);
}

QPainterPath GraphicalWireSegment::shape() const
{
  QPainterPathStroker stroker;
  return stroker.createStroke(this->path);
}

bool GraphicalWireSegment::isPointOnPath(const QPointF point) const
{
  constexpr double tolerance = 5.0;

  if (points.empty())
    return false;

  if (points.size() == 1)
    return QLineF(point, points[0]).length() <= tolerance;

  // Helper lambdas for clean semantic checks
  auto isClose = [](double a, double b) { return std::abs(a - b) <= tolerance; };

  auto inRange = [](double val, double bound1, double bound2) {
    const auto [min_val, max_val] = std::minmax(bound1, bound2);
    return val >= min_val - tolerance && val <= max_val + tolerance;
  };

  for (const auto el : points | silicon::views::slide(2)) {
    // Map the sub-range elements to readable names immediately
    const auto& p1 = el[0];
    const auto& p2 = el[1];

    // Horizontal check
    if (isClose(p1.y(), p2.y()) && isClose(point.y(), p1.y())
        && inRange(point.x(), p1.x(), p2.x()))
      return true;

    // Vertical check
    if (isClose(p1.x(), p2.x()) && isClose(point.x(), p1.x())
        && inRange(point.y(), p1.y(), p2.y()))
      return true;
  }

  return false;
}
void GraphicalWireSegment::setPoints(std::vector<QPointF> newPoints)
{
  if (newPoints.empty())
    throw std::invalid_argument("setPoints: points must not be empty");
  prepareGeometryChange();
  points = std::move(newPoints);
  updatePath();
}

int GraphicalWireSegment::pointIndexAt(const QPointF localPos) const
{
  for (size_t i = 0; i < points.size(); i++) {
    if (QLineF(localPos, points[i]).length() <= grabRadius)
      return i;
  }
  return -1;
}

void GraphicalWireSegment::movePointTo(const size_t index, QPointF newLocalPos)
{
  if (index >= points.size())
    return;

  newLocalPos = DiagramScene::snapToGrid(newLocalPos);

  const QPointF oldPos = points[index];
  points[index]        = newLocalPos;

  // Helper lambda to maintain horizontal/vertical orthogonality for adjacent points
  auto alignAdjacent = [&](QPointF& adj) {
    const bool wasHorizontal =
        std::abs(adj.y() - oldPos.y()) <= std::abs(adj.x() - oldPos.x());

    if (wasHorizontal) {
      adj.setY(newLocalPos.y());
    } else {
      adj.setX(newLocalPos.x());
    }
  };

  // Adjust previous and next points if they exist
  if (index > 0) {
    alignAdjacent(points[index - 1]);
  }
  if (index + 1 < points.size()) {
    alignAdjacent(points[index + 1]);
  }

  updatePath();
}

void GraphicalWireSegment::setFirstPointJunction(bool v)
{
  if (firstJunction == v)
    return;

  firstJunction = v;
  prepareGeometryChange();
}

void GraphicalWireSegment::setLastPointJunction(bool v)
{
  if (lastJunction == v)
    return;

  lastJunction = v;
  prepareGeometryChange();
}

bool GraphicalWireSegment::isAlignedWith(const GraphicalWireSegment* other) const
{
  if (!other)
    throw std::invalid_argument("isAlignedWith: other is null");
  if (other == this)
    throw std::logic_error("isAlignedWith: cannot compare segment with itself");
  if (points.size() < 2 || other->points.size() < 2)
    throw std::logic_error("isAlignedWith: segments must have at least 2 points");
  if (!scene())
    throw std::logic_error("isAlignedWith: segment not in a scene");

  const auto p1 = std::array{mapToScene(points.front()), mapToScene(points.back())};

  const auto p2 = std::array{other->mapToScene(other->points.front()),
                             other->mapToScene(other->points.back())};

  auto it = std::ranges::find_first_of(p1, p2);

  return (it != p1.end());
}

void GraphicalWireSegment::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    const QPointF localPos = event->pos();
    dragPointIndex         = pointIndexAt(localPos);
    if (dragPointIndex >= 0) {
      dragStartPos = points[dragPointIndex];
      event->accept();
      return;
    }
  }
  GraphicalItem::mousePressEvent(event);
}

void GraphicalWireSegment::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  if (dragPointIndex >= 0 && isSelected()) {
    movePointTo(dragPointIndex, event->pos());
    update();
    event->accept();
    return;
  }
  GraphicalItem::mouseMoveEvent(event);
}

void GraphicalWireSegment::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  GraphicalItem::mouseReleaseEvent(event);

  if (event->button() != Qt::LeftButton) {
    return;
  }

  const auto ds = qobject_cast<DiagramScene*>(scene());
  if (!ds)
    return;

  if (dragPointIndex >= 0) {
    const QPointF newPos = points[dragPointIndex];
    if (newPos != dragStartPos) {
      const auto undoStack = ds->getUndoStack();
      const auto moveCmd =
          new MoveWirePointCommand(this, dragPointIndex, dragStartPos, newPos);
      undoStack->push(moveCmd);
    }
    dragPointIndex = -1;
  }

  if (graphicalWire && graphicalWire->getManager()) {
    graphicalWire->getManager()->updateSegmentTopology(this);
  }
}

void GraphicalWireSegment::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
  const int newHovered = pointIndexAt(event->pos());
  if (newHovered != hoveredPointIndex) {
    hoveredPointIndex = newHovered;
    update();
  }

  GraphicalItem::hoverMoveEvent(event);
}

GraphicalWireSegment::~GraphicalWireSegment()
{
  if (!graphicalWire)
    return;

  // Cache the manager as removing the segment from the wire might trigger a clean-up
  auto* manager = graphicalWire->getManager();

  // Extract the segment from the wire so the manager sees the accurate segment count
  graphicalWire->removeSegment(this);

  if (manager)
    manager->removeSegment(this);
}

nlohmann::ordered_json GraphicalWireSegment::serialize() const
{
  nlohmann::ordered_json j;
  j["uiId"] = getUiId();

  auto jsonPoints = points | std::views::transform([](const QPointF& p) {
                      return nlohmann::ordered_json{{"x", p.x()}, {"y", p.y()}};
                    })
                    | std::ranges::to<std::vector>();

  j["points"] = jsonPoints;

  if (graphicalWire) {
    const auto& bus = graphicalWire->getBus();
    if (bus.size() > 0 && bus[0]) {
      j["wireId"] = bus[0]->getId();
    }
  }

  return j;
}
