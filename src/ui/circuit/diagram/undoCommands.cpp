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

#include "undoCommands.hpp"

#include <cstdint>
#include <utility>

#include <core/serialization/component_registry.hpp>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/documents/documentNavigator.hpp>
#include <ui/circuit/diagram/graphicalComponent.hpp>
#include <ui/circuit/diagram/graphicalItem.hpp>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  DiagramScene* itemScene(const QGraphicsItem* item)
  {
    if (!item || !item->scene())
      return nullptr;
    return qobject_cast<DiagramScene*>(item->scene());
  }

  std::string activeDocumentPath(DiagramScene* scene)
  {
    if (auto* navigator = scene ? scene->getDocumentNavigator() : nullptr)
      return navigator->currentDocumentPath();

    return {};
  }

  bool activateDocument(DiagramScene* scene, const std::string& documentPath)
  {
    if (documentPath.empty())
      return true;

    if (auto* navigator = scene ? scene->getDocumentNavigator() : nullptr)
      return navigator->activateDocument(documentPath);

    return false;
  }

  GraphicalItem* findItem(DiagramScene* scene, const uint64_t uiId)
  {
    if (!scene)
      return nullptr;
    return scene->findGraphicalItemByUiId(uiId);
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

CallbackUndoCommand::CallbackUndoCommand(QString text, Callback undoCallback,
                                         Callback redoCallback, QUndoCommand* parent)
  : QUndoCommand(std::move(text), parent),
    undoCallback(std::move(undoCallback)),
    redoCallback(std::move(redoCallback))
{
}

void CallbackUndoCommand::undo()
{
  undoCallback();
}

void CallbackUndoCommand::redo()
{
  redoCallback();
}

// --- MoveItemCommand ---

void MoveItemCommand::addItemMove(GraphicalItem* item, const QPointF& oldPos,
                                  const QPointF& newPos)
{
  if (!item)
    return;
  if (!scene)
    scene = itemScene(item);
  if (documentPath.empty())
    documentPath = activeDocumentPath(scene);
  moves.push_back({item->getUiId(), oldPos, newPos});
}

void MoveItemCommand::undo()
{
  if (scene && activateDocument(scene, documentPath))
    scene->applyItemMoves(moves, wireMoves, false);
}

void MoveItemCommand::redo()
{
  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }
  if (scene && activateDocument(scene, documentPath))
    scene->applyItemMoves(moves, wireMoves, true);
}

// --- EditWireRouteCommand ---

EditWireRouteCommand::EditWireRouteCommand(GraphicalWireSegment* segment,
                                           std::vector<WireRouteChange> routeChanges,
                                           const bool movedEdge, QUndoCommand* parent)
  : QUndoCommand(parent),
    scene(itemScene(segment)),
    documentPath(activeDocumentPath(scene)),
    changes(std::move(routeChanges))
{
  setText(movedEdge ? "Move Wire Segment" : "Move Wire Bend");
}

void EditWireRouteCommand::undo()
{
  if (!activateDocument(scene, documentPath))
    return;

  if (scene)
    scene->applyWireRouteChanges(changes, false);
}

void EditWireRouteCommand::redo()
{
  if (skipInitialRedo) {
    skipInitialRedo = false;
    return;
  }

  if (!activateDocument(scene, documentPath))
    return;

  if (scene)
    scene->applyWireRouteChanges(changes, true);
}

// --- RotateItemCommand ---

RotateItemCommand::RotateItemCommand(GraphicalComponent* component,
                                     const qreal oldRotation, const qreal newRotation,
                                     QUndoCommand* parent)
  : QUndoCommand(parent),
    scene(itemScene(component)),
    documentPath(activeDocumentPath(scene)),
    uiId(component ? component->getUiId() : 0),
    oldRotation(oldRotation),
    newRotation(newRotation)
{
  setText("Rotate Component");
}

void RotateItemCommand::undo()
{
  if (!activateDocument(scene, documentPath))
    return;

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

  if (!activateDocument(scene, documentPath))
    return;

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

  auto* scene = itemScene(component);
  if (documentPath.empty())
    documentPath = activeDocumentPath(scene);
  changes.push_back({scene, component->getUiId(), oldValue, newValue});
}

void ModifyPropertyCommand::apply(const bool useNewValue)
{
  if (!changes.empty() && !activateDocument(changes.front().scene, documentPath))
    return;

  bool applied = false;
  for (const auto& change : changes) {
    if (auto* component = findLogicComponent(change.scene, change.uiId)) {
      component->applyProperty(key, useNewValue ? change.newValue : change.oldValue);
      applied = true;
    }
  }

  if (applied && changes.front().scene)
    changes.front().scene->updateSceneAfterEdit();
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
    documentPath(activeDocumentPath(scene)),
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

  if (!activateDocument(scene, documentPath))
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

  if (!activateDocument(scene, documentPath))
    return;

  if (scene->getInteractionMode() != InteractionMode::NORMAL_MODE)
    scene->setInteractionMode(InteractionMode::NORMAL_MODE);

  const auto selectionPayload = payload();

  if (operation == Operation::Add) {
    // Redo rebuilds the serialized selection in place without generating fresh paste
    // ids.
    scene->insertSelection(selectionPayload, GUIComponentFactory::instance(),
                           ComponentRegistry::instance(),
                           payloadOrigin(selectionPayload));
    return;
  }

  scene->removeSelection(selectionPayload);
}
MetadataEditCommand::MetadataEditCommand(QString text, std::string oldValue,
                                         std::string newValue, ApplyFn apply,
                                         QUndoCommand* parent)
  : QUndoCommand(std::move(text), parent),
    oldValue(std::move(oldValue)),
    newValue(std::move(newValue)),
    apply(std::move(apply))
{
}

void MetadataEditCommand::undo()
{
  apply(oldValue);
}

void MetadataEditCommand::redo()
{
  apply(newValue);
}

}  // namespace SILICON::ui
