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
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>
#include <core/serialization/yosys.hpp>
#include <core/simulator.hpp>
#include <core/subcircuitDefinition.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/graphicalLogStream.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/logSideView.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/waveformViewer.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/componentShapeEditor.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/hdlCodeEditor.hpp>
#include <ui/serialization/gui_component_factory.hpp>

const Logger uiLog("ui");

LogiFlowWindow::~LogiFlowWindow()
{
#ifdef __EMSCRIPTEN__
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
                                  nullptr);
#endif

  if (diagramScene) {
    disconnect(diagramScene, nullptr, this, nullptr);
  }
}

#ifdef __EMSCRIPTEN__
EM_BOOL LogiFlowWindow::wasmKeyDownCallback(int, const EmscriptenKeyboardEvent* keyEvent,
                                            void* userData)
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
  diagramView  = new DiagramView(this);
  diagramView->setScene(diagramScene);
  hdlEditor = new HDLCodeEditor(this);
  hdlEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
  hdlEditor->setProperty("class", "mono");
  editorStack = new QStackedWidget(this);
  editorStack->addWidget(diagramView);
  editorStack->addWidget(hdlEditor);
  editorStack->setCurrentWidget(diagramView);

  connect(diagramScene, &DiagramScene::modeChanged, this, &LogiFlowWindow::updateStatus);
  updateStatus();

  connect(diagramScene, &DiagramScene::selectionChanged, this,
          &LogiFlowWindow::selectionChanged);

  layout->addWidget(editorStack);
  componentCatalogOverlay =
      new ComponentCatalogOverlay(diagramScene, diagramView->viewport());
  diagramView->viewport()->installEventFilter(this);
  updateComponentCatalogGeometry();
  initializeProjectTree();

  aboutDialog = new AboutDialog("SILICON", this);

  undoStack = new QUndoStack(this);

  createActions();
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
