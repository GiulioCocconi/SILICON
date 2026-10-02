/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "circuitEditor.hpp"

#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCursor>
#include <QMimeData>
#include <QPointF>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/circuit/components/subcircuit/metadata.hpp>
#include <ui/circuit/components/subcircuit/utils.hpp>
#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/scene/diagramSceneSerializer.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
namespace {
  /** Clipboard format carrying a serialized circuit selection. */
  constexpr char CIRCUIT_SELECTION_MIME_TYPE[] =
      "application/vnd.silicon.circuit-selection+bson";

  struct PreparedCircuit final : PreparedEditorDocument {
    std::shared_ptr<SceneLoadPlan> plan;
  };

  bool hasClipboardItems(const nlohmann::json& payload)
  {
    if (!payload.contains("visual") || !payload["visual"].is_object())
      return false;

    const auto& visual        = payload["visual"];
    const bool  hasComponents = visual.contains("components")
                               && visual["components"].is_array()
                               && !visual["components"].empty();
    const bool hasWires = visual.contains("wires") && visual["wires"].is_array()
                          && !visual["wires"].empty();

    return hasComponents || hasWires;
  }
}  // namespace

CircuitEditor::CircuitEditor(ProjectSession& session, QWidget* parent,
                             EditorNotifications notifications)
  : session(session),
    diagramScene(new DiagramScene(parent)),
    diagramView(new DiagramView(parent))
{
  diagramScene->setDocumentStore(&session.projectContext.documents());
  diagramScene->setCircuitResolver(&session.circuitResolver);
  diagramView->setScene(diagramScene);

  // Selection and interaction mode both decide which diagram commands apply, so the scene
  // reports them through the workspace. The clipboard is not observed here: the workspace
  // watches it once and this editor only answers what its own contents mean.
  const auto reportEditState = [notifications] { notifications.editStateChanged(); };
  QObject::connect(diagramScene, &DiagramScene::selectionChanged, diagramScene,
                   reportEditState);
  QObject::connect(diagramScene, &DiagramScene::modeChanged, diagramScene,
                   reportEditState);
}

CircuitEditor::~CircuitEditor()
{
  diagramView->setScene(nullptr);
  delete diagramScene;
}

QWidget* CircuitEditor::widget() const noexcept
{
  return diagramView;
}

std::shared_ptr<PreparedEditorDocument>
CircuitEditor::prepare(const SILICON::project::Document& document)
{
  auto prepared  = std::make_shared<PreparedCircuit>();
  prepared->plan = diagramScene->prepareDeserialize(
      document.getContents(), GUIComponentFactory::instance(),
      SILICON::core::ComponentRegistry::instance());
  return prepared;
}

void CircuitEditor::apply(const PreparedEditorDocument& prepared)
{
  const auto* circuit = dynamic_cast<const PreparedCircuit*>(&prepared);
  if (!circuit || !circuit->plan)
    throw std::logic_error("Prepared circuit document has no load plan");
  diagramScene->clear(false, false);
  diagramScene->setSubcircuitDocumentMode(true);
  diagramScene->applyDeserialize(circuit->plan);
}

void CircuitEditor::flush(const std::string& path)
{
  auto serialized = diagramScene->serialize();
  if (const auto* existing = session.projectContext.documents().find(path)) {
    try {
      auto       json     = nlohmann::json::parse(serialized);
      const auto fallback = parseGraphicalSubcircuitMetadata(existing->getContents())
                                .value_or(GraphicalSubcircuitMetadata{});
      json["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
          synchronizeGraphicalSubcircuitMetadata(serialized, fallback));
      serialized = json.dump(2);
    } catch (const nlohmann::json::exception&) {
    }
  }
  session.projectContext.upsertDocument({path, std::move(serialized)});
}

bool CircuitEditor::isDirty() const
{
  return projectHistory && !projectHistory->isClean();
}
void CircuitEditor::resetDirtyState() noexcept {}
void CircuitEditor::reset()
{
  diagramScene->clear();
  diagramScene->setDocumentCircuit(std::make_shared<SILICON::core::Circuit>());
  diagramScene->setSubcircuitDocumentMode(false);
}
void CircuitEditor::undo()
{
  if (projectHistory)
    projectHistory->undo();
}
void CircuitEditor::redo()
{
  if (projectHistory)
    projectHistory->redo();
}
bool CircuitEditor::canUndo() const
{
  return projectHistory && projectHistory->canUndo();
}
bool CircuitEditor::canRedo() const
{
  return projectHistory && projectHistory->canRedo();
}

bool CircuitEditor::copySelectionToClipboard()
{
  try {
    const auto payload = diagramScene->serializeSelection();
    if (!hasClipboardItems(payload))
      return false;

    const auto bson = nlohmann::json::to_bson(payload);
    QByteArray bytes(reinterpret_cast<const char*>(bson.data()),
                     static_cast<qsizetype>(bson.size()));

    auto* mimeData = new QMimeData();
    mimeData->setData(CIRCUIT_SELECTION_MIME_TYPE, bytes);
    QApplication::clipboard()->setMimeData(mimeData);

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void CircuitEditor::copy()
{
  copySelectionToClipboard();
}

void CircuitEditor::cut()
{
  if (copySelectionToClipboard())
    deleteSelection();
}

void CircuitEditor::paste()
{
  const QMimeData* mimeData = QApplication::clipboard()->mimeData();
  if (!mimeData || !mimeData->hasFormat(CIRCUIT_SELECTION_MIME_TYPE))
    return;

  const QByteArray bytes = mimeData->data(CIRCUIT_SELECTION_MIME_TYPE);
  if (bytes.isEmpty())
    return;

  try {
    auto&      guiFactory   = GUIComponentFactory::instance();
    auto&      coreRegistry = ComponentRegistry::instance();
    const auto payload      = nlohmann::json::from_bson(
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));

    if (diagramScene->getInteractionMode() != InteractionMode::NORMAL_MODE)
      diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);

    const QPointF targetOrigin =
        diagramView->mapToScene(diagramView->mapFromGlobal(QCursor::pos()));
    if (!diagramScene->insertSelection(payload, guiFactory, coreRegistry, targetOrigin,
                                       true))
      return;

    if (projectHistory)
      projectHistory->push(
          new SceneSelectionCommand(diagramScene, diagramScene->serializeSelection(),
                                    SceneSelectionCommand::Operation::Add, true));
  } catch (const std::exception&) {
  }
}

void CircuitEditor::deleteSelection()
{
  auto itemsToDelete =
      diagramScene->selectedItems()
      | std::views::filter([](auto* item) { return item->type() > UNKNOWN; })
      | std::ranges::to<std::vector>();
  if (itemsToDelete.empty())
    return;

  const auto payload = diagramScene->serializeItems(itemsToDelete);
  diagramScene->removeItems(itemsToDelete);
  if (projectHistory)
    projectHistory->push(new SceneSelectionCommand(
        diagramScene, payload, SceneSelectionCommand::Operation::Remove, true));
}

EditorEditState CircuitEditor::editState() const
{
  const auto interactionMode = diagramScene->getInteractionMode();
  const bool normalMode      = interactionMode == InteractionMode::NORMAL_MODE;
  const auto selected        = diagramScene->selectedItems();

  EditorEditState state;
  state.canCut = state.canCopy = state.canDelete = normalMode && !selected.empty();
  state.canRotate                                = (normalMode && selected.size() == 1)
                    || interactionMode == InteractionMode::COMPONENT_PLACING_MODE;

  const auto* clipboardData = QApplication::clipboard()->mimeData();
  state.canPaste            = normalMode && clipboardData
                   && clipboardData->hasFormat(CIRCUIT_SELECTION_MIME_TYPE);
  return state;
}

}  // namespace SILICON::ui
