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

#pragma once

#include <cassert>
#include <ranges>
#include <unordered_set>
#include <vector>

#include <QDebug>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QRect>

#include <core/wire.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalItem.hpp>
#include <utils/ranges_wrapper.hpp>

class GraphicalWireSegment;
class WireManager;

/* --- GraphicalWire ---------------------------------------------------------------------
 * An abstract entity that groups one or more GraphicalWireSegments sharing the same
 * logical bus. It is NOT a QGraphicsItem itself -- it is a plain object managed by the
 * WireManager. It carries the logical Bus and the bus size.
 */

class GraphicalWire {
public:
  GraphicalWire()  = default;
  ~GraphicalWire() = default;
  explicit GraphicalWire(unsigned int busSize);

  void addSegment(GraphicalWireSegment* segment);
  void removeSegment(GraphicalWireSegment* segment);

  [[nodiscard]] const std::unordered_set<GraphicalWireSegment*>& getSegments() const
  {
    return segments;
  }

  [[nodiscard]] bool empty() const { return segments.empty(); }

  void setBus(Bus bus) { this->bus = bus; }
  void setBusSize(unsigned int size);

  [[nodiscard]] Bus    getBus() const { return bus; }
  [[nodiscard]] size_t getBusSize() const { return bus.size(); }
  void                 clearBusState();

  // --- Topology ------------------------------------------------------------------------
  // Junctions: endpoint pairs where two segments in this wire touch.
  [[nodiscard]] std::vector<QPointF> getJunctions() const;

  // Vertices: segment endpoints that are NOT junctions (i.e. free tips).
  [[nodiscard]] std::vector<QPointF> getVertices() const;

  // Combined shape of all segments (in scene coordinates).
  [[nodiscard]] QPainterPath shape() const;

  // --- Color ---------------------------------------------------------------------------
  QColor        getColor() const;
  static QColor getColor(const GraphicalWire* w);

  void         setManager(WireManager* manager) { this->manager = manager; }
  WireManager* getManager() const { return manager; }

private:
  Bus                                       bus;
  std::unordered_set<GraphicalWireSegment*> segments;
  WireManager*                              manager = nullptr;
};

/* --- GraphicalWireSegment --------------------------------------------------------------
 * The visible, selectable polyline on the scene. Junctions can only occur at
 * endpoints that collide with other GraphicalWireSegments. Individual points are
 * highlighted in red and can be dragged, causing adjacent points to update.
 */

class GraphicalWireSegment : public GraphicalItem {
public:
  explicit GraphicalWireSegment(QPointF firstPoint, QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::WIRE_SEGMENT; }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  [[nodiscard]] QPainterPath shape() const override;
  [[nodiscard]] QRectF       boundingRect() const override;
  [[nodiscard]] QRectF       getCollisionRect() const override { return boundingRect(); }

  bool isPointOnPath(QPointF point) const;

  void addPoints();

  void                 setShowPoints(const std::vector<QPointF>& showPoints);
  std::vector<QPointF> getShowPoints() { return showPoints; }

  [[nodiscard]] const std::vector<QPointF>& getPoints() const { return points; }
  [[nodiscard]] QPointF lastPoint() const { return points[points.size() - 1]; }
  [[nodiscard]] QPointF firstPoint() const { return points[0]; }
  [[nodiscard]] QPointF lastShowPoint() const
  {
    return showPoints[showPoints.size() - 1];
  }
  [[nodiscard]] bool empty() const { return points.size() == 1; }

  // Replace the entire points vector (used by WireManager during aligned merge).
  void setPoints(std::vector<QPointF> newPoints);

  // Returns the index of the point closest to `localPos`, or -1 if none is
  // within the grab radius.
  [[nodiscard]] int pointIndexAt(QPointF localPos) const;

  // Move the point at `index` to `newLocalPos`. Adjacent points are adjusted
  // so that segments remain horizontal/vertical.
  void movePointTo(size_t index, QPointF newLocalPos);

  [[nodiscard]] bool isFirstPointJunction() const { return firstJunction; }
  [[nodiscard]] bool isLastPointJunction() const { return lastJunction; }
  void               setFirstPointJunction(bool v);
  void               setLastPointJunction(bool v);

  GraphicalWire* getGraphicalWire() const { return graphicalWire; }
  void           setGraphicalWire(GraphicalWire* graphicalWire);

  // Null out the wire pointer without side-effects. Used only during
  // WireManager teardown to avoid creating new wires/accessing freed memory.
  void detachFromWire() { graphicalWire = nullptr; }

  ~GraphicalWireSegment() override;

  [[nodiscard]] bool isAlignedWith(const GraphicalWireSegment* other) const;

  void optimize();

private:
  QPainterPath path;
  QPainterPath showPath;

  std::vector<QPointF> points;
  std::vector<QPointF> showPoints;
  GraphicalWire*       graphicalWire = nullptr;

  // Junction flags for the two endpoints
  bool firstJunction = false;
  bool lastJunction  = false;

  // Point-drag state
  int     dragPointIndex = -1;
  QPointF dragStartPos;

  // Hovered point (for highlight)
  int hoveredPointIndex = -1;

  void updatePath();

  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
  void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

  static constexpr int    interval    = 60;
  static constexpr int    slashLength = 20;
  static constexpr int    slashAngle  = 135;
  static constexpr int    boxHeight   = 20;
  static constexpr int    boxWidth    = interval * 0.6;
  static constexpr double pointRadius = 4.0;
  static constexpr double grabRadius  = 8.0;
};
