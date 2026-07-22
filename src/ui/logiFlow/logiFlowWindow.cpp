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

#include <QApplication>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QFocusEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QTemporaryFile>
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
#include <core/simulator.hpp>
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
#include <ui/serialization/gui_component_factory.hpp>

namespace {

// Clipboard data is intentionally BSON-only so partial JSON fallbacks cannot drift
// from the native LogiFlow selection format.
constexpr auto LogiFlowSelectionMimeType =
    "application/vnd.silicon.logiflow-selection+bson";

enum class ProjectTreeItemKind {
  Project,
  CircuitSection,
  Circuit,
  SubcircuitSection,
  Subcircuit
};

constexpr int ProjectTreeItemKindRole = Qt::UserRole;
constexpr int ProjectTreePathRole     = Qt::UserRole + 1;

const Logger uiLog("ui");

std::string defaultMainCircuitPath()
{
  return std::string(silicon::project::DefaultMainCircuitPath);
}

silicon::project::Document defaultCircuitDocument()
{
  return {defaultMainCircuitPath(), ""};
}

QString defaultProjectName(const QString& currentFileName)
{
  const QString baseName = QFileInfo(currentFileName).baseName();
  return baseName.isEmpty() ? QStringLiteral("Untitled Project") : baseName;
}

silicon::project::ProjectInfo defaultProjectInfo(const QString& currentFileName)
{
  return {.name = defaultProjectName(currentFileName).toStdString(),
          .mainCircuit = defaultMainCircuitPath(),
          .description = ""};
}

std::string projectMainCircuitPath(
    const std::optional<silicon::project::ProjectInfo>& projectInfo)
{
  if (projectInfo && !projectInfo->mainCircuit.empty())
    return projectInfo->mainCircuit;

  return defaultMainCircuitPath();
}

void ensureProjectDocuments()
{
  auto& store = silicon::project::DocumentStore::active();
  if (store.documents(silicon::project::DocumentKind::Circuit).empty())
    store.upsertDocument(defaultCircuitDocument());
}

QString documentDisplayName(const silicon::project::Document& document)
{
  try {
    const auto scene = nlohmann::json::parse(document.sceneJson());
    if (scene.contains("circuit") && scene["circuit"].is_object()) {
      const auto name = scene["circuit"].value("name", "");
      if (!name.empty())
        return QString::fromStdString(name);
    }
  } catch (const nlohmann::json::exception&) {
  }

  const QString fileName =
      QFileInfo(QString::fromStdString(document.path())).baseName();
  return fileName.isEmpty() ? QString::fromStdString(document.path()) : fileName;
}

std::pair<std::string, std::string> circuitMetadata(
    const silicon::project::Document& document)
{
  try {
    const auto scene = nlohmann::json::parse(document.sceneJson());
    if (scene.contains("circuit") && scene["circuit"].is_object())
      return {scene["circuit"].value("name", ""),
              scene["circuit"].value("description", "")};
  } catch (const nlohmann::json::exception&) {
  }
  return {};
}

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

QAction* makeAction(QObject* parent, const QIcon& icon, const QString& text,
                    const QString& statusTip = {})
{
  auto* action = new QAction(icon, text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

QAction* makeAction(QObject* parent, const QString& text,
                    const QString& statusTip = {})
{
  auto* action = new QAction(text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

void setActionsEnabled(std::initializer_list<QAction*> actions, const bool enabled)
{
  for (QAction* action : actions) {
    if (action)
      action->setEnabled(enabled);
  }
}

QTreeWidgetItem* selectedProjectTreeItem(QTreeWidget* projectTree)
{
  if (!projectTree)
    return nullptr;

  const auto selectedItems = projectTree->selectedItems();
  return selectedItems.empty() ? nullptr : selectedItems.front();
}

QTreeWidgetItem* projectSectionItem(QTreeWidget* projectTree,
                                    const ProjectTreeItemKind sectionKind)
{
  if (!projectTree || projectTree->topLevelItemCount() == 0)
    return nullptr;

  auto* projectItem = projectTree->topLevelItem(0);
  if (!projectItem)
    return nullptr;
  for (int i = 0; i < projectItem->childCount(); ++i) {
    auto* child = projectItem->child(i);
    if (static_cast<ProjectTreeItemKind>(
            child->data(0, ProjectTreeItemKindRole).toInt())
        == sectionKind)
      return child;
  }
  return nullptr;
}

ProjectTreeItemKind projectTreeItemKind(QTreeWidgetItem* item)
{
  return static_cast<ProjectTreeItemKind>(
      item->data(0, ProjectTreeItemKindRole).toInt());
}

std::string projectTreeCircuitPath(QTreeWidgetItem* item)
{
  return item->data(0, ProjectTreePathRole).toString().toStdString();
}

class ProjectStateCommand : public QUndoCommand {
public:
  using Fn = std::function<void()>;

  ProjectStateCommand(QString text, Fn undoFn, Fn redoFn, QUndoCommand* parent = nullptr)
    : QUndoCommand(std::move(text), parent),
      undoFn(std::move(undoFn)),
      redoFn(std::move(redoFn))
  {
  }

  void undo() override { undoFn(); }
  void redo() override { redoFn(); }

private:
  Fn undoFn;
  Fn redoFn;
};

class MetadataDescriptionEdit : public QPlainTextEdit {
public:
  explicit MetadataDescriptionEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

  std::function<void()> commit;

protected:
  void focusOutEvent(QFocusEvent* event) override
  {
    if (commit)
      commit();

    QPlainTextEdit::focusOutEvent(event);
  }
};

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

void syncWasmShortcutCapture(const QVector<ShortcutSetting>& shortcuts)
{
#ifdef __EMSCRIPTEN__
  QJsonArray shortcutSequences;
  for (const ShortcutSetting& shortcut : shortcuts) {
    if (!shortcut.action)
      continue;

    for (const QKeySequence& sequence : shortcut.action->shortcuts()) {
      if (sequence.isEmpty())
        continue;

      for (int index = 0; index < sequence.count(); ++index)
        shortcutSequences.append(
            QKeySequence(sequence[index]).toString(QKeySequence::PortableText));
    }
  }

  const QByteArray shortcutsJson =
      QJsonDocument(shortcutSequences).toJson(QJsonDocument::Compact);

  // clang-format off
  EM_ASM(
      {
        const shortcuts = JSON.parse(UTF8ToString($0));
        globalThis.__siliconShortcutSequences =
            shortcuts.map(sequence => sequence.trim()
                                           .split('+')
                                           .map(part => part.trim().toLowerCase())
                                           .filter(Boolean));

        const isEditableTarget = target => target instanceof HTMLInputElement || target
            instanceof HTMLTextAreaElement || target
            instanceof HTMLSelectElement || target?.isContentEditable;

        const normalizedKeyToken = token => ({
                                             esc : 'escape',
                                             del : 'delete',
                                             ins : 'insert',
                                             return : 'enter',
                                             enter : 'enter',
                                             backtab : 'tab',
                                             space : ' ',
                                             pgup : 'pageup',
                                             pgdown : 'pagedown',
                                             plus : '+',
                                             comma : ','
                                           })[token]
            ?? token;

        const eventMatchesShortcut = (event, shortcut) =>
        {
          if (shortcut.length === 0)
            return false;

          const key        = event.key.toLowerCase();
          const keyToken   = normalizedKeyToken(shortcut[shortcut.length - 1]);
          const wantsCtrl  = shortcut.includes('ctrl') || shortcut.includes('control');
          const wantsMeta  = shortcut.includes('meta');
          const wantsAlt   = shortcut.includes('alt');
          const wantsShift = shortcut.includes('shift');
          const usesCommandModifier = wantsCtrl || wantsMeta || wantsAlt;

          if (!usesCommandModifier && isEditableTarget(event.target))
            return false;

          if (wantsCtrl && !(event.ctrlKey || event.metaKey))
            return false;
          if (wantsMeta && !event.metaKey)
            return false;
          if (wantsAlt !== event.altKey)
            return false;
          if (wantsShift !== event.shiftKey)
            return false;

          return key === keyToken || event.code.toLowerCase() === keyToken;
        };

        globalThis.__siliconShouldCaptureShortcut = event =>
            globalThis.__siliconShortcutSequences.some(
                shortcut => eventMatchesShortcut(event, shortcut));

        if (globalThis.__siliconShortcutCaptureInstalled)
          return;

        globalThis.__siliconShortcutCaptureInstalled = true;
        document.addEventListener(
        'keydown',
        event => {
          if (globalThis.__siliconShouldCaptureShortcut?.(event))
            event.preventDefault();
        },
        true);
      },
      shortcutsJson.constData());
  // clang-format on
#endif
}

}  // namespace

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

  connect(diagramScene, &DiagramScene::modeChanged, this, &LogiFlowWindow::updateStatus);
  updateStatus();

  connect(diagramScene, &DiagramScene::selectionChanged, this,
          &LogiFlowWindow::selectionChanged);

  layout->addWidget(diagramView);
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

void LogiFlowWindow::createActions()
{
  newAct         = makeAction(this, Icon("file"), tr("&New"), tr("Create a new file"));
  openAct        = makeAction(this, Icon("open"), tr("&Open..."),
                              tr("Open an existing logiFlow file"));
  saveAct        = makeAction(this, Icon("save"), tr("&Save"),
                              tr("Save the circuit to disk"));
  exportImageAct = makeAction(this, Icon("export"), tr("&Export..."),
                              tr("Export the circuit as an image"));
  exitAct        = makeAction(this, Icon("xmark"), tr("E&xit"),
                              tr("Exit the application"));
  cutAct         = makeAction(this, Icon("cut"), tr("Cu&t"),
                              tr("Cut the current selection's contents to the clipboard"));
  copyAct        = makeAction(this, Icon("copy"), tr("&Copy"));
  pasteAct       = makeAction(this, Icon("paste"), tr("&Paste"),
                              tr("Paste the clipboard's contents into the current selection"));
  rotateAct      = makeAction(this, Icon("rotate"), tr("&Rotate"));
  deleteAct      = makeAction(this, Icon("delete"), tr("&Delete"),
                              tr("Delete selected components"));
  aboutAct       = makeAction(this, Icon("info"), tr("&About"),
                              tr("Show the application's about box"));
  settingsAct    = makeAction(this, Icon("settings"), tr("&Settings..."),
                              tr("Edit application settings"));

  undoAct = undoStack->createUndoAction(this, tr("&Undo"));
  undoAct->setIcon(Icon("undo"));
  undoAct->setStatusTip(tr("Undo the last operation"));

  redoAct = undoStack->createRedoAction(this, tr("&Redo"));
  redoAct->setIcon(Icon("redo"));
  redoAct->setStatusTip(tr("Redo the last operation"));

  setActionsEnabled({rotateAct, cutAct, copyAct, deleteAct}, false);

  setNormalModeAct       = new QAction(Icon("mouse-pointer"), "", this);
  setPanModeAct          = new QAction(Icon("pan"), "", this);
  setWireCreationModeAct = new QAction(Icon("link"), "", this);
  setSimulationModeAct   = new QAction(Icon("play"), "", this);
  toggleFstTraceAct      = makeAction(this, Icon("chart"), tr("Trace"),
                                      tr("Show waveform viewer"));
  toggleFstTraceAct->setCheckable(true);
  cancelInteractionAct = makeAction(this, QString(), tr("Cancel the current interaction"));

  openComponentCatalogAct =
      makeAction(this, Icon("plus"), "", tr("Open the component catalog"));
  editSubcircuitShapeAct =
      makeAction(this, Icon("circuit-board"), tr("Edit Shape"),
                 tr("Edit the active subcircuit shape"));
  editSubcircuitShapeAct->setVisible(false);
  editSubcircuitShapeAct->setEnabled(false);
  setComponentPlacingModeAct =
      makeAction(this, Icon("plus"), "", tr("Open quick component search"));

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
  connect(openComponentCatalogAct, &QAction::triggered, this,
          &LogiFlowWindow::showComponentCatalog);
  connect(editSubcircuitShapeAct, &QAction::triggered, this,
          &LogiFlowWindow::editActiveSubcircuitShape);
  connect(setComponentPlacingModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setComponentPlacingMode);
  connect(cancelInteractionAct, &QAction::triggered, this,
          &LogiFlowWindow::cancelCurrentInteraction);
  connect(toggleFstTraceAct, &QAction::toggled, this, &LogiFlowWindow::toggleFstTracing);

  addAction(setComponentPlacingModeAct);
  addAction(cancelInteractionAct);
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
      shortcut(QStringLiteral("keybindings/cancelInteraction"),
               tr("Cancel current interaction"), cancelInteractionAct,
               QKeySequence(Qt::Key_Escape)),
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

  const auto shortcuts = shortcutSettings();
  for (const ShortcutSetting& shortcut : shortcuts) {
    shortcut.action->setShortcut(
        SiliconSetting::value(settings, shortcut.setting).value<QKeySequence>());
#ifdef __EMSCRIPTEN__
    shortcut.action->setShortcutContext(Qt::ApplicationShortcut);
    if (!actions().contains(shortcut.action))
      addAction(shortcut.action);
#endif
  }

  syncWasmShortcutCapture(shortcuts);
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
  toolBar->addAction(openComponentCatalogAct);
  toolBar->addAction(editSubcircuitShapeAct);

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
    waveformViewer->setEditMode(false);
  });
  connect(diagramScene, &DiagramScene::waveformTraceReset, waveformViewer,
          &WaveformViewer::resetTrace);
  connect(diagramScene, &DiagramScene::waveformTraceSnapshot, waveformViewer,
          &WaveformViewer::appendSnapshot);
  connect(diagramScene, &DiagramScene::waveformTraceSnapshots, waveformViewer,
          &WaveformViewer::appendSnapshots);
  connect(
      waveformViewer, &WaveformViewer::editModeChanged, this,
      [this](const bool enabled) { diagramScene->setIoInteractionsEnabled(!enabled); });
  connect(waveformViewer, &WaveformViewer::editTraceCommitted, diagramScene,
          &DiagramScene::simulateEditedWaveform);
}

void LogiFlowWindow::updateSubcircuitShapeAction()
{
  if (!editSubcircuitShapeAct)
    return;
  const bool active = silicon::project::classifyDocumentPath(activeDocumentPath)
                      == silicon::project::DocumentKind::Subcircuit;
  editSubcircuitShapeAct->setVisible(active);
  editSubcircuitShapeAct->setEnabled(active);
}

void LogiFlowWindow::editActiveSubcircuitShape()
{
  const auto slug = activeProjectSubcircuitSlug();
  if (slug.empty())
    return;
  try {
    saveActiveDocumentPayload();
    editGraphicalSubcircuitShape(slug, undoStack, this);
  } catch (const std::exception& e) {
    SiliconInputDialog::warning(
        this, tr("Edit shape"),
        tr("Failed to save the active subcircuit before editing its shape:\n%1")
            .arg(e.what()));
  }
}

void LogiFlowWindow::initializeProjectTree()
{
  projectTree = new QTreeWidget(componentsDock);
  projectTree->setHeaderHidden(true);
  projectTree->setRootIsDecorated(true);
  projectTree->setSelectionMode(QAbstractItemView::SingleSelection);
  projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
  componentsDock->setWidget(projectTree);

  connect(projectTree, &QTreeWidget::itemSelectionChanged, this,
          &LogiFlowWindow::projectTreeSelectionChanged);
  connect(projectTree, &QTreeWidget::customContextMenuRequested, this,
          &LogiFlowWindow::showProjectTreeContextMenu);
}

std::string LogiFlowWindow::activeProjectCircuitPath() const
{
  if (!activeDocumentPath.empty()
      && silicon::project::classifyDocumentPath(activeDocumentPath)
             == silicon::project::DocumentKind::Circuit)
    return activeDocumentPath;

  return projectMainCircuitPath(currentProjectInfo);
}

bool LogiFlowWindow::activateProjectCircuit(const std::string& circuitPath)
{ return activateProjectDocument(circuitPath); }

std::string LogiFlowWindow::activeProjectSubcircuitSlug() const
{
  return silicon::project::subcircuitSlugForPath(activeDocumentPath)
      .value_or(std::string{});
}

bool LogiFlowWindow::activateProjectDocument(const std::string& documentPath)
{
  if (!silicon::project::classifyDocumentPath(documentPath)
      || !silicon::project::DocumentStore::active().contains(documentPath))
    return false;
  if (documentPath == activeDocumentPath) {
    selectProjectTreeDocument(documentPath);
    return true;
  }

  return switchToDocument(documentPath, true);
}

std::shared_ptr<Circuit> LogiFlowWindow::activeCircuit()
{
  return diagramScene->getCircuit();
}

bool LogiFlowWindow::hasDocument(const std::string& path) const
{ return silicon::project::DocumentStore::active().contains(path); }

std::string LogiFlowWindow::emptyCircuitSceneJson(const std::string& name) const
{
  const auto circuitName = name.empty() ? std::string("Circuit") : name;
  nlohmann::ordered_json scene;
  scene["circuit"] =
      nlohmann::ordered_json{{"version", SILICON_VERSION},
                             {"name", circuitName},
                             {"description", ""},
                             {"components", nlohmann::ordered_json::array()}};
  scene["visual"]["components"] = nlohmann::ordered_json::array();
  scene["visual"]["wires"]      = nlohmann::ordered_json::array();
  return scene.dump(2);
}

std::string LogiFlowWindow::emptySubcircuitSceneJson(const std::string& name) const
{
  auto scene = nlohmann::ordered_json::parse(emptyCircuitSceneJson(name));
  scene["graphicalComponent"] =
      nlohmann::ordered_json{{"shape",
                              {{"type", "rectangle"},
                               {"width", GraphicalSubcircuitDefaultSize},
                               {"height", GraphicalSubcircuitDefaultSize}}},
                             {"inputs", nlohmann::ordered_json::array()},
                             {"outputs", nlohmann::ordered_json::array()}};
  return scene.dump(2);
}

std::string LogiFlowWindow::uniqueDocumentPath(
    const silicon::project::DocumentKind kind, const QString& requestedName) const
{
  const auto trimmed = requestedName.trimmed();
  const auto fallback =
      kind == silicon::project::DocumentKind::Circuit ? "circuit" : "subcircuit";
  const auto directory =
      kind == silicon::project::DocumentKind::Circuit ? "circuits" : "subcircuits";
  std::string slug = trimmed.isEmpty() ? fallback : trimmed.toStdString();

  for (char& ch : slug) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte)) {
      ch = static_cast<char>(std::tolower(byte));
    } else if (ch != '-' && ch != '_') {
      ch = '_';
    }
  }

  if (const auto first = slug.find_first_not_of('_'); first == std::string::npos) {
    slug = fallback;
  } else {
    const auto last = slug.find_last_not_of('_');
    slug            = slug.substr(first, last - first + 1);
  }

  auto candidate = std::format("{}/{}.json", directory, slug);
  int  suffix    = 2;
  while (hasDocument(candidate)) {
    candidate = std::format("{}/{}-{}.json", directory, slug, suffix);
    ++suffix;
  }

  return candidate;
}

void LogiFlowWindow::saveActiveDocumentPayload()
{
  if (activeDocumentPath.empty())
    activeDocumentPath = projectMainCircuitPath(currentProjectInfo);
  auto& store = silicon::project::DocumentStore::active();
  auto serializedScene = diagramScene->serialize();
  const auto kind = silicon::project::classifyDocumentPath(activeDocumentPath);
  if (!kind)
    throw std::runtime_error("Active project document path is invalid");

  if (*kind == silicon::project::DocumentKind::Subcircuit) {
    if (const auto* existing = store.find(activeDocumentPath)) {
      try {
        auto newJson = nlohmann::json::parse(serializedScene);
        const auto fallback =
            parseGraphicalSubcircuitMetadata(existing->sceneJson())
                .value_or(GraphicalSubcircuitMetadata{});
        newJson["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
            synchronizeGraphicalSubcircuitMetadata(serializedScene, fallback));
        serializedScene = newJson.dump(2);
      } catch (const nlohmann::json::exception&) {
      }
    }
  }

  dependencyGraph.replaceDocumentDependencies(activeDocumentPath, serializedScene);
  if (*kind == silicon::project::DocumentKind::Subcircuit)
    store.upsertDocument(
        preparedSubcircuitDocument(activeDocumentPath, std::move(serializedScene)));
  else
    store.upsertDocument({activeDocumentPath, std::move(serializedScene)});
}

void LogiFlowWindow::selectProjectTreeDocument(const std::string& path)
{
  if (!projectTree || projectTree->topLevelItemCount() == 0)
    return;

  const QSignalBlocker blocker(projectTree);
  projectTree->clearSelection();

  const auto kind = silicon::project::classifyDocumentPath(path);
  if (!kind)
    return;
  auto* section = projectSectionItem(
      projectTree, *kind == silicon::project::DocumentKind::Circuit
                       ? ProjectTreeItemKind::CircuitSection
                       : ProjectTreeItemKind::SubcircuitSection);
  if (!section)
    return;

  const QString targetPath = QString::fromStdString(path);
  for (int i = 0; i < section->childCount(); ++i) {
    auto* item = section->child(i);
    if (item->data(0, ProjectTreePathRole).toString() == targetPath) {
      item->setSelected(true);
      projectTree->setCurrentItem(item);
      return;
    }
  }
}

bool LogiFlowWindow::switchToDocument(const std::string& path,
                                      const bool selectInTree)
{
  auto& store = silicon::project::DocumentStore::active();
  const auto kind = silicon::project::classifyDocumentPath(path);
  if (path.empty() || !kind || !store.contains(path))
    return false;

  if (path == activeDocumentPath) {
    if (selectInTree)
      selectProjectTreeDocument(path);
    updateSubcircuitShapeAction();
    updatePropertyDock();
    return true;
  }

  if (componentCatalogOverlay)
    componentCatalogOverlay->hide();

  if (diagramScene->getInteractionMode() != InteractionMode::NORMAL_MODE)
    diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    const auto noun = *kind == silicon::project::DocumentKind::Circuit
                          ? tr("Circuit")
                          : tr("Subcircuit");
    SiliconInputDialog::warning(this, tr("%1 Switch Error").arg(noun),
                         tr("Failed to save the current document before switching:\n%1")
                             .arg(e.what()));
    return false;
  }

  const auto* target = store.find(path);
  if (!target)
    return false;
  const auto payload = target->sceneJson();

  try {
    diagramScene->clear(false, false);
    diagramScene->setSubcircuitDocumentMode(
        *kind == silicon::project::DocumentKind::Subcircuit);

    auto& guiFactory   = GUIComponentFactory::instance();
    auto& coreRegistry = ComponentRegistry::instance();
    diagramScene->deserialize(payload, guiFactory, coreRegistry);
    activeDocumentPath = path;
    updateSubcircuitShapeAction();
  } catch (const std::exception& e) {
    const auto noun = *kind == silicon::project::DocumentKind::Circuit
                          ? tr("Circuit")
                          : tr("Subcircuit");
    SiliconInputDialog::critical(this, tr("%1 Switch Error").arg(noun),
                          tr("Failed to load the selected %1:\n%2")
                              .arg(noun.toLower(), e.what()));
    return false;
  }

  if (selectInTree)
    selectProjectTreeDocument(path);

  setActionsEnabled({rotateAct, cutAct, copyAct, deleteAct}, false);
  updatePropertyDock();
  return true;
}

void LogiFlowWindow::removeDocument(const std::string& path)
{
  auto& store = silicon::project::DocumentStore::active();
  if (!store.contains(path))
    return;

  if (activeDocumentPath == path)
    switchToDocument(projectMainCircuitPath(currentProjectInfo), true);

  store.removeDocument(path);
  dependencyGraph.removeDocument(path);
  rebuildProjectTree();
  selectProjectTreeDocument(activeProjectCircuitPath());
  updatePropertyDock();
}

void LogiFlowWindow::insertDocument(
    silicon::project::Document document,
    const std::optional<std::ptrdiff_t> insertAt, const bool activate)
{
  auto& store = silicon::project::DocumentStore::active();
  const auto path = document.path();
  if (store.contains(path))
    return;

  dependencyGraph.addDocument(path);
  try {
    dependencyGraph.replaceDocumentDependencies(path, document.sceneJson());
  } catch (...) {
    dependencyGraph.removeDocument(path);
    throw;
  }

  if (insertAt)
    store.insertDocument(std::move(document),
                         static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, *insertAt)));
  else
    store.upsertDocument(std::move(document));

  rebuildProjectTree();
  if (activate)
    switchToDocument(path, true);
}

void LogiFlowWindow::rebuildProjectTree()
{
  if (!projectTree)
    return;

  const QSignalBlocker blocker(projectTree);
  projectTree->clear();

  const auto project =
      currentProjectInfo.value_or(defaultProjectInfo(currentFileName));

  auto* projectItem = new QTreeWidgetItem(projectTree);
  projectItem->setText(0, QString::fromStdString(project.name));
  projectItem->setData(0, ProjectTreeItemKindRole,
                       static_cast<int>(ProjectTreeItemKind::Project));
  projectItem->setExpanded(true);

  ensureProjectDocuments();

  auto addSection = [&](const silicon::project::DocumentKind kind) {
    const bool circuits = kind == silicon::project::DocumentKind::Circuit;
    auto* section = new QTreeWidgetItem(projectItem);
    section->setText(0, circuits ? tr("Circuits") : tr("Subcircuits"));
    section->setData(
        0, ProjectTreeItemKindRole,
        static_cast<int>(circuits ? ProjectTreeItemKind::CircuitSection
                                  : ProjectTreeItemKind::SubcircuitSection));
    section->setExpanded(true);
    for (const auto& document :
         silicon::project::DocumentStore::active().documents(kind)) {
      auto* item = new QTreeWidgetItem(section);
      item->setText(0, circuits
                           ? documentDisplayName(document)
                           : QString::fromStdString(
                                 document.subcircuitSlug().value_or(document.path())));
      item->setIcon(0, Icon("circuit-board"));
      item->setData(0, ProjectTreeItemKindRole,
                    static_cast<int>(circuits ? ProjectTreeItemKind::Circuit
                                              : ProjectTreeItemKind::Subcircuit));
      item->setData(0, ProjectTreePathRole,
                    QString::fromStdString(document.path()));
    }
  };
  addSection(silicon::project::DocumentKind::Circuit);
  addSection(silicon::project::DocumentKind::Subcircuit);

  projectTree->expandAll();
  if (!activeDocumentPath.empty())
    selectProjectTreeDocument(activeDocumentPath);
}

void LogiFlowWindow::updateProjectTreeLabels()
{
  if (!projectTree || projectTree->topLevelItemCount() == 0)
    return;

  const QSignalBlocker blocker(projectTree);
  auto*                projectItem = projectTree->topLevelItem(0);

  if (currentProjectInfo)
    projectItem->setText(0, QString::fromStdString(currentProjectInfo->name));

  for (const auto kind : {silicon::project::DocumentKind::Circuit,
                          silicon::project::DocumentKind::Subcircuit}) {
    auto* section = projectDocumentSectionItem(kind);
    const auto documents =
        silicon::project::DocumentStore::active().documents(kind);
    if (!section)
      continue;
    for (int i = 0;
         i < section->childCount() && i < static_cast<int>(documents.size()); ++i) {
      section->child(i)->setText(
          0, kind == silicon::project::DocumentKind::Circuit
                 ? documentDisplayName(documents[i])
                 : QString::fromStdString(
                       documents[i].subcircuitSlug().value_or(documents[i].path())));
    }
  }
}

QTreeWidgetItem* LogiFlowWindow::projectDocumentSectionItem(
    const silicon::project::DocumentKind kind) const
{
  return projectSectionItem(
      projectTree, kind == silicon::project::DocumentKind::Circuit
                       ? ProjectTreeItemKind::CircuitSection
                       : ProjectTreeItemKind::SubcircuitSection);
}

void LogiFlowWindow::clearProjectTreeSelection()
{
  if (!projectTree)
    return;

  const QSignalBlocker blocker(projectTree);
  projectTree->clearSelection();
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

void LogiFlowWindow::updateComponentCatalogGeometry()
{
  if (!componentCatalogOverlay || !diagramView)
    return;

  componentCatalogOverlay->setGeometry(diagramView->viewport()->rect());
}

/* ACTIONS IMPLEMENTATION */

void LogiFlowWindow::newFile()
{
  setFileName("");
  resetProjectState();
  rebuildProjectTree();
  updatePropertyDock();
}

void LogiFlowWindow::resetProjectState()
{
  currentProjectMetadata.reset();
  currentProjectInfo = defaultProjectInfo(currentFileName);
  activeDocumentPath = defaultMainCircuitPath();
  diagramScene->clear();
  diagramScene->setCircuit(std::make_shared<Circuit>());
  diagramScene->setSubcircuitDocumentMode(false);
  auto document = defaultCircuitDocument();
  document.setSceneJson(diagramScene->serialize());
  silicon::project::DocumentStore::active().setDocuments({std::move(document)});
  dependencyGraph.rebuildFromProject(
      silicon::project::DocumentStore::active().documents());
  updateSubcircuitShapeAction();
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
    auto&      guiFactory   = GUIComponentFactory::instance();
    auto&      coreRegistry = ComponentRegistry::instance();
    const auto payload      = nlohmann::json::from_bson(
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));

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

void LogiFlowWindow::loadCircuitContent(const QString&    fileName,
                                        const QByteArray& fileContent)
{
  uiLog.info(std::format("Opening {}", fileName.toStdString()));

  // 2. Read, validate, deserialize, and update the scene safely
  try {
    QTemporaryFile archive;
    if (!archive.open()
        || archive.write(fileContent) != static_cast<qint64>(fileContent.size())
        || !archive.flush()) {
      throw std::runtime_error("Cannot stage the selected project archive");
    }
    const QString archivePath = archive.fileName();
    archive.close();

    auto projectFile =
        silicon::project::readProjectFile(archivePath.toStdString());

    // Clear the current scene items to prepare for the new circuit.
    diagramScene->clear();

    // 3. Update application state on success
    currentProjectMetadata = std::move(projectFile.metadata);
    currentProjectInfo     = std::move(projectFile.project);
    activeDocumentPath     = projectMainCircuitPath(currentProjectInfo);
    std::vector<silicon::project::Document> documents;
    documents.reserve(projectFile.documents.size());
    for (auto& document : projectFile.documents) {
      if (document.kind() == silicon::project::DocumentKind::Subcircuit)
        documents.push_back(
            preparedSubcircuitDocument(document.path(), document.sceneJson()));
      else
        documents.push_back(std::move(document));
    }
    silicon::project::DocumentStore::active().setDocuments(std::move(documents));
    dependencyGraph.rebuildFromProject(
        silicon::project::DocumentStore::active().documents());

    auto& guiFactory   = GUIComponentFactory::instance();
    auto& coreRegistry = ComponentRegistry::instance();
    const auto* document =
        silicon::project::DocumentStore::active().find(activeDocumentPath);
    if (!document)
      throw std::runtime_error("Main circuit payload is missing");
    diagramScene->deserialize(document->sceneJson(), guiFactory, coreRegistry);
    diagramScene->setSubcircuitDocumentMode(false);
    updateSubcircuitShapeAction();

    setFileName(fileName);
    rebuildProjectTree();
    updatePropertyDock();

  } catch (const nlohmann::json::exception& e) {
    SiliconInputDialog::critical(
        this, tr("Corrupted File"),
        tr("The circuit file contains invalid JSON data:\n%1").arg(e.what()));
  } catch (const std::exception& e) {
    SiliconInputDialog::critical(this, tr("Load Error"),
                          tr("Failed to load the circuit:\n%1").arg(e.what()));
  }
}

void LogiFlowWindow::open()
{
  SiliconFileDialog::openFileContent(
      this, tr("Open Circuit"), tr("Silicon Circuit (*.sil);;All Files (*)"),
      [this](const QString& fileName, const QByteArray& fileContent) {
        loadCircuitContent(fileName, fileContent);
      });
}

void LogiFlowWindow::save()
{
  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SiliconInputDialog::critical(this, tr("Save Error"),
                          tr("Failed to serialize the active circuit:\n%1").arg(e.what()));
    return;
  }

  QString destinationFileName = currentFileName;
#ifndef __EMSCRIPTEN__
  if (destinationFileName.isEmpty()) {
    destinationFileName =
        QFileDialog::getSaveFileName(this, tr("Save Circuit"), QString(),
                                     tr("Silicon Circuit (*.sil);;All Files (*)"));
    if (destinationFileName.isEmpty())
      return;
  }
#else
  if (destinationFileName.isEmpty())
    destinationFileName = QStringLiteral("circuit.sil");
#endif

  try {
    auto metadata = currentProjectMetadata.value_or(silicon::project::metadataForNewFile());
    metadata.formatVersion  = silicon::project::ProjectFormatVersion;
    metadata.siliconVersion = SILICON_VERSION;
    metadata.lastModify     = silicon::project::currentUtcTimestamp();

    auto project = currentProjectInfo.value_or(silicon::project::ProjectInfo{});
    if (project.name.empty())
      project.name = QFileInfo(destinationFileName).baseName().toStdString();
    if (project.mainCircuit.empty())
      project.mainCircuit = defaultMainCircuitPath();
    currentProjectInfo = project;
    ensureProjectDocuments();
    const auto documents = silicon::project::DocumentStore::active().documents();
    const auto* mainDocument =
        silicon::project::DocumentStore::active().find(project.mainCircuit);
    if (!mainDocument)
      throw std::runtime_error("Main circuit payload is missing");

    silicon::project::ProjectFile projectFile{.metadata = metadata,
                                              .project = project,
                                              .documents = documents,
                                              .mainCircuitJson =
                                                  mainDocument->sceneJson()};

#ifdef __EMSCRIPTEN__
    QTemporaryFile archive;
    if (!archive.open())
      throw std::runtime_error("Cannot create a temporary project archive");
    const QString archivePath = archive.fileName();
    archive.close();

    silicon::project::writeProjectFile(archivePath.toStdString(), projectFile);

    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::ReadOnly))
      throw std::runtime_error("Cannot read the temporary project archive");

    const auto savedFileName = SiliconFileDialog::saveFileContent(
        this, tr("Save Circuit"), destinationFileName,
        tr("Silicon Circuit (*.sil);;All Files (*)"), archiveFile.readAll());
    if (!savedFileName)
      return;
    setFileName(*savedFileName);
#else
    silicon::project::writeProjectFile(destinationFileName.toStdString(), projectFile);
    setFileName(destinationFileName);
#endif
    currentProjectMetadata = std::move(metadata);
    updateProjectTreeLabels();
  } catch (const std::exception& e) {
    SiliconInputDialog::critical(this, tr("Save Error"),
                          tr("Failed to save the circuit:\n%1").arg(e.what()));
  }
}

void LogiFlowWindow::about() const
{
  aboutDialog->show();
}

void LogiFlowWindow::openSettings()
{
  const auto     shortcuts = shortcutSettings();
  SettingsWindow settingsWindow("LogiFlow", shortcuts, this);
  settingsWindow.exec();
  syncWasmShortcutCapture(shortcuts);
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

void LogiFlowWindow::toggleFstTracing(bool enabled)
{
  if (!waveformWindow)
    return;

  waveformWindow->setVisible(enabled);
  if (enabled) {
    diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
    waveformWindow->raise();
    waveformWindow->activateWindow();
  }
}

void LogiFlowWindow::updateStatus() const
{
  statusBar()->showMessage(
      tr("Interaction Mode: %1").arg(interactionModeName(diagramScene->getInteractionMode())));
}

void LogiFlowWindow::selectionChanged()
{
  const auto interactionMode = diagramScene->getInteractionMode();
  const auto selected        = diagramScene->selectedItems();
  const bool hasSelection    = !selected.empty();

  // Enable rotation only when a single component is selected or when in component placing
  // mode
  rotateAct->setEnabled((interactionMode == InteractionMode::NORMAL_MODE
                         && selected.size() == 1)
                        || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  // Enable cut, copy and delete only when in normal mode and some items are selected
  const bool cutCopyDelete = interactionMode == InteractionMode::NORMAL_MODE && hasSelection;
  setActionsEnabled({cutAct, copyAct, deleteAct}, cutCopyDelete);

  if (hasSelection)
    clearProjectTreeSelection();

  updatePropertyDock();
}

void LogiFlowWindow::projectTreeSelectionChanged()
{
  auto* selectedProjectItem = selectedProjectTreeItem(projectTree);
  if (!selectedProjectItem) {
    updatePropertyDock();
    return;
  }

  const auto itemKind = projectTreeItemKind(selectedProjectItem);
  if (itemKind == ProjectTreeItemKind::Circuit
      || itemKind == ProjectTreeItemKind::Subcircuit) {
    const QSignalBlocker blocker(diagramScene);
    diagramScene->clearSelection();
    switchToDocument(projectTreeCircuitPath(selectedProjectItem), false);
    return;
  }

  const QSignalBlocker blocker(diagramScene);
  diagramScene->clearSelection();
  setActionsEnabled({rotateAct, cutAct, copyAct, deleteAct}, false);
  updatePropertyDock();
}

void LogiFlowWindow::showProjectTreeContextMenu(const QPoint& position)
{
  if (!projectTree)
    return;

  if (auto* item = projectTree->itemAt(position)) {
    projectTree->setCurrentItem(item);
    item->setSelected(true);
  }

#ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
#else
  QMenu stackMenu(this);
  auto* menu = &stackMenu;
#endif
  menu->addAction(Icon("plus"), tr("New Circuit"), this,
                  &LogiFlowWindow::createCircuit);
  menu->addAction(Icon("plus"), tr("New Subcircuit"), this,
                  &LogiFlowWindow::createSubcircuit);

  auto* selectedProjectItem = selectedProjectTreeItem(projectTree);
  if (selectedProjectItem) {
    const auto kind = projectTreeItemKind(selectedProjectItem);
    if (kind == ProjectTreeItemKind::Circuit
        || kind == ProjectTreeItemKind::Subcircuit) {
      const auto path = projectTreeCircuitPath(selectedProjectItem);
      const bool circuit = kind == ProjectTreeItemKind::Circuit;
      auto* deleteAction =
          menu->addAction(Icon("delete"),
                          circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
                          this, &LogiFlowWindow::deleteSelectedDocument);
      deleteAction->setEnabled(path != projectMainCircuitPath(currentProjectInfo));
    }
  }

#ifdef __EMSCRIPTEN__
  menu->popup(projectTree->viewport()->mapToGlobal(position));
#else
  menu->exec(projectTree->viewport()->mapToGlobal(position));
#endif
}

void LogiFlowWindow::createCircuit()
{ createDocument(silicon::project::DocumentKind::Circuit); }

void LogiFlowWindow::createSubcircuit()
{ createDocument(silicon::project::DocumentKind::Subcircuit); }

void LogiFlowWindow::createDocument(const silicon::project::DocumentKind kind)
{
  const bool circuit = kind == silicon::project::DocumentKind::Circuit;
  SiliconInputDialog::getText(
      this, circuit ? tr("New Circuit") : tr("New Subcircuit"),
      circuit ? tr("Circuit name") : tr("Subcircuit name"),
      circuit ? tr("Circuit") : tr("Subcircuit"),
      [this, kind](const QString& requestedName) {
        const bool circuit = kind == silicon::project::DocumentKind::Circuit;
        const QString trimmedName = requestedName.trimmed();
        const QString displayName =
            trimmedName.isEmpty()
                ? (circuit ? QStringLiteral("Circuit")
                           : QStringLiteral("Subcircuit"))
                : trimmedName;
        const auto displayNameString = displayName.toStdString();
        const auto path = uniqueDocumentPath(kind, displayName);
        const auto sceneJson =
            circuit ? emptyCircuitSceneJson(displayNameString)
                    : emptySubcircuitSceneJson(displayNameString);
        silicon::project::Document document =
            circuit ? silicon::project::Document(path, sceneJson)
                    : preparedSubcircuitDocument(path, sceneJson);

        auto addDocument = [this, document] {
          try {
            saveActiveDocumentPayload();
          } catch (const std::exception&) {
          }
          insertDocument(document, std::nullopt, true);
        };

        auto removeCreated = [this, path] { removeDocument(path); };

        undoStack->push(new ProjectStateCommand(
            circuit ? tr("Create Circuit") : tr("Create Subcircuit"),
            removeCreated, addDocument));
      });
}

void LogiFlowWindow::deleteSelectedCircuit()
{ deleteSelectedDocument(); }

void LogiFlowWindow::deleteSelectedSubcircuit()
{ deleteSelectedDocument(); }

void LogiFlowWindow::deleteSelectedDocument()
{
  auto* selectedProjectItem = selectedProjectTreeItem(projectTree);
  if (!selectedProjectItem)
    return;

  const auto itemKind = projectTreeItemKind(selectedProjectItem);
  if (itemKind != ProjectTreeItemKind::Circuit
      && itemKind != ProjectTreeItemKind::Subcircuit)
    return;

  const auto path = projectTreeCircuitPath(selectedProjectItem);
  if (path == projectMainCircuitPath(currentProjectInfo))
    return;
  const bool circuit = itemKind == ProjectTreeItemKind::Circuit;

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SiliconInputDialog::warning(
        this, circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
        tr("Failed to save the active document before deleting it:\n%1")
            .arg(e.what()));
    return;
  }

  if (!circuit) {
    const auto dependents = dependencyGraph.dependentsOf(path);
    if (!dependents.empty()) {
      QStringList dependentNames;
      for (const auto& dependent : dependents)
        dependentNames.push_back(QString::fromStdString(dependent));
      SiliconInputDialog::warning(
          this, tr("Delete Subcircuit"),
          tr("This subcircuit is still used by:\n%1")
              .arg(dependentNames.join('\n')));
      return;
    }
  }

  SiliconInputDialog::question(
      this, circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
      (circuit ? tr("Delete circuit \"%1\"?")
               : tr("Delete subcircuit \"%1\"?"))
          .arg(selectedProjectItem->text(0)),
      [this, path, circuit] {
        auto& store = silicon::project::DocumentStore::active();
        const auto* storedDocument = store.find(path);
        const auto  storedIndex    = store.indexOf(path);
        if (!storedDocument || !storedIndex)
          return;

        const auto document = *storedDocument;
        const auto index    = static_cast<std::ptrdiff_t>(*storedIndex);

        auto removeStored = [this, path] { removeDocument(path); };
        auto restoreDocument = [this, document, index] {
          insertDocument(document, index, true);
        };

        undoStack->push(new ProjectStateCommand(
            circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
            restoreDocument, removeStored));
      });
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
    QTreeWidgetItem* selectedProjectItem = selectedProjectTreeItem(projectTree);

    if (!selectedProjectItem) {
      layout->addRow(new QLabel(
          tr("Select a project, circuit, or one or more components\nto view properties.")));
      return;
    }

    const auto itemKind = projectTreeItemKind(selectedProjectItem);

    if (itemKind == ProjectTreeItemKind::CircuitSection
        || itemKind == ProjectTreeItemKind::SubcircuitSection) {
      layout->addRow(new QLabel(
          itemKind == ProjectTreeItemKind::CircuitSection
              ? tr("Select a circuit to view its properties.")
              : tr("Select a subcircuit to view its properties.")));
      return;
    }

    auto* nameEdit        = new QLineEdit(container);
    auto* descriptionEdit = new MetadataDescriptionEdit(container);
    descriptionEdit->setMinimumHeight(90);

    auto pushMetadataEdit = [this](const QString& label, const std::string& oldValue,
                                   const std::string& newValue,
                                   MetadataEditCommand::ApplyFn apply) {
      if (oldValue == newValue)
        return;

      undoStack->push(
          new MetadataEditCommand(label, oldValue, newValue, std::move(apply)));
    };

    auto schedulePropertyDockRefresh = [this] {
      QTimer::singleShot(0, this, &LogiFlowWindow::updatePropertyDock);
    };

    if (itemKind == ProjectTreeItemKind::Project) {
      if (!currentProjectInfo)
        currentProjectInfo = defaultProjectInfo(currentFileName);

      nameEdit->setText(QString::fromStdString(currentProjectInfo->name));
      descriptionEdit->setPlainText(
          QString::fromStdString(currentProjectInfo->description));
      descriptionEdit->document()->setModified(false);

      connect(nameEdit, &QLineEdit::editingFinished, this,
              [this, nameEdit, pushMetadataEdit, schedulePropertyDockRefresh] {
        if (!currentProjectInfo || !nameEdit->isModified())
          return;

        const auto oldValue = currentProjectInfo->name;
        const auto newValue = nameEdit->text().toStdString();
        nameEdit->setModified(false);
        pushMetadataEdit(tr("Modify Project Name"), oldValue, newValue,
                         [this, schedulePropertyDockRefresh](const std::string& value) {
                           if (!currentProjectInfo)
                             return;
                           currentProjectInfo->name = value;
                           updateProjectTreeLabels();
                           schedulePropertyDockRefresh();
                         });
      });
      descriptionEdit->commit =
          [this, descriptionEdit, pushMetadataEdit, schedulePropertyDockRefresh] {
            if (!currentProjectInfo || !descriptionEdit->document()->isModified())
              return;

            const auto oldValue = currentProjectInfo->description;
            const auto newValue = descriptionEdit->toPlainText().toStdString();
            descriptionEdit->document()->setModified(false);
            pushMetadataEdit(
                tr("Modify Project Description"), oldValue, newValue,
                [this, schedulePropertyDockRefresh](const std::string& value) {
                  if (!currentProjectInfo)
                    return;
                  currentProjectInfo->description = value;
                  schedulePropertyDockRefresh();
                });
          };
    } else {
      const auto circuitPath = projectTreeCircuitPath(selectedProjectItem);
      std::string name;
      std::string description;
      if (activeProjectCircuitPath() == circuitPath) {
        const auto circuit = activeCircuit();
        if (circuit) {
          name        = circuit->getName();
          description = circuit->getDescription();
        }
      } else if (const auto* document =
                     silicon::project::DocumentStore::active().find(circuitPath)) {
        std::tie(name, description) = circuitMetadata(*document);
      }
      nameEdit->setText(QString::fromStdString(name));
      descriptionEdit->setPlainText(QString::fromStdString(description));
      descriptionEdit->document()->setModified(false);

      connect(nameEdit, &QLineEdit::editingFinished, this,
              [this, nameEdit, circuitPath, pushMetadataEdit,
               schedulePropertyDockRefresh] {
        if (!nameEdit->isModified())
          return;

        const auto oldValue =
            activeCircuit() ? activeCircuit()->getName() : std::string{};
        const auto newValue = nameEdit->text().toStdString();
        nameEdit->setModified(false);
        pushMetadataEdit(tr("Modify Circuit Name"), oldValue, newValue,
                         [this, circuitPath,
                          schedulePropertyDockRefresh](const std::string& value) {
                           if (!activateProjectCircuit(circuitPath))
                             return;
                           if (const auto circuit = activeCircuit()) {
                             circuit->setName(value);
                             saveActiveDocumentPayload();
                           }
                           updateProjectTreeLabels();
                           schedulePropertyDockRefresh();
                         });
      });
      descriptionEdit->commit =
          [this, circuitPath, descriptionEdit, pushMetadataEdit,
           schedulePropertyDockRefresh] {
            if (!descriptionEdit->document()->isModified())
              return;

            const auto oldValue =
                activeCircuit() ? activeCircuit()->getDescription() : std::string{};
            const auto newValue = descriptionEdit->toPlainText().toStdString();
            descriptionEdit->document()->setModified(false);
            pushMetadataEdit(
                tr("Modify Circuit Description"), oldValue, newValue,
                [this, circuitPath,
                 schedulePropertyDockRefresh](const std::string& value) {
                  if (!activateProjectCircuit(circuitPath))
                    return;
                  if (const auto circuit = activeCircuit()) {
                    circuit->setDescription(value);
                    saveActiveDocumentPayload();
                  }
                  schedulePropertyDockRefresh();
                });
          };
    }

    layout->addRow(tr("Name"), nameEdit);
    layout->addRow(tr("Description"), descriptionEdit);
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
      SiliconInputDialog::warning(this, tr("Invalid Property"), e.what());
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
        const auto stringOptions =
            selectedNodes.front()->getComponent()->getStringPropertyOptions(key);
        if (stringOptions) {
          auto* comboBox = new QComboBox(container);

          for (const std::string& option : stringOptions->get()) {
            comboBox->addItem(QString::fromStdString(option));
          }

          if (isMixed) {
            comboBox->setPlaceholderText(tr("Mixed values"));
            comboBox->setCurrentIndex(-1);
          } else {
            comboBox->setCurrentText(QString::fromStdString(arg));
          }

          connect(comboBox, &QComboBox::currentTextChanged, this,
                  [=](const QString& text) {
                    if (text.isEmpty())
                      return;
                    applyProperty(key, text.toStdString());
                  });

          layout->addRow(QString::fromStdString(key), comboBox);
          return;
        }

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
  m_isMixed = mixed;

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
