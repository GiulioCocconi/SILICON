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

#include <QGraphicsItem>
#include <QKeyEvent>
#include <QPoint>
#include <QRect>

#include <nlohmann/json.hpp>

#include <ui/common/diagramScene.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalItem.hpp>

class GUIComponentFactory;

/**
 * @class Port
 * @brief Represents a connection point on a component.
 *
 * Port is a QGraphicsItem that represents an input or output
 * connection point on a graphical component. It connects to
 * its parent component via a visible line and provides collision
 * detection for wire connections.
 */
class Port : public QGraphicsItem {
private:
  /** @brief Port index in the port list */
  unsigned int index;

  /** @brief Position relative to component center */
  QPoint position;

  /** @brief Visual line connecting to component */
  QGraphicsLineItem* line;

  /** @brief Port name (bus name) */
  std::string name;

public:
  /**
   * @brief Constructs a port.
   * @param index The port index
   * @param position Position relative to component
   * @param name The port/bus name
   * @param parent Optional parent graphics item
   */
  Port(unsigned int index, QPoint position, std::string name,
       QGraphicsItem* parent = nullptr);

  /**
   * @brief Painting is handled by the parent component.
   */
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override {};

  /** @brief Gets the bounding rectangle from the line */
  QRectF boundingRect() const override { return this->line->boundingRect(); };

  /** @brief Gets the port index */
  unsigned int getIndex() const { return this->index; }

  /** @brief Gets the port position */
  QPoint getPosition() const { return this->position; }

  /** @brief Gets the port name */
  std::string getName() const { return this->name; }

  /**
   * @brief Sets the connecting line.
   * @param line The line item
   */
  void setLine(QGraphicsLineItem* line);

  /**
   * @brief Gets the collision rectangle for wire detection.
   * @return Rectangle for hit testing
   */
  [[nodiscard]] QRectF collisionRect() const;
};

/**
 * @class GraphicalComponent
 * @brief Base class for graphical circuit components.
 *
 * GraphicalComponent is the base class for all visually rendered
 * circuit components in the diagram. It manages the component
 * shape, input/output ports, rotation, and property editing.
 *
 * Ports are positioned relative to the component and can be
 * automatically detected via alpha-scanning the shape image.
 *
 * @see GraphicalLogicComponent
 * @see Port
 */
class GraphicalComponent : public GraphicalItem {
  Q_OBJECT
public:
  /**
   * @brief Gets the collision rectangle extended for wire connections.
   * @return Collision rectangle including ports
   */
  [[nodiscard]] QRectF collisionRectForWires() const;

protected:
  /**
   * @brief Sets the component shape graphics item.
   * @param shape The shape item
   */
  void setItemShape(QGraphicsItem* shape);

  /** @brief Gets the component shape */
  [[nodiscard]] QGraphicsItem* getItemShape() const { return itemShape; }

  /** @brief Gets bounding rectangle with selection margin */
  [[nodiscard]] QRectF boundingRect() const override;

  /** @brief Gets bounding rectangle without margins */
  [[nodiscard]] QRectF boundingRectWithoutMargins() const;

  /**
   * @brief Paints the component (selection outline).
   */
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  /** @brief Gets collision rectangle for drag/drop */
  [[nodiscard]] QRectF getCollisionRect() const override;

  /** @brief Components can rotate by default */
  [[nodiscard]] bool canRotate() const override { return true; }

  /**
   * @brief Handles double-click for properties dialog.
   */
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

  /** @brief Input connection ports */
  std::vector<Port*> inputPorts;

  /** @brief Output connection ports */
  std::vector<Port*> outputPorts;

  /**
   * @brief Creates the connecting line from port to shape.
   * @param port The port to connect
   */
  void setPortLine(Port* port);

  /** @brief Whether to scan shape alpha for port placement */
  bool scanShape = false;

protected:
  explicit GraphicalComponent(ItemCategory category, QGraphicsItem* shape,
                              QGraphicsItem* parent = nullptr, bool scanShape = false);

public:
  explicit GraphicalComponent(QGraphicsItem* shape, QGraphicsItem* parent = nullptr,
                              bool scanShape = false);

  /**
   * @brief Rotates the component by 90 degrees.
   */
  void rotate();

  /**
   * @brief Sets input and output ports.
   *
   * @param busToPortInputs Vector of (name, position) pairs for inputs
   * @param busToPortOutputs Vector of (name, position) pairs for outputs
   */
  virtual void
  setPorts(const std::vector<std::pair<std::string, QPoint>>& busToPortInputs,
           const std::vector<std::pair<std::string, QPoint>>& busToPortOutputs);

  /** @brief Gets all input ports */
  [[nodiscard]] std::vector<Port*> getInputPorts() const { return inputPorts; };

  /** @brief Gets all output ports */
  [[nodiscard]] std::vector<Port*> getOutputPorts() const { return outputPorts; };

  /**
   * @brief Gets the component type name for serialization.
   * @return String representing the component type
   */
  [[nodiscard]] virtual std::string getTypeName() const;

  /**
   * @brief Serializes the component to JSON.
   * @return JSON object with position, rotation, and type
   */
  [[nodiscard]] virtual nlohmann::ordered_json serialize() const;

  /**
   * @brief Deserializes a component from JSON using the factory.
   * @param j JSON object
   * @param factory Component factory for creating instances
   * @return Unique pointer to deserialized component
   */
  static std::unique_ptr<GraphicalComponent> deserialize(const nlohmann::json& j,
                                                         GUIComponentFactory&  factory);

private:
  /**
   * @brief Scans image for first non-transparent pixel.
   *
   * @param image Image to scan
   * @param initialPoint Starting point
   * @param coordinate True for X, false for Y
   * @param direction True for increasing, false for decreasing
   * @return First non-transparent point
   */
  QPoint scanImage(const QImage& image, const QPoint& initialPoint, bool coordinate,
                   bool direction) const;

  /** @brief The visual shape item */
  QGraphicsItem* itemShape = nullptr;
};
