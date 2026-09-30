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

#include "windowActions.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QTextDocument>
#include <QToolBar>
#include <QUndoStack>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/editor/diagramInteractionController.hpp>
#include <ui/documents/binary/binaryEditor.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/serialization/document_conversion.hpp>
#include <ui/shell/icons.hpp>
#include <ui/shell/uiUtils.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  const SILICON::logging::Logger uiLog("ui");

}  // namespace

WindowActions::WindowActions(QMainWindow& window, ProjectSession& session,
                             EditorWorkspace& workspace, QUndoStack& history)
  : QObject(&window),
    window_(window),
    session_(session),
    workspace_(workspace),
    undoStack_(history)
{
}

void WindowActions::setActionsEnabled(std::initializer_list<QAction*> actions,
                                      const bool                      enabled)
{
  for (QAction* action : actions) {
    if (action)
      action->setEnabled(enabled);
  }
}

void WindowActions::updateHistoryActions()
{
  if (!undoAct || !redoAct)
    return;
  if (workspace_.isVisualizerActive()) {
    undoAct->setEnabled(false);
    redoAct->setEnabled(false);
    return;
  }
  const auto  type   = activeDocumentType();
  const auto* editor = type && SILICON::project::isCodeDocument(*type)
                           ? workspace_.activeCodeEditor()
                           : nullptr;
  const bool  binary = type
                      && SILICON::project::categoryOf(*type)
                             == SILICON::project::DocumentCategory::Binary;
  undoAct->setEnabled(undoStack_.canUndo()
                      || (editor && editor->document()->isUndoAvailable())
                      || (binary && workspace_.binaryEditor()->history()->canUndo()));
  redoAct->setEnabled(undoStack_.canRedo()
                      || (editor && editor->document()->isRedoAvailable())
                      || (binary && workspace_.binaryEditor()->history()->canRedo()));
}

void WindowActions::createMenus()
{
  fileMenu = window_.menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(newAct);
  auto* newMenu = fileMenu->addMenu(Icon("file"), tr("New &Document"));
  newMenu->addAction(newCircuitAct);
  newMenu->addAction(newCodeFileAct);
  newMenu->addAction(newArchitectureAct);
  newMenu->addAction(newBinaryFileAct);
  fileMenu->addAction(openAct);
  fileMenu->addAction(saveAct);
  fileMenu->addAction(exportImageAct);
  fileMenu->addAction(toggleWaveformViewerAct);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAct);

  editMenu = window_.menuBar()->addMenu(tr("&Edit"));
  editMenu->addAction(undoAct);
  editMenu->addAction(redoAct);
  editMenu->addSeparator();
  editMenu->addAction(cutAct);
  editMenu->addAction(copyAct);
  editMenu->addAction(pasteAct);
  editMenu->addAction(rotateAct);
  editMenu->addAction(autoPlaceAct);
  editMenu->addAction(deleteAct);
  editMenu->addSeparator();
  editMenu->addAction(settingsAct);

  helpMenu = window_.menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(aboutAct);
}

void WindowActions::createToolBar()
{
  toolBar = new QToolBar(&window_);
  toolBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
  toolBar->setFloatable(false);

  toolBar->addAction(newAct);
  toolBar->addAction(openAct);
  toolBar->addAction(saveAct);

  diagramToolsSeparator = toolBar->addSeparator();

  toolBar->addAction(setNormalModeAct);
  toolBar->addAction(setPanModeAct);
  toolBar->addAction(setWireCreationModeAct);
  toolBar->addAction(setSimulationModeAct);
  toolBar->addAction(toggleWaveformViewerAct);

  documentToolsSeparator = toolBar->addSeparator();
  toolBar->addAction(openComponentCatalogAct);
  toolBar->addAction(editSubcircuitShapeAct);
  toolBar->addAction(codeConversionAct);
  toolBar->addAction(buildArchitectureAct);
  toolBar->addAction(visualizeArchitectureAct);

  window_.addToolBar(toolBar);
}

void WindowActions::updateSubcircuitShapeAction()
{
  if (!editSubcircuitShapeAct)
    return;
  const bool active = activeDocumentType() == SILICON::project::DocumentType::Circuit;
  editSubcircuitShapeAct->setVisible(active);
  editSubcircuitShapeAct->setEnabled(active);
  updateCodeAction();
}

std::optional<SILICON::project::DocumentType>
WindowActions::activeDocumentType() const noexcept
{
  return SILICON::project::documentTypeForPath(session_.activeDocumentPath);
}

void WindowActions::updateCodeAction()
{
  if (!codeConversionAct)
    return;
  const auto* document =
      session_.projectContext.documents().find(session_.activeDocumentPath);
  const auto converters = document ? documentConvertersFor(document->getType())
                                   : std::vector<const DocumentConverter*>{};
  const auto available =
      std::ranges::count_if(converters, [](const DocumentConverter* converter) {
        return converter->available;
      });
  codeConversionAct->setVisible(!converters.empty());
  codeConversionAct->setEnabled(available != 0);
  codeConversionAct->setToolTip(
      !converters.empty() && available == 0
          ? QString::fromUtf8(
                converters.front()->unavailableReason.data(),
                static_cast<qsizetype>(converters.front()->unavailableReason.size()))
          : QString());
  if (converters.size() == 1) {
    codeConversionAct->setText(
        tr("Convert to %1").arg(documentTypeName(converters.front()->target)));
  } else if (!converters.empty()) {
    codeConversionAct->setText(tr("Convert..."));
  }
  const auto type         = activeDocumentType();
  const bool nonGraphical = type
                            && SILICON::project::categoryOf(*type)
                                   != SILICON::project::DocumentCategory::Diagram;
  setActionsEnabled({setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                     setSimulationModeAct, toggleWaveformViewerAct,
                     openComponentCatalogAct, setComponentPlacingModeAct, autoPlaceAct},
                    !nonGraphical);

  updateDocumentActionVisibility();
}

void WindowActions::updateDocumentActionVisibility()
{
  if (!toolBar)
    return;

  const auto type    = activeDocumentType();
  const bool diagram = type
                       && SILICON::project::categoryOf(*type)
                              == SILICON::project::DocumentCategory::Diagram;

  const bool catalogVisible = diagram && openComponentCatalogAct->isEnabled();
  const bool shapeVisible =
      editSubcircuitShapeAct->isVisible() && editSubcircuitShapeAct->isEnabled();
  const bool conversionVisible =
      codeConversionAct->isVisible() && codeConversionAct->isEnabled();

  // QWidget visibility is not authoritative for toolbar actions: Qt may recreate or
  // show the widget again after the shared QAction changes state. Remove unavailable
  // actions from this toolbar and re-add the active group in its canonical order.
  for (auto* action :
       {diagramToolsSeparator, setNormalModeAct, setPanModeAct, setWireCreationModeAct,
        setSimulationModeAct, toggleWaveformViewerAct, documentToolsSeparator,
        openComponentCatalogAct, editSubcircuitShapeAct, codeConversionAct,
        buildArchitectureAct, visualizeArchitectureAct})
    toolBar->removeAction(action);

  if (diagram) {
    toolBar->addAction(diagramToolsSeparator);
    for (auto* action : {setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                         setSimulationModeAct, toggleWaveformViewerAct}) {
      if (action->isEnabled())
        toolBar->addAction(action);
    }
  }

  const bool buildVisible = type == SILICON::project::DocumentType::Sisl;
  if (catalogVisible || shapeVisible || conversionVisible || buildVisible) {
    toolBar->addAction(documentToolsSeparator);
    if (catalogVisible)
      toolBar->addAction(openComponentCatalogAct);
    if (shapeVisible)
      toolBar->addAction(editSubcircuitShapeAct);
    if (conversionVisible)
      toolBar->addAction(codeConversionAct);
    if (buildVisible) {
      toolBar->addAction(buildArchitectureAct);
      toolBar->addAction(visualizeArchitectureAct);
    }
  }
}

void WindowActions::createActions()
{
  newAct =
      makeAction(&window_, Icon("file"), tr("&New Project"), tr("Create a new project"));
  newCircuitAct =
      makeAction(&window_, categoryIcon(SILICON::project::DocumentType::Circuit),
                 tr("Circuit"), tr("Create a new circuit document"));
  newCodeFileAct =
      makeAction(&window_, categoryIcon(SILICON::project::DocumentType::Verilog),
                 tr("Code File..."), tr("Create an empty source-code document"));
  newArchitectureAct = makeAction(&window_, Icon("cpu"), tr("ISA Architecture..."),
                                  tr("Create a SISL instruction format"));
  newBinaryFileAct =
      makeAction(&window_, categoryIcon(SILICON::project::DocumentType::RawBinary),
                 tr("Binary File..."), tr("Create a fixed-size raw binary document"));
  openAct = makeAction(&window_, Icon("open"), tr("&Open..."),
                       tr("Open an existing silicon file"));
  saveAct =
      makeAction(&window_, Icon("save"), tr("&Save"), tr("Save the project to disk"));
  exportImageAct = makeAction(&window_, Icon("export"), tr("&Export..."),
                              tr("Export the circuit as an image"));
  exitAct  = makeAction(&window_, Icon("xmark"), tr("E&xit"), tr("Exit the application"));
  cutAct   = makeAction(&window_, Icon("cut"), tr("Cu&t"),
                        tr("Cut the current selection's contents to the clipboard"));
  copyAct  = makeAction(&window_, Icon("copy"), tr("&Copy"));
  pasteAct = makeAction(&window_, Icon("paste"), tr("&Paste"),
                        tr("Paste the clipboard's contents into the current selection"));
  rotateAct    = makeAction(&window_, Icon("rotate"), tr("&Rotate"));
  autoPlaceAct = makeAction(&window_, Icon("rearrange"), tr("&Auto place"),
                            tr("Automatically place components and reroute wires"));
  deleteAct    = makeAction(&window_, Icon("delete"), tr("&Delete"),
                            tr("Delete selected components"));
  aboutAct     = makeAction(&window_, Icon("info"), tr("&About"),
                            tr("Show the application's about box"));
  settingsAct  = makeAction(&window_, Icon("settings"), tr("&Settings..."),
                            tr("Edit application settings"));

  undoAct =
      makeAction(&window_, Icon("undo"), tr("&Undo"), tr("Undo the last operation"));
  undoAct->setIcon(Icon("undo"));
  undoAct->setStatusTip(tr("Undo the last operation"));

  redoAct =
      makeAction(&window_, Icon("redo"), tr("&Redo"), tr("Redo the last operation"));
  redoAct->setIcon(Icon("redo"));
  redoAct->setStatusTip(tr("Redo the last operation"));

  setActionsEnabled({rotateAct, cutAct, copyAct, deleteAct}, false);

  setNormalModeAct       = new QAction(Icon("mouse-pointer"), "", &window_);
  setPanModeAct          = new QAction(Icon("pan"), "", &window_);
  setWireCreationModeAct = new QAction(Icon("link"), "", &window_);
  setSimulationModeAct   = new QAction(Icon("play"), "", &window_);
  toggleWaveformViewerAct =
      makeAction(&window_, Icon("chart"), tr("Trace"), tr("Show waveform viewer"));
  toggleWaveformViewerAct->setCheckable(true);
  cancelInteractionAct =
      makeAction(&window_, QString(), tr("Cancel the current interaction"));

  openComponentCatalogAct =
      makeAction(&window_, Icon("plus"), "", tr("Open the component catalog"));
  editSubcircuitShapeAct = makeAction(&window_, Icon("circuit-board"), tr("Edit Shape"),
                                      tr("Edit the active circuit shape"));
  editSubcircuitShapeAct->setVisible(false);
  editSubcircuitShapeAct->setEnabled(false);
  codeConversionAct = makeAction(&window_, Icon("code"), tr("Code"));
  codeConversionAct->setVisible(false);
  codeConversionAct->setEnabled(false);
  buildArchitectureAct     = makeAction(&window_, Icon("build"), tr("Build"),
                                        tr("Compile the current SISL instruction format"));
  visualizeArchitectureAct = makeAction(&window_, Icon("diagram"), tr("Visualize"),
                                        tr("Visualize SISL instruction formats"));
  setComponentPlacingModeAct =
      makeAction(&window_, Icon("plus"), "", tr("Open quick component search"));
}

void WindowActions::updateEditActions()
{
  if (!rotateAct || !cutAct || !copyAct || !pasteAct || !deleteAct)
    return;
  if (workspace_.isVisualizerActive()) {
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

  const auto interactionMode = workspace_.scene()->getInteractionMode();
  const auto selected        = workspace_.scene()->selectedItems();
  const bool hasSelection    = !selected.empty();

  rotateAct->setEnabled(
      (interactionMode == InteractionMode::NORMAL_MODE && selected.size() == 1)
      || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  const bool canEditSelection =
      interactionMode == InteractionMode::NORMAL_MODE && hasSelection;
  setActionsEnabled({cutAct, copyAct, deleteAct}, canEditSelection);
  const auto* clipboardData = QApplication::clipboard()->mimeData();
  pasteAct->setEnabled(interactionMode == InteractionMode::NORMAL_MODE && clipboardData
                       && clipboardData->hasFormat(CircuitSelectionMimeType));
}

}  // namespace SILICON::ui
