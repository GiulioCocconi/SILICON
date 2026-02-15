/*
 Copyright (c) 2025. Giulio Cocconi

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

#include "enums.hpp"

#include <QGraphicsObject>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include <unordered_set>
#include <vector>

class DiagramScene;
class GraphicalComponent;
class GraphicalWire;

enum CollidingStatus {
  NOT_COLLIDING,
  COLLIDING_WITH_COMPONENT,
  COLLIDING_WITH_PORT,
  COLLIDING_WITH_WIRE
};

class GraphicalItem : public QGraphicsObject {
  Q_OBJECT
public:
  using QGraphicsObject::QGraphicsObject;

  void     setCollidingStatus(CollidingStatus newStatus);
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

  [[nodiscard]] int  type() const override { return UNKNOWN; }
  [[nodiscard]] bool isColliding() const
  { return getCollidingStatus() != NOT_COLLIDING; };

protected:
  [[nodiscard]] virtual QRectF  getCollisionRect() const = 0;
  [[nodiscard]] virtual bool    canRotate() const { return true; }
  virtual void                  onPositionChanged(QPointF offset) {}
  [[nodiscard]] CollidingStatus getCollidingStatus() const { return collidingStatus; }

private:
  CollidingStatus collidingStatus = NOT_COLLIDING;
};
