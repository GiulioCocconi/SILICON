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

#include <QByteArray>
#include <QPointF>
#include <QUndoCommand>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/component.hpp>
#include <ui/circuit/diagram/graphicalWire.hpp>
#include <ui/circuit/diagram/wireManager.hpp>
#include <ui/circuit/diagram/scene/diagramSceneEdit.hpp>

namespace SILICON::ui {
  using namespace SILICON::core;

  class GraphicalItem;
  class GraphicalComponent;
  class GraphicalLogicComponent;
  class DiagramScene;

  class WireTopologyCommand : public QUndoCommand {
  public:
    enum class Operation { Add, Remove };
    /**
     * @brief Captures segments for undoing or redoing wire addition or removal.
     * @param scene Scene containing the segments.
     * @param segments Segments whose geometry, identifiers, and buses are captured.
     * @param operation Whether the original action adds or removes segments.
     * @param skipInitialRedo Whether the action was already applied before pushing the command.
     * @param parent Optional parent undo command.
     */
    WireTopologyCommand(DiagramScene* scene,
                        const std::vector<GraphicalWireSegment*>& segments,
                        Operation operation, bool skipInitialRedo = true,
                        QUndoCommand* parent = nullptr);
    /** @brief Reverses the captured wire addition or removal. */
    void undo() override;
    /** @brief Reapplies the operation, optionally skipping the first invocation. */
    void redo() override;

  private:
    struct Snapshot {
      uint64_t uiId;
      uint64_t routeId;
      Bus bus;
      QPointF position;
      std::vector<QPointF> points;
    };
    /**
     * @brief Restores captured segments or removes them from their original document.
     * @param restore True restores the snapshots; false removes their scene items.
     */
    void apply(bool restore);
    DiagramScene* scene;
    std::string documentPath;
    std::vector<Snapshot> snapshots;
    Operation operation;
    bool skipInitialRedo;
  };

  /** Undo command backed by caller-provided state transition callbacks. */
  class CallbackUndoCommand : public QUndoCommand {
  public:
    using Callback = std::function<void()>;

    CallbackUndoCommand(QString text, Callback undoCallback, Callback redoCallback,
                        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

  private:
    Callback undoCallback;
    Callback redoCallback;
  };

  class MetadataEditCommand : public QUndoCommand {
  public:
    using ApplyFn = std::function<void(const std::string&)>;

    MetadataEditCommand(QString text, std::string oldValue, std::string newValue,
                        ApplyFn apply, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

  private:
    std::string oldValue;
    std::string newValue;
    ApplyFn     apply;
  };

  class MoveItemCommand : public QUndoCommand {
  public:
    explicit MoveItemCommand(QUndoCommand* parent = nullptr) : QUndoCommand(parent)
    {
      setText("Move Item(s)");
    }

    void addItemMove(GraphicalItem* item, const QPointF& oldPos, const QPointF& newPos);
    /** @brief Stores before and after wire routes accompanying the item moves. */
    void setWireMoves(std::vector<WireRouteChange> changes)
    {
      wireMoves = std::move(changes);
    }
    /** @brief Stores topology and net snapshots accompanying the item moves. */
    void setTopologyEdit(WireEndpointEdit edit) { topologyEdit = std::move(edit); }

    void undo() override;
    void redo() override;

  private:
    DiagramScene* scene = nullptr;
    std::vector<ItemPositionChange> moves;
    std::vector<WireRouteChange> wireMoves;
    WireEndpointEdit topologyEdit;
    std::string           documentPath;
    bool                  skipInitialRedo = true;
  };

  class EditWireRouteCommand : public QUndoCommand {
  public:
    /**
     * @brief Captures an already applied wire gesture for undo and redo.
     * @param segment Edited segment used to locate the scene and document.
     * @param changes Before and after route snapshots for affected segments.
     * @param kind Handle type used to label the command.
     * @param endpointEdit Contact, topology, and net snapshots for the gesture.
     * @param parent Optional parent undo command.
     * @note The first redo is skipped because the gesture is already applied.
     */
    explicit EditWireRouteCommand(GraphicalWireSegment* segment,
                                  std::vector<WireRouteChange> changes,
                                  WireDragKind kind, WireEndpointEdit endpointEdit = {},
                                  QUndoCommand* parent = nullptr);

    /** @brief Restores the wire routes from before the gesture. */
    void undo() override;
    /** @brief Restores the gesture's final routes, skipping the first invocation. */
    void redo() override;

  private:
    DiagramScene* scene;
    std::string   documentPath;
    std::vector<WireRouteChange> changes;
    WireEndpointEdit endpointEdit;
    bool          skipInitialRedo = true;
  };

  class RotateItemCommand : public QUndoCommand {
  public:
    explicit RotateItemCommand(GraphicalComponent* component, qreal oldRotation,
                               qreal newRotation, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

  private:
    DiagramScene* scene;
    std::string   documentPath;
    uint64_t      uiId;
    qreal         oldRotation;
    qreal         newRotation;
    bool          skipInitialRedo = true;
  };

  class ModifyPropertyCommand : public QUndoCommand {
  public:
    explicit ModifyPropertyCommand(std::string key, QUndoCommand* parent = nullptr);

    void               addPropertyChange(GraphicalLogicComponent* component,
                                         const PropertyValue& oldValue, const PropertyValue& newValue);
    [[nodiscard]] bool isEmpty() const { return changes.empty(); }

    void undo() override;
    void redo() override;

  private:
    struct PropertyChange {
      DiagramScene* scene;
      uint64_t      uiId;
      PropertyValue oldValue;
      PropertyValue newValue;
    };

    void apply(bool useNewValue);

    std::string                 key;
    std::string                 documentPath;
    std::vector<PropertyChange> changes;
  };

  class SceneSelectionCommand : public QUndoCommand {
  public:
    enum class Operation { Add, Remove };

    SceneSelectionCommand(DiagramScene* scene, const nlohmann::json& payload,
                          Operation operation, bool skipInitialRedo = false,
                          QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

  private:
    [[nodiscard]] nlohmann::json payload() const;
    static QByteArray            encodePayload(const nlohmann::json& payload);
    static nlohmann::json        decodePayload(const QByteArray& payload);
    static QPointF               payloadOrigin(const nlohmann::json& payload);

    DiagramScene* scene;
    std::string   documentPath;
    QByteArray    bsonPayload;
    Operation     operation;
    bool          skipInitialRedo;
  };

}  // namespace SILICON::ui
