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

/** Finishes Qt drag state while the main-window layout is still alive. */
class WindowLifetime {
public:
  explicit WindowLifetime(QMainWindow& window) : window(window) {}
  ~WindowLifetime()
  {
    // Non-closable docks accept close after the main window has been hidden.
    window.hide();
    for (const auto& dock : docks) {
      if (dock)
        dock->close();
    }
    // Qt releases toolbar drag state in mouseReleaseEvent(), not its destructor.
    if (toolBar) {
      QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(), QPointF(),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(toolBar, &releaseEvent);
    }
  }

  QPointer<QDockWidget> docks[3];
  QPointer<QToolBar>    toolBar;

private:
  QMainWindow& window;
};

#ifdef __EMSCRIPTEN__
class BrowserKeyDownRegistration {
public:
  BrowserKeyDownRegistration(void* owner, em_key_callback_func callback)
  {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, owner, true,
                                    callback);
  }
  ~BrowserKeyDownRegistration()
  {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
                                    nullptr);
  }
};
#endif

SiliconWindow::SiliconWindow()
{
  windowLifetime = std::make_unique<WindowLifetime>(*this);
  // --- Layout Setup --------------------------------------------------------------------
  const auto centralWidget = new QWidget();
  setCentralWidget(centralWidget);

  const auto layout = new QHBoxLayout();
  layout->setContentsMargins(5, 5, 5, 5);
  centralWidget->setLayout(layout);

  documentDock = new QDockWidget(this);
  propertyDock = new QDockWidget(this);
  logDock      = new QDockWidget(this);
  windowLifetime->docks[0] = documentDock;
  windowLifetime->docks[1] = propertyDock;
  windowLifetime->docks[2] = logDock;

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

  workspace     = std::make_unique<EditorWorkspace>(projectSession, centralWidget);
  circuitEditor = &workspace->circuitEditor();

  callbacks.connect(circuitEditor->scene(), &DiagramScene::modeChanged,
                    [this] { updateStatus(); });
  updateStatus();

  layout->addWidget(workspace.get());
  componentCatalogOverlay = new ComponentCatalogOverlay(
      circuitEditor->scene(), projectSession.projectContext.documents(),
      &projectSession.circuitResolver, circuitEditor->view()->viewport());
  viewportFilter.reset(circuitEditor->view()->viewport(), this);
  updateComponentCatalogGeometry();
  initializeProjectTree();
  undoStack = std::make_unique<QUndoStack>(this);
  // The circuit scene, its document editor and the rest of the shell share one history:
  // the workspace hands it to every editor it hosts.
  workspace->setProjectHistory(undoStack.get());
  circuitEditor->scene()->setUndoStack(undoStack.get());
  interactionController = std::make_unique<DiagramInteractionController>(
      projectSession, *circuitEditor, *componentCatalogOverlay, *undoStack, *this);
  documentController = std::make_unique<ProjectDocumentController>(projectSession, *workspace, *projectTree,
                                    *componentCatalogOverlay, *undoStack, this);
  architectureController = std::make_unique<ArchitectureController>(projectSession, *workspace, *documentController, this);
  circuitEditor->scene()->setDocumentNavigator(documentController.get());
  callbacks.connect(documentController.get(), &ProjectDocumentController::activeDocumentChanged,
          [this](const QString&, SILICON::project::DocumentCategory) {
            refreshActiveDocumentUi();
          });
  callbacks.connect(documentController.get(), &ProjectDocumentController::projectDocumentsChanged,
                    [this] { refreshProjectUi(); });

  aboutDialog = new AboutDialog("SILICON", this);

  fileController = std::make_unique<ProjectFileController>(projectSession, *workspace,
                                             *documentController, *undoStack, this);
  callbacks.connect(fileController.get(), &ProjectFileController::projectChanged,
                    [this] { refreshProjectUi(); });
  propertyPanel = std::make_unique<PropertyPanel>(propertyDock, circuitEditor->scene(), projectTree, undoStack.get(),
                        projectSession.currentProjectInfo, projectSession.currentFileName,
                        [this] { documentController->rebuildTree(); });

  actionSet = std::make_unique<WindowActions>(*this, projectSession, *workspace);
  actionSet->createActions();
  callbacks.connect(circuitEditor->scene(), &DiagramScene::selectionChanged,
                    [this] { selectionChanged(); });
  waveformController = std::make_unique<WaveformController>(projectSession, *circuitEditor->scene(),
                                              *documentController, this);
  wireActions();
  actionSet->applyStoredSettings();
  actionSet->createMenus();
  actionSet->createToolBar();
  windowLifetime->toolBar = actionSet->toolBar;

  logSideView = new LogSideView(logDock);
  connect(circuitEditor->scene(), &DiagramScene::logsClearRequested, logSideView,
          &LogSideView::clear);
  graphicalLogStream = std::make_unique<GraphicalLogStream>(this);
  logDock->setWidget(logSideView);
  logDock->setMinimumHeight(logSideView->minimumSizeHint().height());
  logDock->resize(width(), logSideView->sizeHint().height());

#ifdef __EMSCRIPTEN__
  browserKeyDown = std::make_unique<BrowserKeyDownRegistration>(
      this,
                                  &SiliconWindow::wasmKeyDownCallback);
#endif

  connect(graphicalLogStream.get(), &GraphicalLogStream::lineReceived, logSideView,
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
  connect(actionSet->project.newProject, &QAction::triggered, fileController.get(),
          &ProjectFileController::newFile);
  connect(actionSet->project.open, &QAction::triggered, fileController.get(),
          &ProjectFileController::open);
  callbacks.connect(actionSet->project.save, &QAction::triggered,
          [this] { fileController->save(); });
  callbacks.connect(actionSet->project.exportImage, &QAction::triggered,
                    [this] { exportImage(); });
  callbacks.connect(actionSet->project.exit, &QAction::triggered, [this] { close(); });
  connect(actionSet->project.settings, &QAction::triggered, actionSet.get(),
          &WindowActions::openSettings);
  callbacks.connect(actionSet->project.about, &QAction::triggered, [this] { about(); });

  connect(actionSet->documents.newCircuit, &QAction::triggered, documentController.get(),
          &ProjectDocumentController::createCircuit);
  connect(actionSet->documents.newCodeFile, &QAction::triggered, documentController.get(),
          &ProjectDocumentController::createCodeFile);
  connect(actionSet->documents.newArchitecture, &QAction::triggered, documentController.get(),
          &ProjectDocumentController::createArchitecture);
  connect(actionSet->documents.newBinaryFile, &QAction::triggered, documentController.get(),
          &ProjectDocumentController::createBinaryFile);
  connect(actionSet->documents.codeConversion, &QAction::triggered, documentController.get(),
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
  if (watched == viewportFilter.target() && event->type() == QEvent::Resize) {
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
    fileController->confirmSaveIfDirty(callbacks.guard([this] {
      closeAfterSaveConfirmation = true;
      close();
    }));
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

SiliconWindow::~SiliconWindow() = default;

void SiliconWindow::initializeProjectTree()
{
  projectTree = new ProjectTree(documentDock);
  documentDock->setWidget(projectTree);

  callbacks.connect(projectTree, &QTreeWidget::itemSelectionChanged,
                    [this] { projectTreeSelectionChanged(); });
  callbacks.connect(projectTree, &QTreeWidget::customContextMenuRequested,
      [this](const QPoint& position) { showProjectTreeContextMenu(position); });
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
  menu->addAction(Icon("import"), tr("Import Document..."), documentController.get(),
                  &ProjectDocumentController::importProjectDocument);

  if (const auto selection = projectTree->selectedDocument()) {
    const bool architecture = ProjectTree::itemKind(projectTree->selectedProjectItem())
                              == ProjectTreeItemKind::Architecture;
    menu->addAction(Icon("export"), tr("Export Document..."), documentController.get(),
                    &ProjectDocumentController::exportSelectedDocument);
    menu->addSeparator();
    menu->addAction(
        Icon("pencil"),
        architecture ? tr("Rename ISA Architecture...") : tr("Rename Document..."),
        documentController.get(), &ProjectDocumentController::renameSelectedDocument);
    auto* deleteAction = menu->addAction(
        Icon("delete"),
        architecture ? tr("Delete ISA Architecture")
                     : tr("Delete %1").arg(documentTypeName(selection->type)),
        documentController.get(), &ProjectDocumentController::deleteSelectedDocument);
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
