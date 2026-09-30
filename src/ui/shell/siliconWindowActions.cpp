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

#include <ui/shell/siliconWindow.hpp>

#include <stdexcept>

#include <QAction>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QUndoStack>

#include <ui/circuit/components/subcircuit/componentShapeEditor.hpp>
#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/binary/binaryEditor.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/shell/aboutDialog.hpp>
#include <ui/shell/inputDialogUtils.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

void SiliconWindow::wireActions()
{
  connect(actionSet->newAct, &QAction::triggered, fileController,
          &ProjectFileController::newFile);
  connect(actionSet->newCircuitAct, &QAction::triggered, documentController,
          &ProjectDocumentController::createCircuit);
  connect(actionSet->newCodeFileAct, &QAction::triggered, documentController,
          &ProjectDocumentController::createCodeFile);
  connect(actionSet->newArchitectureAct, &QAction::triggered, documentController,
          &ProjectDocumentController::createArchitecture);
  connect(actionSet->buildArchitectureAct, &QAction::triggered, architectureController,
          &ArchitectureController::buildActiveArchitecture);
  connect(actionSet->visualizeArchitectureAct, &QAction::triggered,
          architectureController, &ArchitectureController::visualizeActiveArchitecture);
  connect(architectureController, &ArchitectureController::visualizerTabSelected, this,
          [this] {
            actionSet->updateEditActions();
            actionSet->updateHistoryActions();
          });
  connect(actionSet->newBinaryFileAct, &QAction::triggered, documentController,
          &ProjectDocumentController::createBinaryFile);
  connect(actionSet->openAct, &QAction::triggered, fileController,
          &ProjectFileController::open);
  connect(actionSet->saveAct, &QAction::triggered, this,
          [this] { fileController->save(); });
  connect(actionSet->exportImageAct, &QAction::triggered, this,
          &SiliconWindow::exportImage);
  connect(actionSet->exitAct, &QAction::triggered, this, &QWidget::close);
  connect(actionSet->cutAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::cut);
  connect(actionSet->copyAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::copy);
  connect(actionSet->pasteAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::paste);
  connect(actionSet->rotateAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::rotate);
  connect(actionSet->autoPlaceAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::autoPlace);
  connect(actionSet->deleteAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::del);
  connect(actionSet->aboutAct, &QAction::triggered, this, &SiliconWindow::about);
  connect(actionSet->settingsAct, &QAction::triggered, actionSet,
          &WindowActions::openSettings);
  connect(actionSet->undoAct, &QAction::triggered, workspace,
          &EditorWorkspace::undoActiveDocument);
  connect(actionSet->redoAct, &QAction::triggered, workspace,
          &EditorWorkspace::redoActiveDocument);
  connect(undoStack, &QUndoStack::canUndoChanged, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  connect(undoStack, &QUndoStack::canRedoChanged, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  connect(workspace->codeEditor(), &QPlainTextEdit::undoAvailable, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  connect(workspace->codeEditor(), &QPlainTextEdit::redoAvailable, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  for (const auto& [type, editor] : workspace->architectureWorkspace()->editors()) {
    connect(editor, &QPlainTextEdit::undoAvailable, this,
            [this](bool) { actionSet->updateHistoryActions(); });
    connect(editor, &QPlainTextEdit::redoAvailable, this,
            [this](bool) { actionSet->updateHistoryActions(); });
  }
  connect(workspace->binaryEditor()->history(), &QUndoStack::canUndoChanged, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  connect(workspace->binaryEditor()->history(), &QUndoStack::canRedoChanged, this,
          [this](bool) { actionSet->updateHistoryActions(); });
  connect(workspace, &QStackedWidget::currentChanged, this,
          [this](int) { actionSet->updateHistoryActions(); });
  actionSet->updateHistoryActions();

  connect(actionSet->setNormalModeAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::setNormalMode);
  connect(actionSet->setPanModeAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::setPanMode);
  connect(actionSet->setWireCreationModeAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::setWireCreationMode);
  connect(actionSet->setSimulationModeAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::setSimulationMode);
  connect(actionSet->openComponentCatalogAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::showComponentCatalog);
  connect(actionSet->editSubcircuitShapeAct, &QAction::triggered, this,
          &SiliconWindow::editActiveSubcircuitShape);
  connect(actionSet->codeConversionAct, &QAction::triggered, documentController,
          &ProjectDocumentController::convertActiveDocument);
  connect(actionSet->setComponentPlacingModeAct, &QAction::triggered,
          interactionController, &DiagramInteractionController::setComponentPlacingMode);
  connect(actionSet->cancelInteractionAct, &QAction::triggered, interactionController,
          &DiagramInteractionController::cancelCurrentInteraction);
  connect(actionSet->toggleWaveformViewerAct, &QAction::toggled, waveformController,
          &WaveformController::toggle);

  addAction(actionSet->setComponentPlacingModeAct);
  addAction(actionSet->cancelInteractionAct);
}

void SiliconWindow::editActiveSubcircuitShape()
{
  if (actionSet->activeDocumentType() != SILICON::project::DocumentType::Circuit)
    return;

  const auto slug =
      SILICON::project::documentSlugForPath(projectSession.activeDocumentPath);
  if (!slug)
    return;

  try {
    workspace->flushActiveDocument();
    editGraphicalSubcircuitShape(*slug, projectSession.projectContext, undoStack, this);
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::warning(
        this, tr("Edit shape"),
        tr("Failed to save the active circuit before editing its shape:\n%1")
            .arg(e.what()));
  }
}

void SiliconWindow::about() const
{
  aboutDialog->show();
}

}  // namespace SILICON::ui
