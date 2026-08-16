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

#include <nlohmann/json.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

class GraphicalWireSegment;
class WireManager;

/**
 * @class GraphicalWire
 * @brief Logical wire entity that groups segments sharing the same bus.
 *
 * GraphicalWire is an abstract logical entity representing a single logical bus
 * in the circuit diagram. It manages one or more GraphicalWireSegment objects
 * that are visually connected and share the same logical Bus and bus size.
 *
 * This class is not a QGraphicsItem itself - it is managed by WireManager
 * and tracks the logical connectivity independent of the visual representation.
 *
 * @see GraphicalWireSegment
 * @see WireManager
 */
class GraphicalWire {
public:
  GraphicalWire()  = default;
  ~GraphicalWire() = default;

  /**
   * @brief Constructs a wire with a specified bus size.
   * @param busSize The number of wires in the bus
   */
  explicit GraphicalWire(unsigned int busSize);

  /**
   * @brief Adds a segment to this wire.
   * @param segment The segment to add
   */
  void addSegment(GraphicalWireSegment* segment);

  /**
   * @brief Removes a segment from this wire.
   * @param segment The segment to remove
   */
  void removeSegment(GraphicalWireSegment* segment);

  /**
   * @brief Gets all segments belonging to this wire.
   * @return Set of segment pointers
   */
  [[nodiscard]] const std::unordered_set<GraphicalWireSegment*>& getSegments() const
  {
    return segments;
  }

  /** @brief Checks if this wire has no segments.
   * @return True if empty
   */
  [[nodiscard]] bool empty() const { return segments.empty(); }

  /**
   * @brief Sets the logical bus for this wire.
   * @param bus The bus to set
   */
  void setBus(Bus bus) { this->bus = bus; }

  /**
   * @brief Sets the bus size.
   * @param size The new size
   */
  void setBusSize(unsigned int size);

  /**
   * @brief Gets the logical bus.
   * @return Reference to the bus
   */
  [[nodiscard]] Bus getBus() const { return bus; }

  /**
   * @brief Gets the bus size.
   * @return Number of wires in the bus
   */
  [[nodiscard]] size_t getBusSize() const { return bus.size(); }

  /**
   * @brief Clears the state of all wires in the bus.
   *
   * Sets all wires to UNKNOWN state, used during simulation initialization.
   */
  void clearBusState();

  // --- Topology ------------------------------------------------------------------------

  /**
   * @brief Gets vertices at wire endpoints that are not junctions.
   *
   * Returns segment endpoints that are free tips (not connected to
   * other wires), representing connection points to components.
   *
   * @return Vector of endpoint positions in scene coordinates
   */
  [[nodiscard]] std::vector<QPointF> getVertices() const;

  /**
   * @brief Gets the combined shape of all segments.
   *
   * @return Painter path in scene coordinates
   */
  [[nodiscard]] QPainterPath shape() const;

  // ---

  /**
   * @brief Gets the color for this wire based on bus size.
   * @return QColor for rendering
   */
  QColor getColor() const;

  /**
   * @brief Gets the color for a wire pointer (null-safe).
   * @param w The wire pointer
   * @return QColor for rendering
   */
  static QColor getColor(const GraphicalWire* w);

  /**
   * @brief Sets the wire manager.
   * @param manager The manager instance
   */
  void setManager(WireManager* manager) { this->manager = manager; }

  /**
   * @brief Gets the wire manager.
   * @return Pointer to the manager
   */
  WireManager* getManager() const { return manager; }

private:
  /** @brief The logical bus */
  Bus bus;

  /** @brief Set of segments belonging to this wire */
  std::unordered_set<GraphicalWireSegment*> segments;

  /** @brief Wire manager for this wire */
  WireManager* manager = nullptr;
};

/**
 * @class GraphicalWireSegment
 * @brief The visible, selectable polyline wire on the diagram scene.
 *
 * GraphicalWireSegment is a QGraphicsItem representing a polyline wire
 * drawn on the diagram. It supports interactive point dragging,
 * automatic junction detection, and orthogonal (horizontal/vertical)
 * path maintenance.
 *
 * Junctions occur where an endpoint meets at least three distinct wire arms.
 * Individual points can be highlighted and dragged to reshape
 * the wire while maintaining orthogonality.
 *
 * @see GraphicalWire
 */
class GraphicalWireSegment : public GraphicalItem {
public:
  /**
   * @brief Constructs a segment starting at the given point.
   * @param firstPoint The starting point in local coordinates
   * @param parent Optional parent graphics item
   */
  explicit GraphicalWireSegment(QPointF firstPoint, QGraphicsItem* parent = nullptr);

  /** @brief Returns the item type identifier */
  int type() const override { return SiliconTypes::WIRE_SEGMENT; }

  /**
   * @brief Paints the wire segment.
   */
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  /** @brief Gets the shape for hit testing */
  [[nodiscard]] QPainterPath shape() const override;

  /** @brief Gets the bounding rectangle */
  [[nodiscard]] QRectF boundingRect() const override;

  /** @brief Gets the collision rectangle */
  [[nodiscard]] QRectF getCollisionRect() const override { return boundingRect(); }

  /**
   * @brief Checks if a point is on the wire path.
   * @param point The point to check
   * @return True if on path
   */
  bool isPointOnPath(QPointF point) const;

  /**
   * @brief Confirms the show points as actual wire points.
   *
   * Called when the wire drawing is completed, converting
   * the temporary showPoints into permanent points.
   */
  void addPoints();

  /**
   * @brief Sets temporary points shown while drawing.
   * @param showPoints The points to show
   */
  void setShowPoints(const std::vector<QPointF>& showPoints);

  /**
   * @brief Gets temporary points shown while drawing.
   * @return Vector of show points
   */
  std::vector<QPointF> getShowPoints() { return showPoints; }

  /** @brief Gets all points of the segment */
  [[nodiscard]] const std::vector<QPointF>& getPoints() const { return points; }

  /** @brief Gets all points mapped into scene coordinates. */
  [[nodiscard]] std::vector<QPointF> getScenePoints() const;

  /** @brief Gets the last point */
  [[nodiscard]] QPointF lastPoint() const { return points[points.size() - 1]; }

  /** @brief Gets the first point */
  [[nodiscard]] QPointF firstPoint() const { return points[0]; }

  /** @brief Gets the last show point */
  [[nodiscard]] QPointF lastShowPoint() const
  {
    return showPoints[showPoints.size() - 1];
  }

  /** @brief Checks if segment has only one point */
  [[nodiscard]] bool empty() const { return points.size() == 1; }

  /**
   * @brief Replaces all points at once.
   *
   * Used by WireManager during aligned merge operations.
   *
   * @param newPoints The new points vector
   */
  void setPoints(std::vector<QPointF> newPoints);

  /**
   * @brief Gets the index of the point nearest to localPos.
   *
   * @param localPos The position to check
   * @return Index of nearest point, or -1 if none within grab radius
   */
  [[nodiscard]] int pointIndexAt(QPointF localPos) const;

  /**
   * @brief Moves a point and adjusts adjacent points for orthogonality.
   *
   * @param index The point index to move
   * @param newLocalPos The new position
   */
  void movePointTo(size_t index, QPointF newLocalPos);

  /** @brief Checks if first endpoint is a junction */
  [[nodiscard]] bool isFirstPointJunction() const { return firstJunction; }

  /** @brief Checks if last endpoint is a junction */
  [[nodiscard]] bool isLastPointJunction() const { return lastJunction; }

  /** @brief Sets first endpoint junction status */
  void setFirstPointJunction(bool v);

  /** @brief Sets last endpoint junction status */
  void setLastPointJunction(bool v);

  /** @brief Gets the logical wire */
  GraphicalWire* getGraphicalWire() const { return graphicalWire; }

  /**
   * @brief Sets the logical wire, optionally propagating to touching siblings.
   * @param graphicalWire The wire to set
   * @param propagateToTouchingSegments Whether geometric contact should merge siblings
   */
  void setGraphicalWire(GraphicalWire* graphicalWire,
                        bool           propagateToTouchingSegments = true);

  /**
   * @brief Detaches from the logical wire without side effects.
   *
   * Used during WireManager teardown to avoid creating
   * new wires or accessing freed memory.
   */
  void detachFromWire() { graphicalWire = nullptr; }

  void updateTopology() override;

  ~GraphicalWireSegment() override;

  /**
   * @brief Checks whether another segment is a straight continuation.
   *
   * @param other The segment to check
   * @return True if the segments share an endpoint and their incident arms are
   * collinear and point in opposite directions.
   */
  [[nodiscard]] bool isAlignedWith(const GraphicalWireSegment* other) const;

  /**
   * @brief Serializes the wire segment to JSON.
   * @return JSON object with points and wire ID
   */
  [[nodiscard]] nlohmann::ordered_json serialize() const;

private:
  /** @brief The main path */
  QPainterPath path;

  /** @brief The temporary path while drawing */
  QPainterPath showPath;

  /** @brief The segment points */
  std::vector<QPointF> points;

  /** @brief Temporary points while drawing */
  std::vector<QPointF> showPoints;

  /** @brief The logical wire this segment belongs to */
  GraphicalWire* graphicalWire = nullptr;

  /** @brief Junction flag for first endpoint */
  bool firstJunction = false;

  /** @brief Junction flag for last endpoint */
  bool lastJunction = false;

  /** @brief Currently dragged point index */
  int dragPointIndex = -1;

  /** @brief Starting position of point drag */
  QPointF dragStartPos;

  /** @brief Currently hovered point index */
  int hoveredPointIndex = -1;

  /** @brief Updates the internal path shapes */
  void updatePath();

  /** @brief Handles mouse press for point dragging */
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

  /** @brief Handles mouse move for point dragging */
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

  /** @brief Handles mouse release */
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

  /** @brief Handles hover for point highlighting */
  void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

  /** @brief Interval for bus size decorations */
  static constexpr int interval = 60;

  /** @brief Length of slash decorations */
  static constexpr int slashLength = 20;

  /** @brief Angle for slash decorations */
  static constexpr int slashAngle = 135;

  /** @brief Height of bus size label box */
  static constexpr int boxHeight = 20;

  /** @brief Width of bus size label box */
  static constexpr int boxWidth = interval * 0.6;

  /** @brief Radius of draggable points */
  static constexpr double pointRadius = 4.0;

  /** @brief Grab radius for point selection */
  static constexpr double grabRadius = 8.0;
};

}  // namespace ui
}  // namespace SILICON
