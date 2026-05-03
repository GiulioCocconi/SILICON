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

#include <memory>
#include <ranges>
#include <string>

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainter>
#include <QRect>
#include <QUndoStack>

#include <core/circuit.hpp>
#include <core/simulator.hpp>

#include <ui/common/componentSearchBox.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/wireManager.hpp>

#include <nlohmann/json.hpp>

class GraphicalComponent;
class GraphicalWireSegment;
class GUIComponentFactory;

/**
 * @class DiagramScene
 * @brief Graphics scene for the circuit diagram editor.
 *
 * DiagramScene extends QGraphicsScene to provide an interactive
 * circuit diagram editor. It manages component placement,
 * wire drawing, and various interaction modes for
 * editing and simulation.
 *
 * The scene integrates with WireManager for wire topology
 * and uses ComponentSearchBox (CSB) for component selection.
 * It maintains an underlying Circuit model and uses a Simulator
 * for logic simulation.
 *
 * @see GraphicalComponent
 * @see GraphicalWireSegment
 * @see WireManager
 * @see Circuit
 * @see Simulator
 */
class DiagramScene : public QGraphicsScene {
  Q_OBJECT
public:
  /**
   * @enum InteractionMode
   * @brief Operating modes for the diagram scene.
   */
  enum class InteractionMode {
    NORMAL_MODE,            /**< Default editing mode */
    PAN_MODE,               /**< Panning the view */
    WIRE_CREATION_MODE,     /**< Drawing a new wire */
    COMPONENT_PLACING_MODE, /**< Placing a component after selection */
    SIMULATION_MODE         /**< Interactive simulation */
  };

  /**
   * @brief Constructs a diagram scene.
   * @param parent Optional parent object
   */
  explicit DiagramScene(QObject* parent = nullptr);

  /**
   * @brief Sets the interaction mode.
   * @param mode The new mode
   */
  void setInteractionMode(InteractionMode mode);

  /**
   * @brief Gets the current interaction mode.
   * @return Current mode
   */
  [[nodiscard]] InteractionMode getInteractionMode() const
  {
    return currentInteractionMode;
  }

  /**
   * @brief Gets the component currently being placed.
   * @return Pointer to component or nullptr
   */
  [[nodiscard]] GraphicalComponent* getComponentToBeDrawn() const;

  /**
   * @brief Shows the component search box at a position.
   * @param pos Position to show the search box
   */
  void showCSB(QPointF pos);

  /**
   * @brief Clears the wire being drawn.
   */
  void clearWireShadow();

  /**
   * @brief Shows the component shadow at cursor position.
   */
  void setComponentShadow();

  /**
   * @brief Clears the component shadow.
   */
  void clearComponentShadow();

  /**
   * @brief Adds a component to the scene.
   * @param component The component to add
   * @param pos Position to place the component
   */
  void addComponent(GraphicalComponent* component, QPointF pos);

  /**
   * @brief Begins placing a component of the given type.
   * @param typeName The component type name
   */
  void placeComponent(std::string typeName);

  /**
   * @brief Snaps a point to the grid.
   * @param point The point to snap
   * @return Snapped point
   */
  static QPointF snapToGrid(QPointF point);

  /** @brief Grid cell size in scene units */
  static constexpr int GRID_SIZE = 10;

  [[nodiscard]] QUndoStack* getUndoStack() const;

  /**
   * @brief Serializes the scene to JSON.
   * @return JSON string representation
   */
  [[nodiscard]] std::string serialize() const;

  /**
   * @brief Deserializes the scene from JSON.
   * @param jsonStr JSON string
   * @param guiFactory Factory for creating graphical components
   * @param coreRegistry Registry for creating core components
   */
  void deserialize(const std::string& jsonStr, GUIComponentFactory& guiFactory,
                   const ComponentRegistry& coreRegistry);

  void clear();

  ~DiagramScene() override;

public slots:
  /**
   * @brief Hides the component search box.
   */
  void hideCSB();

  /**
   * @brief Handles input toggle events from GraphicalInput components.
   * @param targetBus The bus to update
   * @param value The value to set
   * @param source The component that triggered the change
   */
  void handleInputToggled(Bus targetBus, unsigned int value, Component_weakPtr source);

  /**
   * @brief Refreshes the visual state of all graphical outputs.
   *
   * Queries the simulator buses and updates the visual state of
   * SINGLE_OUTPUT components accordingly.
   */
  void refreshGraphicalOutputs();

  /**
   * @brief Calculates wire connections for all components.
   *
   * Computes the logical bus connections between components
   * and wires based on spatial collisions.
   */
  void calculateWiresForComponents() const;

signals:
  /**
   * @brief Emitted when the interaction mode changes.
   * @param mode The new mode
   */
  void modeChanged(InteractionMode mode);

private:
  /**
   * @brief Draws the background grid.
   */
  void drawBackground(QPainter* painter, const QRectF& rect) override;

  /**
   * @brief Internal method to set mode with force option.
   */
  void setInteractionMode(InteractionMode newMode, bool force);

  /**
   * @brief Lifecycle helper methods for interaction modes.
   */
  void finalizeWireCreation();
  void enterComponentPlacingMode();
  void exitComponentPlacingMode();
  void enterSimulationMode();
  void exitSimulationMode();

  /**
   * @brief Handles mouse movement for wire/component dragging.
   */
  void mouseMoveEvent(QGraphicsSceneMouseEvent* mouseEvent) override;

  /**
   * @brief Handles mouse press for wire drawing and component placement.
   */
  void mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) override;

  /**
   * @brief Handles key presses for mode changes.
   */
  void keyPressEvent(QKeyEvent* event) override;

  /** @brief Current interaction mode */
  InteractionMode currentInteractionMode = InteractionMode::NORMAL_MODE;

  /** @brief Component being placed (shadow) */
  GraphicalComponent* componentToBeDrawn = nullptr;

  /** @brief Wire segment being drawn */
  GraphicalWireSegment* wireSegmentToBeDrawn = nullptr;

  /** @brief Component search box */
  ComponentSearchBox* csb = nullptr;

  /** @brief Wire manager for wire topology */
  WireManager wireManager;

  /** @brief Last placed component type for repeat placement */
  std::string lastPlacedComponentType;

  /** @brief Underlying logic circuit model */
  std::shared_ptr<Circuit> circuit;

  /** @brief Logic simulation engine */
  std::unique_ptr<Simulator> simulator;
};

/**
 * @brief Alias for DiagramScene::InteractionMode
 */
using InteractionMode = DiagramScene::InteractionMode;
