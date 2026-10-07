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

#include "siliconWindow.hpp"
#include <ui/circuit/editor/circuitEditor.hpp>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <cstring>
#include <ranges>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QResizeEvent>
#include <QToolBar>
#include <QUndoStack>
#include <QWidget>

#include <logging/logger.hpp>

#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/editor/componentCatalogOverlay.hpp>
#include <ui/circuit/editor/diagramInteractionController.hpp>
#include <ui/circuit/editor/propertyPanel.hpp>
#include <ui/documents/architecture/architectureController.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectFileController.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/project/projectTree.hpp>
#include <ui/shell/aboutDialog.hpp>
#include <ui/shell/icons.hpp>
#include <ui/shell/logging/graphicalLogStream.hpp>
#include <ui/shell/logging/logSideView.hpp>
#include <ui/shell/uiUtils.hpp>
#include <ui/shell/windowActions.hpp>
#include <ui/waveform/waveformController.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

const SILICON::logging::Logger uiLog("ui");

#ifdef __EMSCRIPTEN__
EM_BOOL SiliconWindow::wasmKeyDownCallback(int, const EmscriptenKeyboardEvent* keyEvent,
                                           void* userData)
{
  if (!userData || !keyEvent)
    return EM_FALSE;

  if (std::strcmp(keyEvent->key, "Escape") != 0
      && std::strcmp(keyEvent->code, "Escape") != 0)
    return EM_FALSE;

  auto* window = static_cast<SiliconWindow*>(userData);
  return window->handleWasmEscapeKey() ? EM_TRUE : EM_FALSE;
}

bool SiliconWindow::handleWasmEscapeKey()
{
  if (QApplication::activeModalWidget())
    return false;

  if (componentCatalogOverlay && componentCatalogOverlay->isVisible()) {
    componentCatalogOverlay->hide();
    return true;
  }

  if (circuitEditor->scene())
    circuitEditor->scene()->cancelCurrentInteraction();

  return true;
}
#endif

SiliconWindow::SiliconWindow()
{
  // --- Layout Setup --------------------------------------------------------------------
  const auto centralWidget = new QWidget();
  setCentralWidget(centralWidget);

  const auto layout = new QHBoxLayout();
  layout->setContentsMargins(5, 5, 5, 5);
  centralWidget->setLayout(layout);

  documentDock = new QDockWidget(this);
  propertyDock = new QDockWidget(this);
  logDock      = new QDockWidget(this);

  addDockWidget(Qt::LeftDockWidgetArea, documentDock);
  addDockWidget(Qt::LeftDockWidgetArea, propertyDock);
  addDockWidget(Qt::BottomDockWidgetArea, logDock);

  propertyDock->setFeatures(QDockWidget::DockWidgetMovable);
  documentDock->setFeatures(QDockWidget::DockWidgetMovable);
  logDock->setFeatures(QDockWidget::DockWidgetMovable);
  logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

  propertyDock->setWindowTitle("Properties");
  documentDock->setWindowTitle("Project");
  logDock->setWindowTitle("Logs");

  splitDockWidget(documentDock, propertyDock, Qt::Vertical);

  workspace     = new EditorWorkspace(projectSession, centralWidget);
  circuitEditor = &workspace->circuitEditor();

  connect(circuitEditor->scene(), &DiagramScene::modeChanged, this,
          &SiliconWindow::updateStatus);
  updateStatus();

  layout->addWidget(workspace);
  componentCatalogOverlay = new ComponentCatalogOverlay(
      circuitEditor->scene(), projectSession.projectContext.documents(),
      &projectSession.circuitResolver, circuitEditor->view()->viewport());
  circuitEditor->view()->viewport()->installEventFilter(this);
  watchedViewport = circuitEditor->view()->viewport();
  updateComponentCatalogGeometry();
  initializeProjectTree();
  undoStack = new QUndoStack(this);
  // The circuit scene, its document editor and the rest of the shell share one history:
  // the workspace hands it to every editor it hosts.
  workspace->setProjectHistory(undoStack);
  circuitEditor->scene()->setUndoStack(undoStack);
  interactionController = new DiagramInteractionController(
      projectSession, *circuitEditor, *componentCatalogOverlay, *undoStack, *this);
  documentController =
      new ProjectDocumentController(projectSession, *workspace, *projectTree,
                                    *componentCatalogOverlay, *undoStack, this);
  architectureController =
      new ArchitectureController(projectSession, *workspace, *documentController, this);
  circuitEditor->scene()->setDocumentNavigator(documentController);
  connect(documentController, &ProjectDocumentController::activeDocumentChanged, this,
          [this](const QString&, SILICON::project::DocumentCategory) {
            refreshActiveDocumentUi();
          });
  connect(documentController, &ProjectDocumentController::projectDocumentsChanged, this,
          &SiliconWindow::refreshProjectUi);

  aboutDialog = new AboutDialog("SILICON", this);

  fileController = new ProjectFileController(projectSession, *workspace,
                                             *documentController, *undoStack, this);
  connect(fileController, &ProjectFileController::projectChanged, this,
          &SiliconWindow::refreshProjectUi);
  propertyPanel =
      new PropertyPanel(propertyDock, circuitEditor->scene(), projectTree, undoStack,
                        projectSession.currentProjectInfo, projectSession.currentFileName,
                        [this] { documentController->rebuildTree(); });

  actionSet = new WindowActions(*this, projectSession, *workspace);
  actionSet->createActions();
  connect(circuitEditor->scene(), &DiagramScene::selectionChanged, this,
          &SiliconWindow::selectionChanged);
  waveformController = new WaveformController(projectSession, *circuitEditor->scene(),
                                              *documentController, this);
  wireActions();
  actionSet->applyStoredSettings();
  actionSet->createMenus();
  actionSet->createToolBar();

  logSideView = new LogSideView(logDock);
  connect(circuitEditor->scene(), &DiagramScene::logsClearRequested, logSideView,
          &LogSideView::clear);
  graphicalLogStream = new GraphicalLogStream(this);
  logDock->setWidget(logSideView);
  logDock->setMinimumHeight(logSideView->minimumSizeHint().height());
  logDock->resize(width(), logSideView->sizeHint().height());

#ifdef __EMSCRIPTEN__
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true,
                                  &SiliconWindow::wasmKeyDownCallback);
#endif

  connect(graphicalLogStream, &GraphicalLogStream::lineReceived, logSideView,
          &LogSideView::appendLine, Qt::QueuedConnection);
  graphicalLogStream->attachToBoostLog();

  resizeDocks({documentDock, propertyDock}, {320, 260}, Qt::Vertical);
  resizeDocks({logDock}, {180}, Qt::Vertical);

  setWindowTitle(tr("SILICON"));
  setMinimumSize(160, 160);

  fileController->resetProjectState();
  documentController->rebuildTree();
  refreshActiveDocumentUi();

  uiLog.info("Qt logging sideview initialized");
}

void SiliconWindow::wireActions()
{
  connect(actionSet->project.newProject, &QAction::triggered, fileController,
          &ProjectFileController::newFile);
  connect(actionSet->project.open, &QAction::triggered, fileController,
          &ProjectFileController::open);
  connect(actionSet->project.save, &QAction::triggered, this,
          [this] { fileController->save(); });
  connect(actionSet->project.exportImage, &QAction::triggered, this,
          &SiliconWindow::exportImage);
  connect(actionSet->project.exit, &QAction::triggered, this, &QWidget::close);
  connect(actionSet->project.settings, &QAction::triggered, actionSet,
          &WindowActions::openSettings);
  connect(actionSet->project.about, &QAction::triggered, this, &SiliconWindow::about);

  connect(actionSet->documents.newCircuit, &QAction::triggered, documentController,
          &ProjectDocumentController::createCircuit);
  connect(actionSet->documents.newCodeFile, &QAction::triggered, documentController,
          &ProjectDocumentController::createCodeFile);
  connect(actionSet->documents.newArchitecture, &QAction::triggered, documentController,
          &ProjectDocumentController::createArchitecture);
  connect(actionSet->documents.newBinaryFile, &QAction::triggered, documentController,
          &ProjectDocumentController::createBinaryFile);
  connect(actionSet->documents.codeConversion, &QAction::triggered, documentController,
          &ProjectDocumentController::convertActiveDocument);

  // Editor subsystems own the commands they implement, so they wire their own actions.
  interactionController->bindActions(actionSet->circuit);
  architectureController->bindActions(actionSet->architecture);
  waveformController->bindActions(actionSet->waveform);

  // These two shortcuts are only meaningful mid-interaction, so they are installed on the
  // window to stay reachable from anywhere rather than from a focused child widget.
  addAction(actionSet->circuit.setComponentPlacingMode);
  addAction(actionSet->circuit.cancelInteraction);
}

void SiliconWindow::about() const
{
  aboutDialog->show();
}

#ifndef QT_NO_CONTEXTMENU
void SiliconWindow::contextMenuEvent(QContextMenuEvent* event)
{
  #ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addAction(actionSet->edit.cut);
  menu->addAction(actionSet->edit.copy);
  menu->addAction(actionSet->edit.paste);
  menu->addAction(actionSet->circuit.rotate);
  menu->addAction(actionSet->edit.remove);
  menu->popup(event->globalPos());
  #else
  QMenu menu(this);
  menu.addAction(actionSet->edit.cut);
  menu.addAction(actionSet->edit.copy);
  menu.addAction(actionSet->edit.paste);
  menu.addAction(actionSet->circuit.rotate);
  menu.addAction(actionSet->edit.remove);
  menu.exec(event->globalPos());
  #endif
  event->accept();
}
#endif  // QT_NO_CONTEXTMENU

bool SiliconWindow::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == watchedViewport && event->type() == QEvent::Resize) {
    updateComponentCatalogGeometry();
  }

  return QMainWindow::eventFilter(watched, event);
}

void SiliconWindow::resizeEvent(QResizeEvent* event)
{
  QMainWindow::resizeEvent(event);
  updateComponentCatalogGeometry();

  const int currentWidth  = event->size().width();
  const int currentHeight = event->size().height();

  const int minWidth = std::min(288, currentWidth / 4);
  const int maxWidth = currentWidth / 2;

  const int minHeight = currentHeight / 3;

  auto configureSizeConstraints = [minWidth, maxWidth, minHeight](QDockWidget* widget) {
    widget->setMinimumWidth(minWidth);
    widget->setMaximumWidth(maxWidth);
    widget->setMinimumHeight(minHeight);
  };

  configureSizeConstraints(documentDock);
  configureSizeConstraints(propertyDock);

  logDock->setMinimumWidth(160);
  logDock->setMaximumWidth(QWIDGETSIZE_MAX);
  logDock->setMinimumHeight(120);
  logDock->setMaximumHeight(std::max(160, currentHeight / 3));
}

void SiliconWindow::closeEvent(QCloseEvent* event)
{
  if (workspace->hasUnsavedChanges() && !closeAfterSaveConfirmation) {
    event->ignore();
    fileController->confirmSaveIfDirty([this] {
      closeAfterSaveConfirmation = true;
      close();
    });
    return;
  }

  closeAfterSaveConfirmation = false;
  QMainWindow::closeEvent(event);
}

void SiliconWindow::updateComponentCatalogGeometry()
{
  if (!componentCatalogOverlay || !circuitEditor->view())
    return;

  componentCatalogOverlay->setGeometry(circuitEditor->view()->viewport()->rect());
}

void SiliconWindow::updatePropertyDock()
{
  propertyPanel->refresh();
}

void SiliconWindow::refreshActiveDocumentUi()
{
  actionSet->updateSubcircuitShapeAction();
  actionSet->updateEditActions();
  actionSet->updateHistoryActions();
  updatePropertyDock();
}

void SiliconWindow::refreshProjectUi()
{
  updatePropertyDock();
}

SiliconWindow::~SiliconWindow()
{
#ifdef __EMSCRIPTEN__
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
                                  nullptr);
#endif

  // QObject disconnects receivers in its base destructor, but by then this class's
  // C++ members have already been destroyed. Some children (notably QUndoStack)
  // emit state-change signals from their destructors, so disconnect every owned
  // sender while SiliconWindow is still fully alive.
  const auto ownedObjects =
      findChildren<QObject*>(QString(), Qt::FindChildrenRecursively);
  for (auto* object : ownedObjects)
    disconnect(object, nullptr, this, nullptr);

  // QToolBar only releases its transient drag state in mouseReleaseEvent().
  // Finish a pending drag before Qt destroys the toolbar during window teardown.
  if (actionSet->toolBar) {
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(), QPointF(),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(actionSet->toolBar, &releaseEvent);
  }

  delete propertyPanel;
  propertyPanel = nullptr;
  delete waveformController;
  waveformController = nullptr;
  delete architectureController;
  architectureController = nullptr;
  delete actionSet;
  actionSet = nullptr;
  delete fileController;
  fileController = nullptr;
  delete interactionController;
  interactionController = nullptr;
  // Child teardown sends events that still reach this filter, so stop watching
  // before the workspace and its viewport start being destroyed.
  if (watchedViewport) {
    watchedViewport->removeEventFilter(this);
    watchedViewport = nullptr;
  }
  delete workspace;
  workspace = nullptr;
  delete documentController;
  documentController = nullptr;
}

void SiliconWindow::initializeProjectTree()
{
  projectTree = new ProjectTree(documentDock);
  documentDock->setWidget(projectTree);

  connect(projectTree, &QTreeWidget::itemSelectionChanged, this,
          &SiliconWindow::projectTreeSelectionChanged);
  connect(projectTree, &QTreeWidget::customContextMenuRequested, this,
          &SiliconWindow::showProjectTreeContextMenu);
}

void SiliconWindow::projectTreeSelectionChanged()
{
  const QSignalBlocker blocker(circuitEditor->scene());
  circuitEditor->scene()->clearSelection();

  if (const auto selection =
          projectTree ? projectTree->selectedDocument() : std::nullopt) {
    documentController->switchToDocument(selection->path, false);
    return;
  }

  actionSet->setActionsEnabled({actionSet->circuit.rotate, actionSet->edit.cut,
                                actionSet->edit.copy, actionSet->edit.remove},
                               false);
  updatePropertyDock();
}

void SiliconWindow::showProjectTreeContextMenu(const QPoint& position)
{
  if (!projectTree)
    return;

  projectTree->clearSelection();
  if (auto* item = projectTree->itemAt(position)) {
    projectTree->setCurrentItem(item);
    item->setSelected(true);
  } else {
    projectTree->setCurrentItem(nullptr);
  }

#ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
#else
  QMenu stackMenu(this);
  auto* menu = &stackMenu;
#endif

  auto* newMenu = menu->addMenu(Icon("file"), tr("New"));
  newMenu->addAction(actionSet->documents.newCircuit);
  newMenu->addAction(actionSet->documents.newCodeFile);
  newMenu->addAction(actionSet->documents.newArchitecture);
  newMenu->addAction(actionSet->documents.newBinaryFile);

  menu->addSeparator();
  menu->addAction(Icon("import"), tr("Import Document..."), documentController,
                  &ProjectDocumentController::importProjectDocument);

  if (const auto selection = projectTree->selectedDocument()) {
    const bool architecture = ProjectTree::itemKind(projectTree->selectedProjectItem())
                              == ProjectTreeItemKind::Architecture;
    menu->addAction(Icon("export"), tr("Export Document..."), documentController,
                    &ProjectDocumentController::exportSelectedDocument);
    menu->addSeparator();
    menu->addAction(
        Icon("pencil"),
        architecture ? tr("Rename ISA Architecture...") : tr("Rename Document..."),
        documentController, &ProjectDocumentController::renameSelectedDocument);
    auto* deleteAction = menu->addAction(
        Icon("delete"),
        architecture ? tr("Delete ISA Architecture")
                     : tr("Delete %1").arg(documentTypeName(selection->type)),
        documentController, &ProjectDocumentController::deleteSelectedDocument);
    const auto circuitCount = std::ranges::count_if(
        projectSession.projectContext.documents().getDocuments(),
        [](const auto& document) {
          return document.getType() == SILICON::project::DocumentType::Circuit;
        });
    deleteAction->setEnabled(selection->type != SILICON::project::DocumentType::Circuit
                             || circuitCount > 1);
  }

#ifdef __EMSCRIPTEN__
  menu->popup(projectTree->viewport()->mapToGlobal(position));
#else
  menu->exec(projectTree->viewport()->mapToGlobal(position));
#endif
}

}  // namespace SILICON::ui
