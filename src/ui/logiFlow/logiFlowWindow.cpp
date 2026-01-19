/*
 Copyright (c) 2025. Giulio Cocconi

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
#include "ui/common/diagramScene.hpp"

LogiFlowWindow::LogiFlowWindow()
{
  const auto centralWidget = new QWidget();
  setCentralWidget(centralWidget);

  const auto layout = new QHBoxLayout();
  layout->setContentsMargins(5, 5, 5, 5);
  centralWidget->setLayout(layout);

  componentsDock = new QDockWidget(this);
  propertyDock   = new QDockWidget(this);

  addDockWidget(Qt::LeftDockWidgetArea, componentsDock);
  addDockWidget(Qt::LeftDockWidgetArea, propertyDock);

  propertyDock->setFeatures(QDockWidget::DockWidgetMovable);
  componentsDock->setFeatures(QDockWidget::DockWidgetMovable);

  propertyDock->setWindowTitle("Properties");
  componentsDock->setWindowTitle("Components");

  splitDockWidget(componentsDock, propertyDock, Qt::Vertical);

  diagramScene = new DiagramScene(this);
  diagramView  = new DiagramView(this);
  diagramView->setScene(diagramScene);

  connect(diagramScene, &DiagramScene::modeChanged, this, &LogiFlowWindow::updateStatus);
  updateStatus();

  connect(diagramScene, &DiagramScene::selectionChanged, this,
          &LogiFlowWindow::selectionChanged);

  layout->addWidget(diagramView);

  aboutDialog = new AboutDialog("SILICON", this);

  undoStack = new QUndoStack(this);

  createActions();
  createMenus();
  createToolBar();

  setWindowTitle(tr("SILICON LogiFlow"));
  setMinimumSize(160, 160);
}

void LogiFlowWindow::createActions()
{
  newAct         = new QAction(Icon("file"), tr("&New"), this);
  openAct        = new QAction(Icon("open"), tr("&Open..."), this);
  saveAct        = new QAction(Icon("save"), tr("&Save"), this);
  exportImageAct = new QAction(Icon("export"), tr("&Export..."), this);
  exitAct        = new QAction(Icon("xmark"), tr("E&xit"), this);
  cutAct         = new QAction(Icon("cut"), tr("Cu&t"), this);
  copyAct        = new QAction(Icon("copy"), tr("&Copy"), this);
  pasteAct       = new QAction(Icon("paste"), tr("&Paste"), this);
  rotateAct      = new QAction(Icon("rotate"), tr("&Rotate"), this);
  deleteAct      = new QAction(Icon("delete"), tr("&Delete"), this);
  aboutAct       = new QAction(Icon("info"), tr("&About"), this);

  undoAct = undoStack->createUndoAction(this, tr("&Undo"));
  undoAct->setIcon(Icon("undo"));

  redoAct = undoStack->createRedoAction(this, tr("&Redo"));
  redoAct->setIcon(Icon("redo"));

  // The rotate, cut, copy and delete actions should be disabled when no component is
  // selected
  rotateAct->setEnabled(false);
  cutAct->setEnabled(false);
  copyAct->setEnabled(false);
  deleteAct->setEnabled(false);

  setNormalModeAct       = new QAction(Icon("mouse-pointer"), "", this);
  setPanModeAct          = new QAction(Icon("pan"), "", this);
  setWireCreationModeAct = new QAction(Icon("link"), "", this);
  setSimulationModeAct   = new QAction(Icon("play"), "", this);

  setComponentPlacingModeAct = new QAction(Icon("plus"), "", this);

  newAct->setShortcuts(QKeySequence::New);
  openAct->setShortcuts(QKeySequence::Open);
  saveAct->setShortcuts(QKeySequence::Save);
  exportImageAct->setShortcuts(QKeySequence::Print);
  exitAct->setShortcuts(QKeySequence::Quit);
  undoAct->setShortcuts(QKeySequence::Undo);
  redoAct->setShortcuts(QKeySequence::Redo);
  cutAct->setShortcuts(QKeySequence::Cut);
  copyAct->setShortcuts(QKeySequence::Copy);
  rotateAct->setShortcut(Qt::AltModifier | Qt::Key_R);
  deleteAct->setShortcuts(QKeySequence::Delete);
  pasteAct->setShortcuts(QKeySequence::Paste);

  setWireCreationModeAct->setShortcut(Qt::AltModifier | Qt::Key_W);
  setSimulationModeAct->setShortcut(Qt::AltModifier | Qt::ControlModifier | Qt::Key_S);
  setComponentPlacingModeAct->setShortcut(Qt::AltModifier | Qt::Key_A);

  newAct->setStatusTip(tr("Create a new file"));
  openAct->setStatusTip(tr("Open an existing logiFlow file"));
  saveAct->setStatusTip(tr("Save the circuit to disk"));
  exportImageAct->setStatusTip(tr("Export the circuit as an image"));
  exitAct->setStatusTip(tr("Exit the application"));
  undoAct->setStatusTip(tr("Undo the last operation"));
  redoAct->setStatusTip(tr("Redo the last operation"));
  cutAct->setStatusTip(tr("Cut the current selection's contents to the clipboard"));
  pasteAct->setStatusTip(tr("Paste the clipboard's contents into the current selection"));
  deleteAct->setStatusTip(tr("Delete selected components"));
  aboutAct->setStatusTip(tr("Show the application's about box"));

  connect(newAct, &QAction::triggered, this, &LogiFlowWindow::newFile);
  connect(openAct, &QAction::triggered, this, &LogiFlowWindow::open);
  connect(saveAct, &QAction::triggered, this, &LogiFlowWindow::save);
  connect(exportImageAct, &QAction::triggered, this, &LogiFlowWindow::exportImage);
  connect(exitAct, &QAction::triggered, this, &QWidget::close);
  connect(cutAct, &QAction::triggered, this, &LogiFlowWindow::cut);
  connect(copyAct, &QAction::triggered, this, &LogiFlowWindow::copy);
  connect(pasteAct, &QAction::triggered, this, &LogiFlowWindow::paste);
  connect(rotateAct, &QAction::triggered, this, &LogiFlowWindow::rotate);
  connect(deleteAct, &QAction::triggered, this, &LogiFlowWindow::del);
  connect(aboutAct, &QAction::triggered, this, &LogiFlowWindow::about);

  connect(setNormalModeAct, &QAction::triggered, this, &LogiFlowWindow::setNormalMode);
  connect(setPanModeAct, &QAction::triggered, this, &LogiFlowWindow::setPanMode);
  connect(setWireCreationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setWireCreationMode);
  connect(setSimulationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setSimulationMode);
  connect(setComponentPlacingModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setComponentPlacingMode);
}

void LogiFlowWindow::createMenus()
{
  fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(newAct);
  fileMenu->addAction(openAct);
  fileMenu->addAction(saveAct);
  fileMenu->addAction(exportImageAct);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAct);

  editMenu = menuBar()->addMenu(tr("&Edit"));
  editMenu->addAction(undoAct);
  editMenu->addAction(redoAct);
  editMenu->addSeparator();
  editMenu->addAction(cutAct);
  editMenu->addAction(copyAct);
  editMenu->addAction(pasteAct);
  editMenu->addAction(rotateAct);
  editMenu->addAction(deleteAct);
  editMenu->addSeparator();

  helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(aboutAct);
}

void LogiFlowWindow::createToolBar()
{
  toolBar = new QToolBar(this);
  toolBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
  toolBar->setFloatable(false);

  toolBar->addAction(newAct);
  toolBar->addAction(openAct);
  toolBar->addAction(saveAct);

  toolBar->addSeparator();

  toolBar->addAction(setNormalModeAct);
  toolBar->addAction(setPanModeAct);
  toolBar->addAction(setWireCreationModeAct);
  toolBar->addAction(setSimulationModeAct);

  toolBar->addSeparator();
  toolBar->addAction(setComponentPlacingModeAct);

  addToolBar(toolBar);
}

#ifndef QT_NO_CONTEXTMENU
void LogiFlowWindow::contextMenuEvent(QContextMenuEvent* event)
{
  QMenu menu(this);
  menu.addAction(cutAct);
  menu.addAction(copyAct);
  menu.addAction(pasteAct);
  menu.addAction(rotateAct);
  menu.addAction(deleteAct);
  menu.exec(event->globalPos());
}
#endif  // QT_NO_CONTEXTMENU

/* ACTIONS IMPLEMENTATION */

void LogiFlowWindow::rotate()
{
  auto selectedComponents =
      std::ranges::views::filter(diagramScene->selectedItems(),
                                 [](auto el) { return el->type() >= COMPONENT; })
      | std::ranges::to<std::vector>();

  switch (diagramScene->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: {
      if (selectedComponents.size() != 1)
        return;

      auto component = qgraphicsitem_cast<GraphicalComponent*>(selectedComponents[0]);
      component->rotate();
      break;
    }
    case InteractionMode::COMPONENT_PLACING_MODE: {
      diagramScene->getComponentToBeDrawn()->rotate();
      break;
    }

    default: return;
  }
}

void LogiFlowWindow::del()
{
  for (auto selectedComponent : diagramScene->selectedItems()) {
    // Trying to remove non user-defined components leads to crash
    if (selectedComponent->type() > UNKNOWN) {
      diagramScene->removeItem(selectedComponent);
      delete selectedComponent;
    }
  }
}

void LogiFlowWindow::about() const
{
  aboutDialog->show();
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
  diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
}

void LogiFlowWindow::setComponentPlacingMode()
{
  diagramScene->setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
}

void LogiFlowWindow::updateStatus() const
{
  QString modeMsg = "Interaction Mode: ";

  switch (diagramScene->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: modeMsg += "NORMAL"; break;
    case InteractionMode::COMPONENT_PLACING_MODE: modeMsg += "COMPONENT PLACING"; break;
    case InteractionMode::WIRE_CREATION_MODE: modeMsg += "WIRE CREATION"; break;
    case InteractionMode::PAN_MODE: modeMsg += "PAN"; break;
    case InteractionMode::SIMULATION_MODE: modeMsg += "SIMULATION"; break;
    default: assert(false);
  }

  statusBar()->showMessage(modeMsg);
}
void LogiFlowWindow::selectionChanged() const
{
  auto interactionMode = diagramScene->getInteractionMode();
  // Enable rotation only when a single component is selected or when in component placing
  // mode

  rotateAct->setEnabled((interactionMode == InteractionMode::NORMAL_MODE
                         && diagramScene->selectedItems().size() == 1)
                        || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  // Enable cut, copy, paste and delete only when in normal mode and some items are
  // selected
  const bool cutCopyDelete = interactionMode == InteractionMode::NORMAL_MODE
                             && !diagramScene->selectedItems().empty();

  cutAct->setEnabled(cutCopyDelete);
  copyAct->setEnabled(cutCopyDelete);
  deleteAct->setEnabled(cutCopyDelete);
}
