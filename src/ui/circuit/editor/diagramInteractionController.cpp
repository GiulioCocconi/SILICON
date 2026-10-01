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

#include "diagramInteractionController.hpp"
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectSession.hpp>

#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QMimeData>
#include <QPointF>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/waveform/waveformViewer.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/circuit/editor/componentCatalogOverlay.hpp>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/circuit/components/subcircuit/utils.hpp>
#include <ui/project/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {
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

DiagramInteractionController::DiagramInteractionController(
    ProjectSession& session, EditorWorkspace& workspace, ComponentCatalogOverlay& catalog,
    QUndoStack& history, QObject* parent)
  : QObject(parent),
    session(session),
    workspace(workspace),
    catalog(catalog),
    undoStack(history)
{
}

bool DiagramInteractionController::copySelectionToClipboard()
{
  try {
    const auto payload = workspace.scene()->serializeSelection();
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

void DiagramInteractionController::copy()
{
  if (workspace.isVisualizerActive())
    return;
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (type && SILICON::project::isCodeDocument(*type)) {
    workspace.activeCodeEditor()->copy();
    return;
  }
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

  copySelectionToClipboard();
}

void DiagramInteractionController::cut()
{
  if (workspace.isVisualizerActive())
    return;
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (type && SILICON::project::isCodeDocument(*type)) {
    workspace.activeCodeEditor()->cut();
    return;
  }
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

  if (copySelectionToClipboard())
    del();
}

void DiagramInteractionController::paste()
{
  if (workspace.isVisualizerActive())
    return;
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (type && SILICON::project::isCodeDocument(*type)) {
    workspace.activeCodeEditor()->paste();
    return;
  }
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

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

    if (workspace.scene()->getInteractionMode() != InteractionMode::NORMAL_MODE)
      workspace.scene()->setInteractionMode(InteractionMode::NORMAL_MODE);

    const QPointF targetOrigin =
        workspace.view()->mapToScene(workspace.view()->mapFromGlobal(QCursor::pos()));
    if (!workspace.scene()->insertSelection(payload, guiFactory, coreRegistry,
                                             targetOrigin, true))
      return;

    undoStack.push(new SceneSelectionCommand(
        workspace.scene(), workspace.scene()->serializeSelection(),
        SceneSelectionCommand::Operation::Add, true));
  } catch (const std::exception&) {
  }
}

void DiagramInteractionController::rotate()
{
  std::vector<GraphicalComponent*> selectedComponents;
  for (auto* item : workspace.scene()->selectedItems()) {
    if (auto* component =
            category_cast<GraphicalComponent>(item, ItemCategory::Component)) {
      selectedComponents.push_back(component);
    }
  }

  switch (workspace.scene()->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: {
      if (selectedComponents.size() != 1)
        return;

      auto* component   = selectedComponents.front();
      auto  oldRotation = component->rotation();
      component->rotate();
      auto newRotation = component->rotation();
      component->setInitialRotation();
      auto rotateCmd = new RotateItemCommand(component, oldRotation, newRotation);
      undoStack.push(rotateCmd);
      break;
    }
    case InteractionMode::COMPONENT_PLACING_MODE: {
      workspace.scene()->getComponentToBeDrawn()->rotate();
      break;
    }

    default: return;
  }
}

void DiagramInteractionController::autoPlace()
{
  workspace.scene()->autoPlaceCircuit();
}

void DiagramInteractionController::del()
{
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (type && SILICON::project::isCodeDocument(*type)) {
    auto cursor = workspace.activeCodeEditor()->textCursor();
    if (cursor.hasSelection())
      cursor.removeSelectedText();
    else
      cursor.deleteChar();
    workspace.activeCodeEditor()->setTextCursor(cursor);
    return;
  }
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

  auto itemsToDelete =
      workspace.scene()->selectedItems()
      | std::views::filter([](auto* item) { return item->type() > UNKNOWN; })
      | std::ranges::to<std::vector>();
  if (itemsToDelete.empty())
    return;

  const auto payload = workspace.scene()->serializeItems(itemsToDelete);
  workspace.scene()->removeItems(itemsToDelete);
  undoStack.push(new SceneSelectionCommand(
      workspace.scene(), payload, SceneSelectionCommand::Operation::Remove, true));
}

void DiagramInteractionController::setNormalMode()
{
  workspace.scene()->setInteractionMode(InteractionMode::NORMAL_MODE);
}

void DiagramInteractionController::setPanMode()
{
  workspace.scene()->setInteractionMode(InteractionMode::PAN_MODE);
}

void DiagramInteractionController::setWireCreationMode()
{
  workspace.scene()->setInteractionMode(InteractionMode::WIRE_CREATION_MODE);
}

void DiagramInteractionController::setSimulationMode()
{
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

  workspace.scene()->setInteractionMode(InteractionMode::SIMULATION_MODE);
}

void DiagramInteractionController::setComponentPlacingMode()
{
  workspace.scene()->setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
}

void DiagramInteractionController::showComponentCatalog()
{
  catalog.setGeometry(workspace.view()->viewport()->rect());
  catalog.open();
}

void DiagramInteractionController::cancelCurrentInteraction()
{
  workspace.scene()->cancelCurrentInteraction();
}

}  // namespace SILICON::ui
