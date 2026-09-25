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

#include <algorithm>
#include <cstring>
#include <ranges>

#include <QApplication>
#include <QClipboard>
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
#include <QStackedWidget>
#include <QTextDocument>
#include <QToolBar>
#include <QUndoStack>
#include <QWidget>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>
#include <core/simulator.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/binaryEditor.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/graphicalLogStream.hpp>
#include <ui/common/logSideView.hpp>
#include <ui/logiFlow/code/codeEditor.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>

namespace SILICON {
namespace ui {
  using namespace SILICON::core;

  const SILICON::logging::Logger uiLog("ui");

  LogiFlowWindow::~LogiFlowWindow()
  {
#ifdef __EMSCRIPTEN__
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
                                    nullptr);
#endif

    // QObject disconnects receivers in its base destructor, but by then this class's
    // C++ members have already been destroyed. Some children (notably QUndoStack)
    // emit state-change signals from their destructors, so disconnect every owned
    // sender while LogiFlowWindow is still fully alive.
    const auto ownedObjects =
        findChildren<QObject*>(QString(), Qt::FindChildrenRecursively);
    for (auto* object : ownedObjects)
      disconnect(object, nullptr, this, nullptr);

    // QToolBar only releases its transient drag state in mouseReleaseEvent().
    // Finish a pending drag before Qt destroys the toolbar during window teardown.
    if (toolBar) {
      QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(), QPointF(),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(toolBar, &releaseEvent);
    }

    // QObject children are normally deleted by QMainWindow's base destructor, which
    // runs after this class's members have been destroyed. The scene's graphical
    // components unsubscribe from projectContext during teardown, so destroy the scene
    // explicitly while projectContext and circuitResolver are still alive.
    if (diagramScene) {
      if (diagramView)
        diagramView->setScene(nullptr);
      delete diagramScene;
      diagramScene = nullptr;
    }
  }

#ifdef __EMSCRIPTEN__
  EM_BOOL LogiFlowWindow::wasmKeyDownCallback(int,
                                              const EmscriptenKeyboardEvent* keyEvent,
                                              void*                          userData)
  {
    if (!userData || !keyEvent)
      return EM_FALSE;

    if (std::strcmp(keyEvent->key, "Escape") != 0
        && std::strcmp(keyEvent->code, "Escape") != 0)
      return EM_FALSE;

    auto* window = static_cast<LogiFlowWindow*>(userData);
    return window->handleWasmEscapeKey() ? EM_TRUE : EM_FALSE;
  }

  bool LogiFlowWindow::handleWasmEscapeKey()
  {
    if (QApplication::activeModalWidget())
      return false;

    if (componentCatalogOverlay && componentCatalogOverlay->isVisible()) {
      componentCatalogOverlay->hide();
      return true;
    }

    if (diagramScene)
      diagramScene->cancelCurrentInteraction();

    return true;
  }
#endif

  LogiFlowWindow::LogiFlowWindow()
  {
    const auto centralWidget = new QWidget();
    setCentralWidget(centralWidget);

    const auto layout = new QHBoxLayout();
    layout->setContentsMargins(5, 5, 5, 5);
    centralWidget->setLayout(layout);

    componentsDock = new QDockWidget(this);
    propertyDock   = new QDockWidget(this);
    logDock        = new QDockWidget(this);

    addDockWidget(Qt::LeftDockWidgetArea, componentsDock);
    addDockWidget(Qt::LeftDockWidgetArea, propertyDock);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    propertyDock->setFeatures(QDockWidget::DockWidgetMovable);
    componentsDock->setFeatures(QDockWidget::DockWidgetMovable);
    logDock->setFeatures(QDockWidget::DockWidgetMovable);
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    propertyDock->setWindowTitle("Properties");
    componentsDock->setWindowTitle("Project");
    logDock->setWindowTitle("Logs");

    splitDockWidget(componentsDock, propertyDock, Qt::Vertical);

    diagramScene = new DiagramScene(this);
    diagramScene->setDocumentStore(&projectContext.documents());
    diagramScene->setCircuitResolver(&circuitResolver);
    diagramView = new DiagramView(this);
    diagramView->setScene(diagramScene);
    codeEditor = new CodeEditor(this);
    connect(codeEditor->document(), &QTextDocument::modificationChanged, this,
            [this](const bool modified) {
              if (modified && codeEditor->fileType())
                codeDocumentsDirty = true;
            });
    binaryEditor = new BinaryEditor(this);
    initializeArchitectureEditor();
    connect(binaryEditor->history(), &QUndoStack::cleanChanged, this,
            [this](const bool clean) {
              const auto type = activeDocumentType();
              if (!clean && type
                  && SILICON::project::categoryOf(*type)
                         == SILICON::project::DocumentCategory::Binary)
                binaryDocumentsDirty = true;
            });
    editorStack = new QStackedWidget(this);
    editorStack->addWidget(diagramView);
    editorStack->addWidget(codeEditor);
    editorStack->addWidget(architectureTabs);
    editorStack->addWidget(binaryEditor);
    editorStack->setCurrentWidget(diagramView);

    connect(diagramScene, &DiagramScene::modeChanged, this,
            &LogiFlowWindow::updateStatus);
    connect(diagramScene, &DiagramScene::modeChanged, this,
            [this] { updateEditActions(); });
    updateStatus();

    connect(diagramScene, &DiagramScene::selectionChanged, this,
            &LogiFlowWindow::selectionChanged);

    layout->addWidget(editorStack);
    componentCatalogOverlay =
        new ComponentCatalogOverlay(diagramScene, projectContext.documents(),
                                    &circuitResolver, diagramView->viewport());
    diagramView->viewport()->installEventFilter(this);
    updateComponentCatalogGeometry();
    initializeProjectTree();

    aboutDialog = new AboutDialog("SILICON", this);

    undoStack = new QUndoStack(this);

    createActions();
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            &LogiFlowWindow::updateEditActions);
    applyStoredSettings();
    createMenus();
    createToolBar();
    createWaveformWindow();

    logSideView        = new LogSideView(logDock);
    graphicalLogStream = new GraphicalLogStream(this);
    logDock->setWidget(logSideView);
    logDock->setMinimumHeight(logSideView->minimumSizeHint().height());
    logDock->resize(width(), logSideView->sizeHint().height());

#ifdef __EMSCRIPTEN__
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true,
                                    &LogiFlowWindow::wasmKeyDownCallback);
#endif

    connect(graphicalLogStream, &GraphicalLogStream::lineReceived, logSideView,
            &LogSideView::appendLine, Qt::QueuedConnection);
    graphicalLogStream->attachToBoostLog();

    resizeDocks({componentsDock, propertyDock}, {320, 260}, Qt::Vertical);
    resizeDocks({logDock}, {180}, Qt::Vertical);

    setWindowTitle(tr("SILICON LogiFlow"));
    setMinimumSize(160, 160);

    resetProjectState();
    rebuildProjectTree();
    updatePropertyDock();

    uiLog.info("Qt logging sideview initialized");
  }

#ifndef QT_NO_CONTEXTMENU
  void LogiFlowWindow::contextMenuEvent(QContextMenuEvent* event)
  {
  #ifdef __EMSCRIPTEN__
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(cutAct);
    menu->addAction(copyAct);
    menu->addAction(pasteAct);
    menu->addAction(rotateAct);
    menu->addAction(deleteAct);
    menu->popup(event->globalPos());
  #else
    QMenu menu(this);
    menu.addAction(cutAct);
    menu.addAction(copyAct);
    menu.addAction(pasteAct);
    menu.addAction(rotateAct);
    menu.addAction(deleteAct);
    menu.exec(event->globalPos());
  #endif
    event->accept();
  }
#endif  // QT_NO_CONTEXTMENU

  bool LogiFlowWindow::eventFilter(QObject* watched, QEvent* event)
  {
    if (diagramView && watched == diagramView->viewport()
        && event->type() == QEvent::Resize) {
      updateComponentCatalogGeometry();
    }

    return QMainWindow::eventFilter(watched, event);
  }

  void LogiFlowWindow::resizeEvent(QResizeEvent* event)
  {
    QMainWindow::resizeEvent(event);
    updateComponentCatalogGeometry();

    const int currentWidth  = event->size().width();
    const int currentHeight = event->size().height();

    const int minWidth = currentWidth / 10;
    const int maxWidth = currentWidth / 2;

    const int minHeight = currentHeight / 3;

    auto configureSizeConstraints = [minWidth, maxWidth, minHeight](QDockWidget* widget) {
      widget->setMinimumWidth(minWidth);
      widget->setMaximumWidth(maxWidth);
      widget->setMinimumHeight(minHeight);
    };

    configureSizeConstraints(componentsDock);
    configureSizeConstraints(propertyDock);

    logDock->setMinimumWidth(160);
    logDock->setMaximumWidth(QWIDGETSIZE_MAX);
    logDock->setMinimumHeight(120);
    logDock->setMaximumHeight(std::max(160, currentHeight / 3));
  }

  bool LogiFlowWindow::hasUnsavedChanges() const
  {
    const bool architectureModified = std::ranges::any_of(
        architectureEditors, [](const auto& entry) {
          return entry.second->document()->isModified();
        });
    return (undoStack && !undoStack->isClean()) || codeDocumentsDirty
           || binaryDocumentsDirty || (codeEditor && codeEditor->document()->isModified())
           || architectureModified || (binaryEditor && binaryEditor->isModified());
  }

  void LogiFlowWindow::closeEvent(QCloseEvent* event)
  {
    if (hasUnsavedChanges() && !closeAfterSaveConfirmation) {
      event->ignore();
      confirmSaveIfDirty([this] {
        closeAfterSaveConfirmation = true;
        close();
      });
      return;
    }

    closeAfterSaveConfirmation = false;
    QMainWindow::closeEvent(event);
  }

  void LogiFlowWindow::updateComponentCatalogGeometry()
  {
    if (!componentCatalogOverlay || !diagramView)
      return;

    componentCatalogOverlay->setGeometry(diagramView->viewport()->rect());
  }


}  // namespace ui
}  // namespace SILICON
