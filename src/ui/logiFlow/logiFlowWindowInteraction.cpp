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

#include "logiFlowWindow.hpp"

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
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/waveformViewer.hpp>
#include <ui/logiFlow/code/codeEditor.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON {
namespace ui {
  using namespace SILICON::core;

  namespace {

    // Clipboard data is intentionally BSON-only so partial JSON fallbacks cannot drift
    // from the native LogiFlow selection format.
    constexpr auto LogiFlowSelectionMimeType =
        "application/vnd.silicon.logiflow-selection+bson";

    QString interactionModeName(const InteractionMode mode)
    {
      switch (mode) {
        case InteractionMode::NORMAL_MODE: return QStringLiteral("NORMAL");
        case InteractionMode::COMPONENT_PLACING_MODE:
          return QStringLiteral("COMPONENT PLACING");
        case InteractionMode::WIRE_CREATION_MODE: return QStringLiteral("WIRE CREATION");
        case InteractionMode::PAN_MODE: return QStringLiteral("PAN");
        case InteractionMode::SIMULATION_MODE: return QStringLiteral("SIMULATION");
      }

      throw std::logic_error("Unhandled InteractionMode in interactionModeName");
    }

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

  bool LogiFlowWindow::copySelectionToClipboard()
  {
    try {
      const auto payload = diagramScene->serializeSelection();
      if (!hasClipboardItems(payload))
        return false;

      const auto bson = nlohmann::json::to_bson(payload);
      QByteArray bytes(reinterpret_cast<const char*>(bson.data()),
                       static_cast<qsizetype>(bson.size()));

      auto* mimeData = new QMimeData();
      mimeData->setData(LogiFlowSelectionMimeType, bytes);
      QApplication::clipboard()->setMimeData(mimeData);
      updateEditActions();

      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  void LogiFlowWindow::copy()
  {
    if (isVisualizerActive()) return;
    const auto type = activeDocumentType();
    if (type && SILICON::project::isCodeDocument(*type)) {
      activeCodeEditor()->copy();
      return;
    }
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram)
      return;

    copySelectionToClipboard();
  }

  void LogiFlowWindow::cut()
  {
    if (isVisualizerActive()) return;
    const auto type = activeDocumentType();
    if (type && SILICON::project::isCodeDocument(*type)) {
      activeCodeEditor()->cut();
      return;
    }
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram)
      return;

    if (copySelectionToClipboard())
      del();
  }

  void LogiFlowWindow::paste()
  {
    if (isVisualizerActive()) return;
    const auto type = activeDocumentType();
    if (type && SILICON::project::isCodeDocument(*type)) {
      activeCodeEditor()->paste();
      return;
    }
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram)
      return;

    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasFormat(LogiFlowSelectionMimeType))
      return;

    const QByteArray bytes = mimeData->data(LogiFlowSelectionMimeType);
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

      undoStack->push(
          new SceneSelectionCommand(diagramScene, diagramScene->serializeSelection(),
                                    SceneSelectionCommand::Operation::Add, true));
    } catch (const std::exception&) {
    }
  }

  void LogiFlowWindow::rotate()
  {
    std::vector<GraphicalComponent*> selectedComponents;
    for (auto* item : diagramScene->selectedItems()) {
      if (auto* component =
              category_cast<GraphicalComponent>(item, ItemCategory::Component)) {
        selectedComponents.push_back(component);
      }
    }

    switch (diagramScene->getInteractionMode()) {
      case InteractionMode::NORMAL_MODE: {
        if (selectedComponents.size() != 1)
          return;

        auto* component   = selectedComponents.front();
        auto  oldRotation = component->rotation();
        component->rotate();
        auto newRotation = component->rotation();
        component->setInitialRotation();
        auto rotateCmd = new RotateItemCommand(component, oldRotation, newRotation);
        undoStack->push(rotateCmd);
        break;
      }
      case InteractionMode::COMPONENT_PLACING_MODE: {
        diagramScene->getComponentToBeDrawn()->rotate();
        break;
      }

      default: return;
    }
  }

  void LogiFlowWindow::autoPlace()
  {
    diagramScene->autoPlaceCircuit();
  }

  void LogiFlowWindow::del()
  {
    const auto type = activeDocumentType();
    if (type && SILICON::project::isCodeDocument(*type)) {
      auto cursor = activeCodeEditor()->textCursor();
      if (cursor.hasSelection())
        cursor.removeSelectedText();
      else
        cursor.deleteChar();
      activeCodeEditor()->setTextCursor(cursor);
      return;
    }
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram)
      return;

    auto itemsToDelete =
        diagramScene->selectedItems()
        | std::views::filter([](auto* item) { return item->type() > UNKNOWN; })
        | std::ranges::to<std::vector>();
    if (itemsToDelete.empty())
      return;

    const auto payload = diagramScene->serializeItems(itemsToDelete);
    diagramScene->removeItems(itemsToDelete);
    undoStack->push(new SceneSelectionCommand(
        diagramScene, payload, SceneSelectionCommand::Operation::Remove, true));
  }

  void LogiFlowWindow::setNormalMode()
  {
    diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);
  }

  void LogiFlowWindow::setPanMode()
  {
    diagramScene->setInteractionMode(InteractionMode::PAN_MODE);
  }

  void LogiFlowWindow::setWireCreationMode()
  {
    diagramScene->setInteractionMode(InteractionMode::WIRE_CREATION_MODE);
  }

  void LogiFlowWindow::setSimulationMode()
  {
    const auto type = activeDocumentType();
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram)
      return;

    diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
  }

  void LogiFlowWindow::setComponentPlacingMode()
  {
    diagramScene->setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
  }

  void LogiFlowWindow::showComponentCatalog()
  {
    if (!componentCatalogOverlay)
      return;

    updateComponentCatalogGeometry();
    componentCatalogOverlay->open();
  }

  void LogiFlowWindow::cancelCurrentInteraction()
  {
    diagramScene->cancelCurrentInteraction();
  }

  void LogiFlowWindow::toggleFstTracing(const bool enabled)
  {
    if (!waveformWindow)
      return;

    const auto type = activeDocumentType();
    if (enabled
        && (!type
            || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)) {
      const QSignalBlocker blocker(toggleWaveformViewerAct);
      toggleWaveformViewerAct->setChecked(false);
      return;
    }

    waveformWindow->setVisible(enabled);
    if (!enabled)
      return;

    diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
    waveformWindow->raise();
    waveformWindow->activateWindow();
  }

  void LogiFlowWindow::updateStatus() const
  {
    statusBar()->showMessage(
        tr("Interaction Mode: %1")
            .arg(interactionModeName(diagramScene->getInteractionMode())));
  }

  void LogiFlowWindow::selectionChanged()
  {
    updateEditActions();

    if (!diagramScene->selectedItems().empty() && projectTree)
      projectTree->clearDocumentSelection();

    updatePropertyDock();
  }

  void LogiFlowWindow::updateEditActions()
  {
    if (!rotateAct || !cutAct || !copyAct || !pasteAct || !deleteAct)
      return;
    if (isVisualizerActive()) {
      setActionsEnabled({rotateAct, cutAct, copyAct, pasteAct, deleteAct}, false);
      return;
    }

    const auto type = activeDocumentType();
    if (!type
        || SILICON::project::categoryOf(*type)
               != SILICON::project::DocumentCategory::Diagram) {
      rotateAct->setEnabled(false);
      const bool editableText = type && SILICON::project::isCodeDocument(*type);
      setActionsEnabled({cutAct, copyAct, pasteAct, deleteAct}, editableText);
      return;
    }

    const auto interactionMode = diagramScene->getInteractionMode();
    const auto selected        = diagramScene->selectedItems();
    const bool hasSelection    = !selected.empty();

    rotateAct->setEnabled(
        (interactionMode == InteractionMode::NORMAL_MODE && selected.size() == 1)
        || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

    const bool canEditSelection =
        interactionMode == InteractionMode::NORMAL_MODE && hasSelection;
    setActionsEnabled({cutAct, copyAct, deleteAct}, canEditSelection);
    const auto* clipboardData = QApplication::clipboard()->mimeData();
    pasteAct->setEnabled(interactionMode == InteractionMode::NORMAL_MODE && clipboardData
                         && clipboardData->hasFormat(LogiFlowSelectionMimeType));
  }

}  // namespace ui
}  // namespace SILICON
