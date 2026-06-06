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
#include <limits>
#include <ranges>
#include <stdexcept>
#include <variant>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <core/serialization/component_registry.hpp>
#include <core/simulator.hpp>
#include <logging/logger.hpp>
#include <ui/common/graphicalLogStream.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/logSideView.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/waveformViewer.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace {

// Clipboard data is intentionally BSON-only so partial JSON fallbacks cannot drift
// from the native LogiFlow selection format.
constexpr auto LogiFlowSelectionMimeType =
    "application/vnd.silicon.logiflow-selection+bson";

ShortcutSetting shortcut(const QString& key, const QString& label, QAction* action,
                         const QKeySequence& defaultShortcut)
{
  return {
      .setting =
          {
              .name         = key,
              .defaultValue = QVariant::fromValue(defaultShortcut),
          },
      .label  = label,
      .action = action,
  };
}

bool hasClipboardItems(const nlohmann::json& payload)
{
  if (!payload.contains("visual") || !payload["visual"].is_object())
    return false;

  const auto& visual        = payload["visual"];
  const bool  hasComponents = visual.contains("components")
                             && visual["components"].is_array()
                             && !visual["components"].empty();
  const bool hasWires =
      visual.contains("wires") && visual["wires"].is_array() && !visual["wires"].empty();

  return hasComponents || hasWires;
}

Logger uiLog("ui");

}  // namespace

LogiFlowWindow::~LogiFlowWindow()
{
  if (diagramScene) {
    disconnect(diagramScene, nullptr, this, nullptr);
  }
}

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
  waveformWindow = nullptr;

  addDockWidget(Qt::LeftDockWidgetArea, componentsDock);
  addDockWidget(Qt::LeftDockWidgetArea, propertyDock);
  addDockWidget(Qt::BottomDockWidgetArea, logDock);

  propertyDock->setFeatures(QDockWidget::DockWidgetMovable);
  componentsDock->setFeatures(QDockWidget::DockWidgetMovable);
  logDock->setFeatures(QDockWidget::DockWidgetMovable);
  logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

  propertyDock->setWindowTitle("Properties");
  componentsDock->setWindowTitle("Components");
  logDock->setWindowTitle("Logs");

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
  applyStoredSettings();
  createMenus();
  createToolBar();
  createWaveformWindow();

  logSideView        = new LogSideView(logDock);
  graphicalLogStream = new GraphicalLogStream(this);
  logDock->setWidget(logSideView);
  logDock->setMinimumHeight(logSideView->minimumSizeHint().height());
  logDock->resize(width(), logSideView->sizeHint().height());

  connect(graphicalLogStream, &GraphicalLogStream::lineReceived, logSideView,
          &LogSideView::appendLine, Qt::QueuedConnection);
  graphicalLogStream->attachToBoostLog();

  resizeDocks({componentsDock, propertyDock}, {320, 260}, Qt::Vertical);
  resizeDocks({logDock}, {180}, Qt::Vertical);

  setWindowTitle(tr("SILICON LogiFlow"));
  setMinimumSize(160, 160);

  updatePropertyDock();

  uiLog.info("Qt logging sideview initialized");
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
  settingsAct    = new QAction(Icon("settings"), tr("&Settings..."), this);

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
  toggleFstTraceAct      = new QAction(Icon("chart"), tr("Trace"), this);
  toggleFstTraceAct->setCheckable(true);

  setComponentPlacingModeAct = new QAction(Icon("plus"), "", this);

  toggleFstTraceAct->setStatusTip(tr("Show waveform viewer"));
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
  settingsAct->setStatusTip(tr("Edit application settings"));

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
  connect(settingsAct, &QAction::triggered, this, &LogiFlowWindow::openSettings);

  connect(setNormalModeAct, &QAction::triggered, this, &LogiFlowWindow::setNormalMode);
  connect(setPanModeAct, &QAction::triggered, this, &LogiFlowWindow::setPanMode);
  connect(setWireCreationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setWireCreationMode);
  connect(setSimulationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setSimulationMode);
  connect(setComponentPlacingModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setComponentPlacingMode);
  connect(toggleFstTraceAct, &QAction::toggled, this, &LogiFlowWindow::toggleFstTracing);
}

QVector<ShortcutSetting> LogiFlowWindow::shortcutSettings() const
{
  return {
      shortcut(QStringLiteral("keybindings/new"), tr("New"), newAct,
               QKeySequence(QKeySequence::New)),
      shortcut(QStringLiteral("keybindings/open"), tr("Open"), openAct,
               QKeySequence(QKeySequence::Open)),
      shortcut(QStringLiteral("keybindings/save"), tr("Save"), saveAct,
               QKeySequence(QKeySequence::Save)),
      shortcut(QStringLiteral("keybindings/exportImage"), tr("Export"), exportImageAct,
               QKeySequence(QKeySequence::Print)),
      shortcut(QStringLiteral("keybindings/exit"), tr("Exit"), exitAct,
               QKeySequence(QKeySequence::Quit)),
      shortcut(QStringLiteral("keybindings/undo"), tr("Undo"), undoAct,
               QKeySequence(QKeySequence::Undo)),
      shortcut(QStringLiteral("keybindings/redo"), tr("Redo"), redoAct,
               QKeySequence(QKeySequence::Redo)),
      shortcut(QStringLiteral("keybindings/cut"), tr("Cut"), cutAct,
               QKeySequence(QKeySequence::Cut)),
      shortcut(QStringLiteral("keybindings/copy"), tr("Copy"), copyAct,
               QKeySequence(QKeySequence::Copy)),
      shortcut(QStringLiteral("keybindings/paste"), tr("Paste"), pasteAct,
               QKeySequence(QKeySequence::Paste)),
      shortcut(QStringLiteral("keybindings/rotate"), tr("Rotate"), rotateAct,
               QKeySequence(Qt::AltModifier | Qt::Key_R)),
      shortcut(QStringLiteral("keybindings/delete"), tr("Delete"), deleteAct,
               QKeySequence(QKeySequence::Delete)),
      shortcut(QStringLiteral("keybindings/normalMode"), tr("Normal mode"),
               setNormalModeAct, QKeySequence()),
      shortcut(QStringLiteral("keybindings/panMode"), tr("Pan mode"), setPanModeAct,
               QKeySequence()),
      shortcut(QStringLiteral("keybindings/wireCreationMode"), tr("Wire creation mode"),
               setWireCreationModeAct, QKeySequence(Qt::AltModifier | Qt::Key_W)),
      shortcut(QStringLiteral("keybindings/simulationMode"), tr("Simulation mode"),
               setSimulationModeAct,
               QKeySequence(Qt::AltModifier | Qt::ControlModifier | Qt::Key_S)),
      shortcut(QStringLiteral("keybindings/componentPlacingMode"),
               tr("Component placing mode"), setComponentPlacingModeAct,
               QKeySequence(Qt::AltModifier | Qt::Key_A)),
      shortcut(QStringLiteral("keybindings/toggleTrace"), tr("Waveform trace"),
               toggleFstTraceAct, QKeySequence()),
      shortcut(QStringLiteral("keybindings/settings"), tr("Settings"), settingsAct,
               QKeySequence()),
  };
}

void LogiFlowWindow::applyStoredSettings()
{
  SiliconSettings            settings("LogiFlow", this);
  const CommonSettingsValues values = readCommonSettings(settings);

  Simulator::setMaxSimulationSteps(static_cast<uint64_t>(values.maxSimulationSteps));
  Simulator::setMaxTransitionsPerDeltaCycle(values.maxTransitionsPerDeltaCycle);

  ThemeEngine::apply(*qApp, themeModeFromText(values.theme));

  for (const ShortcutSetting& shortcut : shortcutSettings()) {
    shortcut.action->setShortcut(
        SiliconSetting::value(settings, shortcut.setting).value<QKeySequence>());
  }
}

void LogiFlowWindow::createMenus()
{
  fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(newAct);
  fileMenu->addAction(openAct);
  fileMenu->addAction(saveAct);
  fileMenu->addAction(exportImageAct);
  fileMenu->addAction(toggleFstTraceAct);
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
  editMenu->addAction(settingsAct);

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
  toolBar->addAction(toggleFstTraceAct);

  toolBar->addSeparator();
  toolBar->addAction(setComponentPlacingModeAct);

  addToolBar(toolBar);
}

void LogiFlowWindow::createWaveformWindow()
{
  waveformWindow = new QDialog(this);
  waveformWindow->setWindowTitle(tr("Waveform"));
  waveformWindow->setModal(false);
  waveformWindow->resize(900, 420);

  const auto layout = new QVBoxLayout(waveformWindow);
  layout->setContentsMargins(0, 0, 0, 0);

  waveformViewer = new WaveformViewer(waveformWindow);
  layout->addWidget(waveformViewer);

  connect(waveformWindow, &QDialog::finished, this, [this] {
    const QSignalBlocker blocker(toggleFstTraceAct);
    toggleFstTraceAct->setChecked(false);
  });
  connect(diagramScene, &DiagramScene::waveformTraceReset, waveformViewer,
          &WaveformViewer::resetTrace);
  connect(diagramScene, &DiagramScene::waveformTraceSnapshot, waveformViewer,
          &WaveformViewer::appendSnapshot);
}

void LogiFlowWindow::setFileName(const QString& fn)
{
  currentFileName               = fn;
  const QString displayFileName = QFileInfo(currentFileName).fileName();

  if (!displayFileName.isEmpty())
    setWindowTitle(QString("LogiFlow - %1").arg(displayFileName));
  else
    setWindowTitle("SILICON LogiFlow");
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

void LogiFlowWindow::resizeEvent(QResizeEvent* event)
{
  QMainWindow::resizeEvent(event);

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

/* ACTIONS IMPLEMENTATION */

void LogiFlowWindow::newFile()
{
  setFileName("");
  diagramScene->clear();
}

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

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void LogiFlowWindow::copy()
{
  copySelectionToClipboard();
}

void LogiFlowWindow::paste()
{
  // Ignore anything that is not a Silicon LogiFlow selection payload.
  const QMimeData* mimeData = QApplication::clipboard()->mimeData();
  if (!mimeData || !mimeData->hasFormat(LogiFlowSelectionMimeType))
    return;

  const QByteArray bytes = mimeData->data(LogiFlowSelectionMimeType);
  if (bytes.isEmpty())
    return;

  try {
    std::vector<std::uint8_t> bson;
    bson.reserve(static_cast<size_t>(bytes.size()));
    for (const char byte : bytes)
      bson.push_back(static_cast<std::uint8_t>(byte));

    auto&      guiFactory   = GUIComponentFactory::instance();
    auto&      coreRegistry = ComponentRegistry::instance();
    const auto payload      = nlohmann::json::from_bson(bson);

    // Pasting changes scene topology, so leave simulation/placement modes first.
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
  } catch (const nlohmann::json::exception&) {
    return;
  } catch (const std::exception&) {
    return;
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
      auto oldRotation = component->rotation();
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

void LogiFlowWindow::del()
{
  auto itemsToDelete = diagramScene->selectedItems() | std::views::filter([](auto el) {
                         // Trying to remove non user-defined components leads to crash
                         return el->type() > UNKNOWN;
                       })
                       | std::ranges::to<std::vector>();

  if (itemsToDelete.empty())
    return;

  const auto payload = diagramScene->serializeItems(itemsToDelete);
  diagramScene->removeItems(itemsToDelete);

  undoStack->push(new SceneSelectionCommand(
      diagramScene, payload, SceneSelectionCommand::Operation::Remove, true));
}

void LogiFlowWindow::open()
{
  // 1. Ask the user for the file
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Circuit"), QString(), tr("Silicon Circuit (*.sil);;All Files (*)"));

  // User canceled the dialog
  if (fileName.isEmpty()) {
    return;
  }

  uiLog.info(std::format("Opening {}", fileName.toStdString()));

  // 2. Open the file for reading
  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Cannot open file for reading:\n%1").arg(file.errorString()));
    return;
  }

  // 3. Read the contents with strict UTF-8 encoding
  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);

  QString fileContent = in.readAll();
  file.close();

  // 4. Deserialize and update the scene safely
  try {
    // Clear the current scene items to prepare for the new circuit.
    diagramScene->clear();

    // Fetch the global registry singletons
    auto& guiFactory   = GUIComponentFactory::instance();
    auto& coreRegistry = ComponentRegistry::instance();

    // Delegate parsing to the scene
    diagramScene->deserialize(fileContent.toStdString(), guiFactory, coreRegistry);

    // 5. Update application state on success
    setFileName(fileName);

  } catch (const nlohmann::json::exception& e) {
    // Catches JSON parsing/formatting errors
    QMessageBox::critical(
        this, tr("Corrupted File"),
        tr("The circuit file contains invalid JSON data:\n%1").arg(e.what()));
  } catch (const std::exception& e) {
    // Catches missing components, version mismatches (from Circuit::deserialize), etc.
    QMessageBox::critical(this, tr("Load Error"),
                          tr("Failed to load the circuit:\n%1").arg(e.what()));
  }
}

void LogiFlowWindow::save()
{
  if (currentFileName.isEmpty()) {
    QString desiredFileName =
        QFileDialog::getSaveFileName(this, tr("Save Circuit"), QString(),
                                     tr("Silicon Circuit (*.sil);;All Files (*)"));
    if (desiredFileName.isEmpty())
      return;

    setFileName(desiredFileName);
  }

  QFile file(currentFileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Cannot open file for writing: %1").arg(file.errorString()));
    return;
  }

  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  out << QString::fromStdString(diagramScene->serialize());
  file.close();
}

void LogiFlowWindow::about() const
{
  aboutDialog->show();
}

void LogiFlowWindow::openSettings()
{
  SettingsWindow settingsWindow("LogiFlow", shortcutSettings(), this);
  settingsWindow.exec();
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

void LogiFlowWindow::toggleFstTracing(bool enabled)
{
  if (!waveformWindow)
    return;

  waveformWindow->setVisible(enabled);
  if (enabled) {
    waveformWindow->raise();
    waveformWindow->activateWindow();
  }
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
    default: throw std::logic_error("Unhandled InteractionMode in modeToString");
  }

  statusBar()->showMessage(modeMsg);
}
void LogiFlowWindow::selectionChanged()
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

  updatePropertyDock();
}

void LogiFlowWindow::updatePropertyDock()
{
  // 1. Assign the container immediately.
  // QDockWidget::setWidget automatically deletes the previous widget.
  auto* container = new QWidget();
  auto* layout    = new QFormLayout(container);
  propertyDock->setWidget(container);

  // 2. Gather selected logic components cleanly
  std::vector<GraphicalLogicComponent*> selectedNodes;
  for (QGraphicsItem* item : diagramScene->selectedItems()) {
    if (auto* logicComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        logicComp && logicComp->getComponent()) {
      selectedNodes.push_back(logicComp);
    }
  }

  if (selectedNodes.empty()) {
    layout->addRow(
        new QLabel(tr("Select one or more components\nto view their properties.")));
    return;
  }

  // 3. Intersect properties to find common configurable keys
  auto commonProps = selectedNodes.front()->getComponent()->getProperties();

  for (const GraphicalLogicComponent* node : selectedNodes | std::views::drop(1)) {
    const auto& props = node->getComponent()->getProperties();

    std::erase_if(commonProps, [&](const auto& pair) {
      const auto& [key, val] = pair;
      auto it                = props.find(key);
      return it == props.end() || it->second.index() != val.index();
    });
  }

  if (commonProps.empty()) {
    layout->addRow(new QLabel(tr("No common configurable\nproperties among selection.")));
    return;
  }

  // Helper lambda to apply the property to all components and handle validation
  // exceptions
  auto applyProperty = [this, selectedNodes](const std::string&   key,
                                             const PropertyValue& newVal) {
    try {
      auto* command = new ModifyPropertyCommand(key);

      for (GraphicalLogicComponent* node : selectedNodes) {
        const auto oldValue = node->getComponent()->getProperty(key);
        if (oldValue)
          command->addPropertyChange(node, *oldValue, newVal);
      }

      if (command->isEmpty()) {
        delete command;
      } else {
        undoStack->push(command);
      }
    } catch (const std::exception& e) {
      QMessageBox::warning(this, tr("Invalid Property"), e.what());
      QTimer::singleShot(0, this, &LogiFlowWindow::updatePropertyDock);
    }
  };

  // 4. Build UI for common properties
  for (const auto& [key, initialValue] : commonProps) {
    // Check if the value differs across the selection
    const bool isMixed = std::ranges::any_of(
        selectedNodes | std::views::drop(1), [&](const GraphicalLogicComponent* node) {
          return node->getComponent()->getProperty(key) != initialValue;
        });

    auto createPropertyWidget = [&]<typename T>(const T& arg) {
      if constexpr (std::is_same_v<T, bool>) {
        auto* checkBox = new QCheckBox(container);

        if (isMixed) {
          checkBox->setTristate(true);
          checkBox->setCheckState(Qt::PartiallyChecked);
        } else {
          checkBox->setChecked(arg);
        }

        connect(checkBox, &QCheckBox::checkStateChanged, this, [=](Qt::CheckState state) {
          if (state == Qt::PartiallyChecked)
            return;
          checkBox->setTristate(false);
          applyProperty(key, state == Qt::Checked);
        });

        layout->addRow(QString::fromStdString(key), checkBox);
      } else if constexpr (std::is_same_v<T, int>) {
        auto*         spinBox = new PropertySpinBox(container);
        constexpr int MIN_VAL = std::numeric_limits<int>::min();
        constexpr int MAX_VAL = std::numeric_limits<int>::max();

        spinBox->setRange(MIN_VAL, MAX_VAL);

        if (isMixed) {
          spinBox->setMixed(true, tr("Mixed values"));
        } else {
          spinBox->setValue(arg);
        }

        connect(spinBox, &QSpinBox::valueChanged, this, [=](int val) {
          if (val == MIN_VAL && spinBox->isMixed())
            return;
          spinBox->setMixed(false);
          applyProperty(key, val);
        });

        layout->addRow(QString::fromStdString(key), spinBox);
      } else if constexpr (std::is_same_v<T, std::string>) {
        auto* lineEdit = new QLineEdit(container);

        if (isMixed) {
          lineEdit->setPlaceholderText(tr("Mixed values..."));
        } else {
          lineEdit->setText(QString::fromStdString(arg));
        }

        connect(lineEdit, &QLineEdit::editingFinished, this, [=]() {
          if (!lineEdit->isModified())
            return;

          applyProperty(key, lineEdit->text().toStdString());
          lineEdit->setModified(false);
        });

        layout->addRow(QString::fromStdString(key), lineEdit);
      }
    };

    std::visit(createPropertyWidget, initialValue);
  }
}

// --- Property SpinBox ------------------------------------------------------------------

PropertySpinBox::PropertySpinBox(QWidget* parent) : QSpinBox(parent)
{
  setButtonSymbols(NoButtons);
}

void PropertySpinBox::setMixed(const bool mixed, const QString& placeholder)
{
  if (mixed) {
    lineEdit()->setPlaceholderText(placeholder);
    setValue(minimum());
  } else {
    lineEdit()->setPlaceholderText("");
  }
}

bool PropertySpinBox::isMixed() const
{
  return m_isMixed;
}

QString PropertySpinBox::textFromValue(int val) const
{
  if (m_isMixed && val == minimum()) {
    return "";
  }
  return QSpinBox::textFromValue(val);
}
