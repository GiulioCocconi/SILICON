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

#include "../logSideView.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QList>
#include <QPainter>
#include <QPair>
#include <QRect>
#include <QStringList>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/component.hpp>
#include <core/siliconWaveform.hpp>

#include <ui/common/componentSearchBox.hpp>
#include <ui/common/wireManager.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::waveform;

class DiagramSceneSerializer;
class DiagramSceneSimulationController;
class GraphicalComponent;
class GraphicalItem;
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
   * @brief Cancels the active transient interaction, returning to normal editing.
   * @return True when there was an interaction to cancel
   */
  bool cancelCurrentInteraction();

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
  void placeComponent(std::string_view typeName);

  /**
   * @brief Begins placing a component of the given type.
   * @param typeName The component type name
   * @param showSearchBox Whether entering placement mode should show the quick search box
   */
  void placeComponent(std::string_view typeName, bool showSearchBox);

  /**
   * @brief Begins placing a component and applies initial core properties first.
   * @param typeName The component type name
   * @param showSearchBox Whether entering placement mode should show quick search
   * @param initialProperties Properties to apply to the associated logic component
   */
  void placeComponent(std::string_view typeName, bool showSearchBox,
                      const PropertyMap& initialProperties);

  /**
   * @brief Snaps a point to the grid.
   * @param point The point to snap
   * @return Snapped point
   */
  static QPoint snapToGrid(QPointF point);

  /** @brief Snaps one scene coordinate to the grid. */
  static int snapToGrid(qreal value);

  /** @brief Grid cell size in scene units */
  static constexpr int GRID_SIZE = 10;

  [[nodiscard]] QUndoStack*              getUndoStack() const;
  [[nodiscard]] LogSideView*             getLogSideView() const;
  [[nodiscard]] WireManager&             getWireManager() { return wireManager; }
  [[nodiscard]] const WireManager&       getWireManager() const { return wireManager; }
  [[nodiscard]] std::shared_ptr<Circuit> getCircuit() const { return circuit; }
  void                                   setCircuit(std::shared_ptr<Circuit> newCircuit)
  {
    circuit = std::move(newCircuit);
  }

  /**
   * @brief Enables or disables user-triggered IO interactions in simulation mode.
   * @param enabled True to allow clicks on interactive IO components
   */
  void setIoInteractionsEnabled(bool enabled) { ioInteractionsEnabled = enabled; }

  /**
   * @brief Returns whether simulation-mode IO clicks are currently allowed.
   */
  [[nodiscard]] bool areIoInteractionsEnabled() const { return ioInteractionsEnabled; }

  /** @brief Marks whether the scene is currently editing a subcircuit document. */
  void setSubcircuitDocumentMode(bool enabled) { subcircuitDocumentMode = enabled; }

  /** @brief Returns true when subcircuit-only graphical tools may be placed. */
  [[nodiscard]] bool isSubcircuitDocumentMode() const { return subcircuitDocumentMode; }

  /**
   * @brief Serializes the full editor scene to JSON.
   *
   * @anchor diagramscene_serialization_format
   * This format wraps the core logical circuit serialization from
   * @ref core_serialization_format and adds the visual data needed to rebuild the
   * editor scene exactly as drawn.
   *
   * The payload contains:
   * - `circuit`: the logical circuit model serialized by Circuit::serialize()
   * - `visual.components[]`: graphical component state such as `type`, `position`,
   *   `rotation`, and `vertexId`
   * - `visual.wires[]`: graphical wire routing data such as `points` and `wireId`
   *
   * `vertexId` links each graphical logic item back to the corresponding core
   * component vertex inside `circuit`. `wireId` keeps multiple wire segments attached
   * to the same logical wire/bus after deserialization.
   *
   * Unlike clipboard payloads, the persisted scene format does not retain runtime
   * `uiId` values because they are editor-local identifiers rather than stable file
   * data.
   *
   * @return JSON string representation of the scene
   */
  [[nodiscard]] std::string serialize() const;

  /**
   * @brief Serializes the selected components and wire segments for clipboard use.
   *
   * The returned JSON is an intermediate representation: callers choose the transport
   * encoding, such as BSON for clipboard operations.
   *
   * This payload is derived from @ref diagramscene_serialization_format rather than
   * being a separate file format. It keeps the same `circuit` plus `visual` split, but
   * limits the payload to the selected fragment, adds an `origin` used for paste
   * placement, and retains `uiId` values because clipboard, undo, and delete flows
   * match runtime scene items by those identifiers.
   *
   * @note Uses serializeItems internally
   *
   * @return JSON payload containing selected visual items and related core component
   * state
   */
  [[nodiscard]] nlohmann::ordered_json serializeSelection() const;

  /**
   * @brief Serializes arbitrary scene items using the internal selection payload format.
   *
   * The returned object follows the clipboard payload described in
   * serializeSelection(), which in turn is a scene-fragment variant of
   * @ref diagramscene_serialization_format.
   *
   * @param sceneItems Items to serialize
   * @return JSON payload containing the provided items and related core component state
   */
  [[nodiscard]] nlohmann::ordered_json
  serializeItems(const std::vector<QGraphicsItem*>& sceneItems) const;

  [[nodiscard]] GraphicalItem* findGraphicalItemByUiId(uint64_t uiId) const;
  void                         registerGraphicalItem(GraphicalItem* item);
  void                         unregisterGraphicalItem(GraphicalItem* item);
  [[nodiscard]] bool itemCollisionChecksEnabled() const { return isItemCollisionEnabled; }
  void setItemCollisionChecksEnabled(bool enabled) { isItemCollisionEnabled = enabled; }

  /**
   * @brief Deserializes a full scene from JSON.
   *
   * Expects the payload documented in @ref diagramscene_serialization_format.
   *
   * @param jsonStr JSON string
   * @param guiFactory Factory for creating graphical components
   * @param coreRegistry Registry for creating core components
   */
  void deserialize(const std::string& jsonStr, GUIComponentFactory& guiFactory,
                   const ComponentRegistry& coreRegistry);

  /**
   * @brief Inserts a clipboard selection payload into the current scene.
   *
   * The payload's top-left origin is mapped to @p targetOrigin. Inserted items are
   * selected and reconnected through the scene's wire topology.
   *
   * The payload format is the scene-fragment clipboard form documented in
   * serializeSelection(), derived from @ref diagramscene_serialization_format.
   *
   * @param payload Decoded clipboard payload
   * @param guiFactory Factory for creating graphical components
   * @param coreRegistry Registry for creating core components
   * @param targetOrigin Scene position where the payload origin should be placed
   * @param isPaste True iff is called by a paste action, remaps items ids
   * @return True when any item was inserted
   */
  bool insertSelection(const nlohmann::json& payload, GUIComponentFactory& guiFactory,
                       const ComponentRegistry& coreRegistry, QPointF targetOrigin,
                       bool isPaste = false);

  /**
   * @brief Removes the given scene items and refreshes the logical topology.
   *
   * Callers should pass only top-level user-editable items.
   */
  void removeItems(const std::vector<QGraphicsItem*>& sceneItems);

  /**
   * @brief Automatically places components and reroutes wires.
   *
   * The scene preserves its authoritative logical circuit when available, computes
   * component positions with OGDF, and replaces WireManager's graphical wire segments
   * with libavoid-routed paths carrying the original buses.
   */
  void autoPlaceCircuit(bool interactive = false);

  /**
   * @brief Removes items matching a serialized selection payload from the current scene.
   *
   * Matching is performed against the same visual serialization used for clipboard and
   * undo operations.
   *
   * @param payload Serialized selection payload
   * @return True when any matching item was removed
   */
  bool removeSelection(const nlohmann::json& payload);

  void clear(bool clearUndoStack = true, bool clearLogs = true);
  void updateSceneAfterEdit();

  /**
   * @brief Enables or disables simulation FST tracing.
   *
   * When enabled during simulation mode, the active simulator immediately starts writing
   * snapshots to the configured file. When enabled before entering simulation mode, the
   * writer is attached during simulation initialization.
   */
  void setFstTraceFile(std::optional<std::string> fileName);

  [[nodiscard]] bool isFstTracingEnabled() const;

  /**
   * @brief Simulates the circuit using an edited input waveform.
   * @param duration Total waveform duration to simulate
   * @param inputSnapshots Input-only timestamped values ordered like waveform inputs
   */
  void simulateEditedWaveform(qulonglong                         duration,
                              std::vector<Sample> inputSnapshots);

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
   * and wires based on spatial collisions. If assigning a wire bus to a component
   * input fails, an error message is shown and the partially calculated topology must
   * not be used to start or refresh simulation.
   *
   * @return True when all component inputs and outputs were assigned successfully.
   */
  [[nodiscard]] bool calculateWiresForComponents();

  /**
   * @brief Clears visual input-assignment error markers from all ports.
   */
  void clearInputAssignmentErrors();

signals:
  /**
   * @brief Emitted when the interaction mode changes.
   * @param mode The new mode
   */
  void modeChanged(InteractionMode mode);

  /**
   * @brief Emitted when interactive waveform signals should be rebuilt.
   * @param signalNames Ordered signal labels
   * @param inputCount Number of leading signals belonging to the input group
   * @param signalWidths Width of each signal in bits
   */
  void waveformTraceReset(QStringList signalNames, int inputCount,
                          QList<int> signalWidths);

  /**
   * @brief Emitted when the simulator reaches a new visible waveform state.
   */
  void waveformTraceSnapshot(qulonglong time, QStringList values);

  /**
   * @brief Emitted when a completed simulation job produced multiple waveform states.
   * @param snapshots Ordered timestamp and signal-value snapshots
   */
  void waveformTraceSnapshots(QList<QPair<qulonglong, QStringList>> snapshots);

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
  [[nodiscard]] bool isWireCompletionPoint(QPointF scenePoint) const;
  void finalizeWireCreation();
  void enterComponentPlacingMode();
  void exitComponentPlacingMode();
  bool enterSimulationMode();
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

  /** @brief Whether simulation-mode IO clicks may mutate input state. */
  bool ioInteractionsEnabled = true;

  /** @brief Whether this scene currently represents a subcircuit document. */
  bool subcircuitDocumentMode = false;

  /** @brief Component being placed (shadow) */
  GraphicalComponent* componentToBeDrawn = nullptr;

  /** @brief Suppresses the quick search popup for catalog-driven placement. */
  bool suppressNextComponentSearch = false;

  /** @brief Wire segment being drawn */
  GraphicalWireSegment* wireSegmentToBeDrawn = nullptr;

  /** @brief Component search box */
  ComponentSearchBox* csb = nullptr;

  /** @brief Wire manager for wire topology */
  WireManager wireManager;

  /** @brief Last placed component type for repeat placement */
  std::string lastPlacedComponentType;

  /** @brief Initial properties used when repeating placement of the last type. */
  PropertyMap lastPlacedComponentProperties;

  /** @brief Underlying logic circuit model */
  std::shared_ptr<Circuit> circuit;

  /** @brief O(1) lookup table for runtime graphical items by stable UI id */
  std::unordered_map<uint64_t, GraphicalItem*> itemsByUiId;

  /** @brief Simulation lifecycle and trace management */
  std::unique_ptr<DiagramSceneSimulationController> simulationController;

  /** @brief Persistence and clipboard payload handling */
  std::unique_ptr<DiagramSceneSerializer> serializer;

  /** @brief Disabled while applying batch layouts that must not be rejected mid-move. */
  bool isItemCollisionEnabled = true;
};

/**
 * @brief Alias for DiagramScene::InteractionMode
 */
using InteractionMode = DiagramScene::InteractionMode;

}  // namespace ui
}  // namespace SILICON
