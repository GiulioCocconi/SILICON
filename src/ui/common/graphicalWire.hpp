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

class GraphicalWire;

/* 1 GraphicalWire <-> GraphicalWireSegment
 * A GraphicalWire contains a collection of GraphicalWireSegment objects (stored in a
 * vector).
 * Each GraphicalWireSegment knows its parent GraphicalWire via a pointer.
 * Segments are added/removed dynamically using addSegment() and removeSegment(). */

/* 2 GraphicalWire <-> Bus
 * A GraphicalWire holds a Bus object, representing the logical connection (e.g., a bundle
 * of Wire objects). The Bus can be set/resized via setBus() or setBusSize(). */

/* 3 GraphicalWire <-> Junctions
 * Junctions are points where multiple GraphicalWireSegment objects connect
 * The getJunctions() method extracts these points by checking for overlapping segment
 * endpoints. Junctions are rendered as part of the wire's visual representation. */

class GraphicalWireSegment : public QGraphicsItem {
public:
  explicit GraphicalWireSegment(QPointF firstPoint, QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::WIRE_SEGMENT; }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  [[nodiscard]] QPainterPath shape() const override;

  bool isCompleted() { return false; };  // TODO: IMPLEMENT
  bool isPointOnPath(const QPointF point) const;

  void addPoints();

  void                 setShowPoints(const std::vector<QPointF>& showPoints);
  std::vector<QPointF> getShowPoints() { return showPoints; }

  QPointF lastPoint() const { return points[points.size() - 1]; }
  QPointF firstPoint() const { return points[0]; }
  QPointF lastShowPoint() const { return points[showPoints.size() - 1]; }

  bool empty() const { return points.size() == 1; }

  GraphicalWire* getGraphicalWire() const { return graphicalWire; }
  void           setGraphicalWire(GraphicalWire* graphicalWire);

  QRectF boundingRect() const override;

  ~GraphicalWireSegment() override;

  void applyPositionOffset(QPointF offset);

private:
  QPainterPath path;
  QPainterPath showPath;

  std::vector<QPointF> points;
  std::vector<QPointF> showPoints;
  GraphicalWire*       graphicalWire = nullptr;

  void updatePath();

  static constexpr int interval    = 60;
  static constexpr int slashLength = 20;
  static constexpr int slashAngle  = 135;

  static constexpr int boxHeight = 20;
  static constexpr int boxWidth  = interval * 0.6;
};

class GraphicalWire : public GraphicalItem {
public:
  explicit GraphicalWire(QGraphicsItem* parent = nullptr) : GraphicalItem(parent)
  {
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptedMouseButtons(Qt::AllButtons);
  };
  explicit GraphicalWire(const std::vector<GraphicalWireSegment*>& segments,
                         QGraphicsItem*                            parent = nullptr);

  int type() const override { return SiliconTypes::WIRE; }

  void addSegment(GraphicalWireSegment* segment);
  void removeSegment(GraphicalWireSegment* segment);

  void setBus(Bus bus) { this->bus = bus; }
  void setBusSize(const unsigned int size);

  Bus getBus() const { return bus; }

  void clearBusState();

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  [[nodiscard]] GraphicalWireSegment* segmentAtPoint(QPointF point) const;
  [[nodiscard]] std::vector<QPointF>  getJunctions() const;
  [[nodiscard]] std::vector<QPointF>  getVertices() const;
  ~GraphicalWire() override;

  QPainterPath shape() const override;

  QColor        getColor();
  static QColor getColor(GraphicalWire* w);

protected:
  [[nodiscard]] QRectF getCollisionRect() const override;
  [[nodiscard]] bool   canRotate() const override { return false; }
  void                 onPositionChanged(QPointF offset) override;

private:
  Bus                                       bus;
  std::unordered_set<GraphicalWireSegment*> segments;

  QRectF boundingRect() const override;
};
