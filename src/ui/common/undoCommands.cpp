#include "undoCommands.hpp"
#include "graphicalComponent.hpp"
#include "graphicalItem.hpp"
#include "wireManager.hpp"

#include <cstdint>
#include <utility>

#include <core/serialization/component_registry.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace {

DiagramScene* itemScene(const QGraphicsItem* item)
{
  if (!item || !item->scene())
    return nullptr;
  return qobject_cast<DiagramScene*>(item->scene());
}

GraphicalItem* findItem(DiagramScene* scene, const uint64_t uiId)
{
  if (!scene)
    return nullptr;
  return scene->findGraphicalItemByUiId(uiId);
}

GraphicalWireSegment* findWireSegment(DiagramScene* scene, const uint64_t uiId)
{
  return category_cast<GraphicalWireSegment>(findItem(scene, uiId),
                                             ItemCategory::WireSegment);
}

GraphicalComponent* findComponent(DiagramScene* scene, const uint64_t uiId)
{
  return category_cast<GraphicalComponent>(findItem(scene, uiId),
                                           ItemCategory::Component);
}

GraphicalLogicComponent* findLogicComponent(DiagramScene* scene, const uint64_t uiId)
{
  return category_cast<GraphicalLogicComponent>(findItem(scene, uiId),
                                                ItemCategory::LogicComponent);
}

}  // namespace

// --- MoveItemCommand ---

void MoveItemCommand::addItemMove(GraphicalItem* item, const QPointF& oldPos,
                                  const QPointF& newPos)
{
  if (!item)
    return;

  // Commands store scene + uiId instead of raw item pointers so undo survives
  // delete/recreate cycles triggered by selection-level commands.
  moves.push_back({itemScene(item), item->getUiId(), oldPos, newPos});
}

void MoveItemCommand::undo()
{
  for (const auto& move : moves) {
    if (auto* item = findItem(move.scene, move.uiId)) {
      item->setPos(move.oldPos);
      item->setInitialPosition();
      item->updateTopology();
    }
  }
}

void MoveItemCommand::redo()
{
  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }

  for (const auto& move : moves) {
    if (auto* item = findItem(move.scene, move.uiId)) {
      item->setPos(move.newPos);
      item->setInitialPosition();
      item->updateTopology();
    }
  }
}

// --- MoveWirePointCommand ---

MoveWirePointCommand::MoveWirePointCommand(GraphicalWireSegment* segment,
                                           const size_t pointIndex, const QPointF& oldPos,
                                           const QPointF& newPos, QUndoCommand* parent)
  : QUndoCommand(parent),
    scene(itemScene(segment)),
    uiId(segment ? segment->getUiId() : 0),
    pointIndex(pointIndex),
    oldPos(oldPos),
    newPos(newPos)
{
  setText("Move Wire Point");
}

void MoveWirePointCommand::undo()
{
  if (auto* segment = findWireSegment(scene, uiId)) {
    segment->movePointTo(pointIndex, oldPos);

    // Update topology for point modification
    if (auto* wire = segment->getGraphicalWire()) {
      if (auto* manager = wire->getManager()) {
        manager->updateSegmentTopology(segment);
      }
    }
  }
}

void MoveWirePointCommand::redo()
{
  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }

  if (auto* segment = findWireSegment(scene, uiId)) {
    segment->movePointTo(pointIndex, newPos);

    // Update topology for point modification
    if (auto* wire = segment->getGraphicalWire()) {
      if (auto* manager = wire->getManager()) {
        manager->updateSegmentTopology(segment);
      }
    }
  }
}

// --- RotateItemCommand ---

RotateItemCommand::RotateItemCommand(GraphicalComponent* component,
                                     const qreal oldRotation, const qreal newRotation,
                                     QUndoCommand* parent)
  : QUndoCommand(parent),
    scene(itemScene(component)),
    uiId(component ? component->getUiId() : 0),
    oldRotation(oldRotation),
    newRotation(newRotation)
{
  setText("Rotate Component");
}

void RotateItemCommand::undo()
{
  if (auto* component = findComponent(scene, uiId)) {
    component->setRotation(oldRotation);
    component->setInitialRotation();
    component->update();
  }
}

void RotateItemCommand::redo()
{
  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }

  if (auto* component = findComponent(scene, uiId)) {
    component->setRotation(newRotation);
    component->setInitialRotation();
    component->update();
  }
}

// --- ModifyPropertyCommand ---

ModifyPropertyCommand::ModifyPropertyCommand(std::string key, QUndoCommand* parent)
  : QUndoCommand(parent), key(std::move(key))
{
  setText("Modify Property");
}

void ModifyPropertyCommand::addPropertyChange(GraphicalLogicComponent* component,
                                              const PropertyValue&     oldValue,
                                              const PropertyValue&     newValue)
{
  if (!component || oldValue == newValue)
    return;

  changes.push_back({itemScene(component), component->getUiId(), oldValue, newValue});
}

void ModifyPropertyCommand::apply(const bool useNewValue)
{
  for (const auto& change : changes) {
    if (auto* component = findLogicComponent(change.scene, change.uiId)) {
      component->applyProperty(key, useNewValue ? change.newValue : change.oldValue);
    }
  }
}

void ModifyPropertyCommand::undo()
{
  apply(false);
}

void ModifyPropertyCommand::redo()
{
  apply(true);
}

// --- SceneSelectionCommand ---

SceneSelectionCommand::SceneSelectionCommand(DiagramScene*         scene,
                                             const nlohmann::json& payload,
                                             const Operation       operation,
                                             const bool            skipInitialRedo,
                                             QUndoCommand*         parent)
  : QUndoCommand(parent),
    scene(scene),
    bsonPayload(encodePayload(payload)),
    operation(operation),
    skipInitialRedo(skipInitialRedo)
{
  setText(operation == Operation::Add ? "Add Selection" : "Remove Selection");
}

nlohmann::json SceneSelectionCommand::payload() const
{
  return decodePayload(bsonPayload);
}

QByteArray SceneSelectionCommand::encodePayload(const nlohmann::json& payload)
{
  const auto bson = nlohmann::json::to_bson(payload);
  return {reinterpret_cast<const char*>(bson.data()),
          static_cast<QByteArray::size_type>(bson.size())};
}

nlohmann::json SceneSelectionCommand::decodePayload(const QByteArray& payload)
{
  const auto* ptr = reinterpret_cast<const std::uint8_t*>(payload.constData());
  return nlohmann::json::from_bson(ptr, ptr + payload.size());
}

QPointF SceneSelectionCommand::payloadOrigin(const nlohmann::json& payload)
{
  if (!payload.contains("origin") || !payload["origin"].is_object())
    return {};

  return {payload["origin"].value("x", 0.0), payload["origin"].value("y", 0.0)};
}

void SceneSelectionCommand::undo()
{
  if (!scene)
    return;

  if (scene->getInteractionMode() != InteractionMode::NORMAL_MODE)
    scene->setInteractionMode(InteractionMode::NORMAL_MODE);

  const auto selectionPayload = payload();

  if (operation == Operation::Add) {
    // Undoing an insertion removes the exact serialized objects by their stable UI ids.
    scene->removeSelection(selectionPayload);
    return;
  }

  scene->insertSelection(selectionPayload, GUIComponentFactory::instance(),
                         ComponentRegistry::instance(), payloadOrigin(selectionPayload));
}

void SceneSelectionCommand::redo()
{
  if (!scene)
    return;

  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }

  if (scene->getInteractionMode() != InteractionMode::NORMAL_MODE)
    scene->setInteractionMode(InteractionMode::NORMAL_MODE);

  const auto selectionPayload = payload();

  if (operation == Operation::Add) {
    // Redo rebuilds the serialized selection in place without generating fresh paste ids.
    scene->insertSelection(selectionPayload, GUIComponentFactory::instance(),
                           ComponentRegistry::instance(),
                           payloadOrigin(selectionPayload));
    return;
  }

  scene->removeSelection(selectionPayload);
}
