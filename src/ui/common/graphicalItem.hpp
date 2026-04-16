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

#include <QGraphicsObject>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include <ui/common/diagramScene.hpp>
#include <ui/common/enums.hpp>

#include <unordered_set>
#include <vector>

class DiagramScene;
class GraphicalComponent;
class GraphicalWireSegment;

/**
 * @enum CollidingStatus
 * @brief Collision state of a graphical item.
 */
enum CollidingStatus {
  NOT_COLLIDING,            /**< Not colliding with anything */
  COLLIDING_WITH_COMPONENT, /**< Colliding with another component */
  COLLIDING_WITH_PORT,      /**< Colliding with a port */
  COLLIDING_WITH_WIRE       /**< Colliding with a wire */
};

/**
 * @class GraphicalItem
 * @brief Base class for all graphical items in the diagram.
 *
 * GraphicalItem extends QGraphicsObject to provide common
 * functionality for circuit diagram elements including
 * collision detection, rotation support, and interaction
 * mode handling.
 *
 * @see GraphicalComponent
 * @see GraphicalWireSegment
 */
class GraphicalItem : public QGraphicsObject {
  Q_OBJECT
public:
  using QGraphicsObject::QGraphicsObject;

  /**
   * @brief Sets the collision status.
   * @param newStatus The new collision status
   */
  void setCollidingStatus(CollidingStatus newStatus);

  /**
   * @brief Handles item change events.
   */
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

  /** @brief Returns the item type identifier */
  [[nodiscard]] int type() const override { return UNKNOWN; }

  /**
   * @brief Checks if the item is colliding.
   * @return True if colliding
   */
  [[nodiscard]] bool isColliding() const
  {
    return getCollidingStatus() != NOT_COLLIDING;
  };

public slots:
  /**
   * @brief Called when the interaction mode changes.
   * @param mode The new interaction mode
   */
  virtual void modeChanged(InteractionMode mode);

protected:
  /**
   * @brief Gets the rectangle used for collision detection.
   * @return Collision rectangle
   */
  [[nodiscard]] virtual QRectF getCollisionRect() const = 0;

  /**
   * @brief Whether the item can be rotated.
   * @return True by default
   */
  [[nodiscard]] virtual bool canRotate() const { return true; }

  /**
   * @brief Called when the item position changes.
   * @param offset The position change offset
   */
  virtual void onPositionChanged(QPointF offset) {}

  /**
   * @brief Gets the current collision status.
   * @return The collision status
   */
  [[nodiscard]] CollidingStatus getCollidingStatus() const { return collidingStatus; }

private:
  /** @brief Current collision status */
  CollidingStatus collidingStatus = NOT_COLLIDING;
};
