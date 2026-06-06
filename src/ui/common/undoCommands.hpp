#pragma once

#include <QByteArray>
#include <QPointF>
#include <QUndoCommand>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/component.hpp>
#include <ui/common/graphicalWire.hpp>

class GraphicalItem;
class GraphicalComponent;
class GraphicalLogicComponent;
class DiagramScene;

class MoveItemCommand : public QUndoCommand {
public:
  explicit MoveItemCommand(QUndoCommand* parent = nullptr) : QUndoCommand(parent)
  {
    setText("Move Item(s)");
  }

  void addItemMove(GraphicalItem* item, const QPointF& oldPos, const QPointF& newPos);

  void undo() override;
  void redo() override;

private:
  struct ItemMove {
    DiagramScene* scene;
    uint64_t      uiId;
    QPointF       oldPos;
    QPointF       newPos;
  };
  std::vector<ItemMove> moves;
  bool                  skipInitialRedo = true;
};

class MoveWirePointCommand : public QUndoCommand {
public:
  explicit MoveWirePointCommand(GraphicalWireSegment* segment, size_t pointIndex,
                                const QPointF& oldPos, const QPointF& newPos,
                                QUndoCommand* parent = nullptr);

  void undo() override;
  void redo() override;

private:
  DiagramScene* scene;
  uint64_t      uiId;
  size_t        pointIndex;
  QPointF       oldPos;
  QPointF       newPos;
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
  QByteArray    bsonPayload;
  Operation     operation;
  bool          skipInitialRedo;
};
