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
#include <ranges>
#include <vector>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>

#include <ui/documents/documentEditor.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/serialization/document_conversion.hpp>
#include <ui/shell/icons.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

WindowActions::WindowActions(QMainWindow& window, ProjectSession& session,
                             EditorWorkspace& workspace)
  : QObject(&window), window(window), session(session), workspace(workspace)
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
  if (!edit.undo || !edit.redo)
    return;
  edit.undo->setEnabled(workspace.canUndoActiveDocument());
  edit.redo->setEnabled(workspace.canRedoActiveDocument());
}

void WindowActions::refreshActiveEditorActions()
{
  updateEditActions();
  updateHistoryActions();
}

void WindowActions::bindEditorActions()
{
  connect(edit.undo, &QAction::triggered, &workspace,
          &EditorWorkspace::undoActiveDocument);
  connect(edit.redo, &QAction::triggered, &workspace,
          &EditorWorkspace::redoActiveDocument);
  connect(edit.cut, &QAction::triggered, &workspace, &EditorWorkspace::cutActiveDocument);
  connect(edit.copy, &QAction::triggered, &workspace,
          &EditorWorkspace::copyActiveDocument);
  connect(edit.paste, &QAction::triggered, &workspace,
          &EditorWorkspace::pasteActiveDocument);
  connect(edit.remove, &QAction::triggered, &workspace,
          &EditorWorkspace::deleteInActiveDocument);

  connect(&workspace, &EditorWorkspace::historyStateChanged, this,
          &WindowActions::updateHistoryActions);
  connect(&workspace, &EditorWorkspace::editStateChanged, this,
          &WindowActions::updateEditActions);
  connect(&workspace, &EditorWorkspace::activeEditorChanged, this,
          &WindowActions::refreshActiveEditorActions);

  refreshActiveEditorActions();
}

void WindowActions::createMenus()
{
  fileMenu = window.menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(project.newProject);
  auto* newMenu = fileMenu->addMenu(Icon("file"), tr("New &Document"));
  newMenu->addAction(documents.newCircuit);
  newMenu->addAction(documents.newCodeFile);
  newMenu->addAction(documents.newArchitecture);
  newMenu->addAction(documents.newBinaryFile);
  fileMenu->addAction(project.open);
  fileMenu->addAction(project.save);
  fileMenu->addAction(project.exportImage);
  fileMenu->addAction(waveform.toggleTrace);
  fileMenu->addSeparator();
  fileMenu->addAction(project.exit);

  editMenu = window.menuBar()->addMenu(tr("&Edit"));
  editMenu->addAction(edit.undo);
  editMenu->addAction(edit.redo);
  editMenu->addSeparator();
  editMenu->addAction(edit.cut);
  editMenu->addAction(edit.copy);
  editMenu->addAction(edit.paste);
  editMenu->addAction(circuit.rotate);
  editMenu->addAction(circuit.autoPlace);
  editMenu->addAction(edit.remove);
  editMenu->addSeparator();
  editMenu->addAction(project.settings);

  helpMenu = window.menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(project.about);
}

void WindowActions::createToolBar()
{
  toolBar = new QToolBar(&window);
  toolBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
  toolBar->setFloatable(false);

  toolBar->addAction(project.newProject);
  toolBar->addAction(project.open);
  toolBar->addAction(project.save);

  diagramToolsSeparator = toolBar->addSeparator();

  toolBar->addAction(circuit.setNormalMode);
  toolBar->addAction(circuit.setPanMode);
  toolBar->addAction(circuit.setWireCreationMode);
  toolBar->addAction(circuit.setSimulationMode);
  toolBar->addAction(waveform.toggleTrace);

  documentToolsSeparator = toolBar->addSeparator();
  toolBar->addAction(circuit.openComponentCatalog);
  toolBar->addAction(circuit.editSubcircuitShape);
  toolBar->addAction(documents.codeConversion);
  toolBar->addAction(architecture.build);
  toolBar->addAction(architecture.visualize);

  window.addToolBar(toolBar);
}

void WindowActions::updateSubcircuitShapeAction()
{
  if (!circuit.editSubcircuitShape)
    return;
  const bool active = activeDocumentType() == SILICON::project::DocumentType::Circuit;
  circuit.editSubcircuitShape->setVisible(active);
  circuit.editSubcircuitShape->setEnabled(active);
  updateCodeAction();
}

std::optional<SILICON::project::DocumentType>
WindowActions::activeDocumentType() const noexcept
{
  return SILICON::project::documentTypeForPath(session.activeDocumentPath);
}

void WindowActions::updateCodeAction()
{
  if (!documents.codeConversion)
    return;
  const auto* document =
      session.projectContext.documents().find(session.activeDocumentPath);
  const auto converters = document ? documentConvertersFor(document->getType())
                                   : std::vector<const DocumentConverter*>{};
  const auto available =
      std::ranges::count_if(converters, [](const DocumentConverter* converter) {
        return converter->available;
      });
  documents.codeConversion->setVisible(!converters.empty());
  documents.codeConversion->setEnabled(available != 0);
  documents.codeConversion->setToolTip(
      !converters.empty() && available == 0
          ? QString::fromUtf8(
                converters.front()->unavailableReason.data(),
                static_cast<qsizetype>(converters.front()->unavailableReason.size()))
          : QString());
  if (converters.size() == 1) {
    documents.codeConversion->setText(
        tr("Convert to %1").arg(documentTypeName(converters.front()->target)));
  } else if (!converters.empty()) {
    documents.codeConversion->setText(tr("Convert..."));
  }
  const auto type         = activeDocumentType();
  const bool nonGraphical = type
                            && SILICON::project::categoryOf(*type)
                                   != SILICON::project::DocumentCategory::Diagram;
  setActionsEnabled({circuit.setNormalMode, circuit.setPanMode,
                     circuit.setWireCreationMode, circuit.setSimulationMode,
                     waveform.toggleTrace, circuit.openComponentCatalog,
                     circuit.setComponentPlacingMode, circuit.autoPlace},
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

  const bool catalogVisible = diagram && circuit.openComponentCatalog->isEnabled();
  const bool shapeVisible   = circuit.editSubcircuitShape->isVisible()
                            && circuit.editSubcircuitShape->isEnabled();
  const bool conversionVisible =
      documents.codeConversion->isVisible() && documents.codeConversion->isEnabled();

  // QWidget visibility is not authoritative for toolbar actions: Qt may recreate or
  // show the widget again after the shared QAction changes state. Remove unavailable
  // actions from this toolbar and re-add the active group in its canonical order.
  for (auto* action :
       {diagramToolsSeparator, circuit.setNormalMode, circuit.setPanMode,
        circuit.setWireCreationMode, circuit.setSimulationMode, waveform.toggleTrace,
        documentToolsSeparator, circuit.openComponentCatalog, circuit.editSubcircuitShape,
        documents.codeConversion, architecture.build, architecture.visualize})
    toolBar->removeAction(action);

  if (diagram) {
    toolBar->addAction(diagramToolsSeparator);
    for (auto* action :
         {circuit.setNormalMode, circuit.setPanMode, circuit.setWireCreationMode,
          circuit.setSimulationMode, waveform.toggleTrace}) {
      if (action->isEnabled())
        toolBar->addAction(action);
    }
  }

  const bool buildVisible = type == SILICON::project::DocumentType::Sisl;
  if (catalogVisible || shapeVisible || conversionVisible || buildVisible) {
    toolBar->addAction(documentToolsSeparator);
    if (catalogVisible)
      toolBar->addAction(circuit.openComponentCatalog);
    if (shapeVisible)
      toolBar->addAction(circuit.editSubcircuitShape);
    if (conversionVisible)
      toolBar->addAction(documents.codeConversion);
    if (buildVisible) {
      toolBar->addAction(architecture.build);
      toolBar->addAction(architecture.visualize);
    }
  }
}

void WindowActions::createActions()
{
  project.newProject =
      makeAction(&window, Icon("file"), tr("&New Project"), tr("Create a new project"));
  documents.newCircuit =
      makeAction(&window, categoryIcon(SILICON::project::DocumentType::Circuit),
                 tr("Circuit"), tr("Create a new circuit document"));
  documents.newCodeFile =
      makeAction(&window, categoryIcon(SILICON::project::DocumentType::Verilog),
                 tr("Code File..."), tr("Create an empty source-code document"));
  documents.newArchitecture = makeAction(&window, Icon("cpu"), tr("ISA Architecture..."),
                                         tr("Create a SISL instruction format"));
  documents.newBinaryFile =
      makeAction(&window, categoryIcon(SILICON::project::DocumentType::RawBinary),
                 tr("Binary File..."), tr("Create a fixed-size raw binary document"));
  project.open = makeAction(&window, Icon("open"), tr("&Open..."),
                            tr("Open an existing silicon file"));
  project.save =
      makeAction(&window, Icon("save"), tr("&Save"), tr("Save the project to disk"));
  project.exportImage = makeAction(&window, Icon("export"), tr("&Export..."),
                                   tr("Export the circuit as an image"));
  project.exit =
      makeAction(&window, Icon("xmark"), tr("E&xit"), tr("Exit the application"));
  edit.cut  = makeAction(&window, Icon("cut"), tr("Cu&t"),
                         tr("Cut the current selection's contents to the clipboard"));
  edit.copy = makeAction(&window, Icon("copy"), tr("&Copy"));
  edit.paste =
      makeAction(&window, Icon("paste"), tr("&Paste"),
                 tr("Paste the clipboard's contents into the current selection"));
  circuit.rotate    = makeAction(&window, Icon("rotate"), tr("&Rotate"));
  circuit.autoPlace = makeAction(&window, Icon("rearrange"), tr("&Auto place"),
                                 tr("Automatically place components and reroute wires"));
  edit.remove       = makeAction(&window, Icon("delete"), tr("&Delete"),
                                 tr("Delete selected components"));
  project.about     = makeAction(&window, Icon("info"), tr("&About"),
                                 tr("Show the application's about box"));
  project.settings  = makeAction(&window, Icon("settings"), tr("&Settings..."),
                                 tr("Edit application settings"));

  edit.undo =
      makeAction(&window, Icon("undo"), tr("&Undo"), tr("Undo the last operation"));
  edit.redo =
      makeAction(&window, Icon("redo"), tr("&Redo"), tr("Redo the last operation"));

  setActionsEnabled({circuit.rotate, edit.cut, edit.copy, edit.remove}, false);

  circuit.setNormalMode       = new QAction(Icon("mouse-pointer"), "", &window);
  circuit.setPanMode          = new QAction(Icon("pan"), "", &window);
  circuit.setWireCreationMode = new QAction(Icon("link"), "", &window);
  circuit.setSimulationMode   = new QAction(Icon("play"), "", &window);
  waveform.toggleTrace =
      makeAction(&window, Icon("chart"), tr("Trace"), tr("Show waveform viewer"));
  waveform.toggleTrace->setCheckable(true);
  circuit.cancelInteraction =
      makeAction(&window, QString(), tr("Cancel the current interaction"));

  circuit.openComponentCatalog =
      makeAction(&window, Icon("plus"), "", tr("Open the component catalog"));
  circuit.editSubcircuitShape =
      makeAction(&window, Icon("circuit-board"), tr("Edit Shape"),
                 tr("Edit the active circuit shape"));
  circuit.editSubcircuitShape->setVisible(false);
  circuit.editSubcircuitShape->setEnabled(false);
  documents.codeConversion = makeAction(&window, Icon("code"), tr("Code"));
  documents.codeConversion->setVisible(false);
  documents.codeConversion->setEnabled(false);
  architecture.build     = makeAction(&window, Icon("build"), tr("Build"),
                                      tr("Compile the current SISL instruction format"));
  architecture.visualize = makeAction(&window, Icon("diagram"), tr("Visualize"),
                                      tr("Visualize SISL instruction formats"));
  circuit.setComponentPlacingMode =
      makeAction(&window, Icon("plus"), "", tr("Open quick component search"));

  bindEditorActions();
}

void WindowActions::updateEditActions()
{
  if (!circuit.rotate || !edit.cut || !edit.copy || !edit.paste || !edit.remove)
    return;

  const auto state = workspace.activeEditState();
  circuit.rotate->setEnabled(state.canRotate);
  edit.cut->setEnabled(state.canCut);
  edit.copy->setEnabled(state.canCopy);
  edit.paste->setEnabled(state.canPaste);
  edit.remove->setEnabled(state.canDelete);
}

}  // namespace SILICON::ui
