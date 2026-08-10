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
#include <ui/logiFlow/metadataDescriptionEdit.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

const SILICON::logging::Logger uiLog("ui");

QAction* makeAction(QObject* parent, const QIcon& icon, const QString& text,
                    const QString& statusTip = {})
{
  auto* action = new QAction(icon, text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

QAction* makeAction(QObject* parent, const QString& text, const QString& statusTip = {})
{
  auto* action = new QAction(text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

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

}  // namespace

void LogiFlowWindow::setActionsEnabled(std::initializer_list<QAction*> actions,
                                       const bool enabled)
{
  for (QAction* action : actions) {
    if (action)
      action->setEnabled(enabled);
  }
}

void LogiFlowWindow::syncWasmShortcutCapture()
{
#ifdef __EMSCRIPTEN__
  const QVector<ShortcutSetting> shortcuts = shortcutSettings();
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

void LogiFlowWindow::createActions()
{
  newAct  = makeAction(this, Icon("file"), tr("&New"), tr("Create a new file"));
  openAct = makeAction(this, Icon("open"), tr("&Open..."),
                       tr("Open an existing logiFlow file"));
  saveAct = makeAction(this, Icon("save"), tr("&Save"), tr("Save the circuit to disk"));
  exportImageAct = makeAction(this, Icon("export"), tr("&Export..."),
                              tr("Export the circuit as an image"));
  exitAct   = makeAction(this, Icon("xmark"), tr("E&xit"), tr("Exit the application"));
  cutAct    = makeAction(this, Icon("cut"), tr("Cu&t"),
                         tr("Cut the current selection's contents to the clipboard"));
  copyAct   = makeAction(this, Icon("copy"), tr("&Copy"));
  pasteAct  = makeAction(this, Icon("paste"), tr("&Paste"),
                         tr("Paste the clipboard's contents into the current selection"));
  rotateAct = makeAction(this, Icon("rotate"), tr("&Rotate"));
  autoPlaceAct   = makeAction(this, Icon("rearrange"), tr("&Auto place"),
                              tr("Automatically place components and reroute wires"));
  deleteAct =
      makeAction(this, Icon("delete"), tr("&Delete"), tr("Delete selected components"));
  aboutAct    = makeAction(this, Icon("info"), tr("&About"),
                           tr("Show the application's about box"));
  settingsAct = makeAction(this, Icon("settings"), tr("&Settings..."),
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
  toggleFstTraceAct =
      makeAction(this, Icon("chart"), tr("Trace"), tr("Show waveform viewer"));
  toggleFstTraceAct->setCheckable(true);
  cancelInteractionAct =
      makeAction(this, QString(), tr("Cancel the current interaction"));

  openComponentCatalogAct =
      makeAction(this, Icon("plus"), "", tr("Open the component catalog"));
  editSubcircuitShapeAct = makeAction(this, Icon("circuit-board"), tr("Edit Shape"),
                                      tr("Edit the active subcircuit shape"));
  editSubcircuitShapeAct->setVisible(false);
  editSubcircuitShapeAct->setEnabled(false);
  toggleHdlCodeModeAct = makeAction(this, Icon("code"), tr("Code"),
                                    tr("Edit or compile the active subcircuit HDL"));
  toggleHdlCodeModeAct->setCheckable(true);
  toggleHdlCodeModeAct->setVisible(false);
  toggleHdlCodeModeAct->setEnabled(false);
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
  connect(autoPlaceAct, &QAction::triggered, this, &LogiFlowWindow::autoPlace);
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
  connect(toggleHdlCodeModeAct, &QAction::toggled, this,
          &LogiFlowWindow::toggleHdlCodeMode);
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
      shortcut(QStringLiteral("keybindings/autoPlace"), tr("Auto place"), autoPlaceAct,
               QKeySequence(Qt::AltModifier | Qt::Key_L)),
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

  SILICON::simulation::Simulator::setMaxSimulationSteps(
      static_cast<uint64_t>(values.maxSimulationSteps));
  SILICON::simulation::Simulator::setMaxTransitionsPerDeltaCycle(
      values.maxTransitionsPerDeltaCycle);

  ThemeEngine::apply(*qApp, themeModeFromText(values.theme));

  const auto shortcuts = shortcutSettings();
  for (const ShortcutSetting& shortcut : shortcuts) {
    shortcut.action->setShortcut(
        SILICON::ui::settings::value(settings, shortcut.setting).value<QKeySequence>());
#ifdef __EMSCRIPTEN__
    shortcut.action->setShortcutContext(Qt::ApplicationShortcut);
    if (!actions().contains(shortcut.action))
      addAction(shortcut.action);
#endif
  }

  syncWasmShortcutCapture();
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
  editMenu->addAction(autoPlaceAct);
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
  toolBar->addAction(toggleHdlCodeModeAct);

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

  waveformViewer = new waveform::Viewer(waveformWindow);
  layout->addWidget(waveformViewer);

  connect(waveformWindow, &QDialog::finished, this, [this] {
    const QSignalBlocker blocker(toggleFstTraceAct);
    toggleFstTraceAct->setChecked(false);
    waveformViewer->setEditMode(false);
  });
  connect(diagramScene, &DiagramScene::waveformTraceReset, waveformViewer,
          &waveform::Viewer::resetTrace);
  connect(diagramScene, &DiagramScene::waveformTraceSnapshot, waveformViewer,
          &waveform::Viewer::appendSnapshot);
  connect(diagramScene, &DiagramScene::waveformTraceSnapshots, waveformViewer,
          &waveform::Viewer::appendSnapshots);
  connect(
      waveformViewer, &waveform::Viewer::editModeChanged, this,
      [this](const bool enabled) { diagramScene->setIoInteractionsEnabled(!enabled); });
  connect(waveformViewer, &waveform::Viewer::editTraceCommitted, diagramScene,
          &DiagramScene::simulateEditedWaveform);
}

void LogiFlowWindow::updateSubcircuitShapeAction()
{
  if (!editSubcircuitShapeAct)
    return;
  const bool active = SILICON::project::classifyDocumentPath(activeDocumentPath)
                      == SILICON::project::DocumentKind::Subcircuit;
  editSubcircuitShapeAct->setVisible(active);
  editSubcircuitShapeAct->setEnabled(active);
  updateHdlActions();
}

SILICON::project::ProjectAsset* LogiFlowWindow::projectAsset(const std::string_view path)
{
  const auto it =
      std::ranges::find(projectAssets, path, &SILICON::project::ProjectAsset::path);
  return it == projectAssets.end() ? nullptr : &*it;
}

const SILICON::project::ProjectAsset*
LogiFlowWindow::projectAsset(const std::string_view path) const
{
  const auto it =
      std::ranges::find(projectAssets, path, &SILICON::project::ProjectAsset::path);
  return it == projectAssets.end() ? nullptr : &*it;
}

bool LogiFlowWindow::activeDocumentHasHdl() const
{
  const auto* document =
      SILICON::project::DocumentStore::active().find(activeDocumentPath);
  return document && document->kind() == SILICON::project::DocumentKind::Subcircuit
         && SILICON::project::parseHdlDescriptor(document->getSceneJson()).has_value();
}

void LogiFlowWindow::updateHdlActions()
{
  if (!toggleHdlCodeModeAct)
    return;

  const bool subcircuit = SILICON::project::classifyDocumentPath(activeDocumentPath)
                          == SILICON::project::DocumentKind::Subcircuit;
  const bool hdl = subcircuit && activeDocumentHasHdl();

  toggleHdlCodeModeAct->setVisible(subcircuit);
#ifdef __EMSCRIPTEN__
  toggleHdlCodeModeAct->setEnabled(false);
  toggleHdlCodeModeAct->setToolTip(
      tr("HDL editing requires Yosys and is unavailable in the web build"));
#else
  toggleHdlCodeModeAct->setEnabled(subcircuit);
  toggleHdlCodeModeAct->setToolTip(
      hdl ? tr("Toggle editable HDL source and compiled mode")
          : tr("Permanently convert this subcircuit to HDL"));
#endif

  {
    const QSignalBlocker blocker(toggleHdlCodeModeAct);
    toggleHdlCodeModeAct->setChecked(hdl && hdlCodeMode);
  }

  const bool editingHdl = hdl && hdlCodeMode;
  setSimulationModeAct->setEnabled(!editingHdl);
  toggleFstTraceAct->setEnabled(!editingHdl);
  if (editingHdl && toggleFstTraceAct->isChecked())
    toggleFstTraceAct->setChecked(false);

  setActionsEnabled({setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                     openComponentCatalogAct, setComponentPlacingModeAct},
                    !hdl);
}

void LogiFlowWindow::showActiveHdlDocument()
{
  const auto* document =
      SILICON::project::DocumentStore::active().find(activeDocumentPath);
  if (!document)
    throw std::runtime_error("Active subcircuit document is missing");
  const auto descriptor = SILICON::project::parseHdlDescriptor(document->getSceneJson());
  if (!descriptor)
    throw std::runtime_error("Active subcircuit has no HDL descriptor");
  const auto* asset = projectAsset(descriptor->path);
  if (!asset)
    throw std::runtime_error(
        std::format("Subcircuit HDL asset '{}' is missing", descriptor->path));

  const auto coreJson = document->getCoreCircuitJson().value_or(
      SILICON::core::extractCoreCircuitJson(document->getSceneJson()));
  diagramScene->setCircuit(std::make_shared<Circuit>(
      Circuit::deserialize(coreJson, ComponentRegistry::instance())));
  hdlEditor->setPlainText(QString::fromStdString(asset->contents));
  hdlEditor->document()->setModified(false);
  hdlEditor->setReadOnly(!hdlCodeMode);
  editorStack->setCurrentWidget(hdlEditor);
  updateHdlActions();
}

void LogiFlowWindow::compileActiveHdl()
{
  const auto* existing =
      SILICON::project::DocumentStore::active().find(activeDocumentPath);
  if (!existing)
    throw std::runtime_error("Active subcircuit document is missing");
  const auto descriptor = SILICON::project::parseHdlDescriptor(existing->getSceneJson());
  const auto slug       = existing->subcircuitSlug();
  if (!descriptor || !slug)
    throw std::runtime_error("Active document is not an HDL-backed subcircuit");

  auto circuit =
      SILICON::yosys::importVerilog(hdlEditor->toPlainText().toStdString(), *slug);
  auto       json     = nlohmann::ordered_json::parse(existing->getSceneJson());
  const auto fallback = parseGraphicalSubcircuitMetadata(existing->getSceneJson())
                            .value_or(GraphicalSubcircuitMetadata{});
  json["circuit"]              = nlohmann::json::parse(circuit.serialize());
  json["visual"]["components"] = nlohmann::ordered_json::array();
  json["visual"]["wires"]      = nlohmann::ordered_json::array();

  auto sceneJson             = json.dump(2);
  json["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
      synchronizeGraphicalSubcircuitMetadata(sceneJson, fallback));
  sceneJson = json.dump(2);

  auto* asset = projectAsset(descriptor->path);
  if (!asset)
    throw std::runtime_error(
        std::format("Subcircuit HDL asset '{}' is missing", descriptor->path));

  const auto source = hdlEditor->toPlainText().toStdString();
  dependencyGraph.replaceDocumentDependencies(activeDocumentPath, sceneJson);
  SILICON::project::DocumentStore::active().upsertDocument(
      preparedSubcircuitDocument(activeDocumentPath, std::move(sceneJson)));
  asset->contents = source;
  diagramScene->setCircuit(std::make_shared<Circuit>(std::move(circuit)));

  hdlEditor->document()->setModified(false);
  hdlEditor->setReadOnly(true);
  hdlCodeMode = false;
  updateHdlActions();
  updatePropertyDock();
}

void LogiFlowWindow::convertActiveSubcircuitToHdl()
{
  const auto slug = activeProjectSubcircuitSlug();
  if (slug.empty())
    throw std::runtime_error("Only subcircuits can be converted to HDL");

  saveActiveDocumentPayload();
  const auto* existing =
      SILICON::project::DocumentStore::active().find(activeDocumentPath);
  if (!existing)
    throw std::runtime_error("Active subcircuit document is missing");

  auto circuit = Circuit::deserialize(
      SILICON::core::extractCoreCircuitJson(existing->getSceneJson()),
      ComponentRegistry::instance());
  circuit.setName(slug);
  const auto source = SILICON::yosys::exportVerilog(circuit);
  // Re-import before committing the irreversible conversion. This also validates
  // that the slug is a supported top-module identifier.
  (void)SILICON::yosys::importVerilog(source, slug);

  const auto assetPath = std::format("hdl/{}.v", slug);
  if (projectAsset(assetPath))
    throw std::runtime_error(std::format("Project asset '{}' already exists", assetPath));

  auto json   = nlohmann::ordered_json::parse(existing->getSceneJson());
  json["hdl"] = nlohmann::ordered_json{{"type", "verilog"}, {"path", assetPath}};
  json["visual"]["components"] = nlohmann::ordered_json::array();
  json["visual"]["wires"]      = nlohmann::ordered_json::array();
  const auto sceneJson         = json.dump(2);

  dependencyGraph.replaceDocumentDependencies(activeDocumentPath, sceneJson);
  SILICON::project::DocumentStore::active().upsertDocument(
      preparedSubcircuitDocument(activeDocumentPath, sceneJson));
  projectAssets.push_back({assetPath, source});

  hdlCodeMode = true;
  diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);
  hdlEditor->setPlainText(QString::fromStdString(source));
  hdlEditor->document()->setModified(false);
  hdlEditor->setReadOnly(false);
  editorStack->setCurrentWidget(hdlEditor);
  undoStack->clear();
  updateHdlActions();
  updatePropertyDock();
}

void LogiFlowWindow::toggleHdlCodeMode(const bool enabled)
{
  try {
    if (!activeDocumentHasHdl()) {
      if (!enabled)
        return;
      convertActiveSubcircuitToHdl();
      return;
    }

    if (enabled) {
      hdlCodeMode = true;
      diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);
      hdlEditor->setReadOnly(false);
      hdlEditor->setFocus();
      updateHdlActions();
      return;
    }

    compileActiveHdl();
  } catch (const std::exception& error) {
    hdlCodeMode = activeDocumentHasHdl();
    if (hdlCodeMode) {
      hdlEditor->setReadOnly(false);
      editorStack->setCurrentWidget(hdlEditor);
    }
    updateHdlActions();
    SILICON::ui::inputDialog::critical(
        this, tr("HDL Error"),
        tr("Failed to generate the subcircuit core:\n%1").arg(error.what()));
  }
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
    SILICON::ui::inputDialog::warning(
        this, tr("Edit shape"),
        tr("Failed to save the active subcircuit before editing its shape:\n%1")
            .arg(e.what()));
  }
}

}  // namespace ui
}  // namespace SILICON
