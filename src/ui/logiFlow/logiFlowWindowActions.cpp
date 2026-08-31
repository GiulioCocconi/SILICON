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
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
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
#include <QPushButton>
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
#include <ui/common/binaryEditor.hpp>
#include <ui/common/codeEditor.hpp>
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
#include <ui/logiFlow/metadataDescriptionEdit.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

  const SILICON::logging::Logger uiLog("ui");

  class ConversionCommand : public QUndoCommand {
  public:
    using Fn = std::function<void()>;
    ConversionCommand(QString text, Fn undoFn, Fn redoFn)
      : QUndoCommand(std::move(text)),
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

  [[nodiscard]] std::optional<std::vector<std::string>>
  selectVerilogModules(QWidget* parent, const SILICON::yosys::ModuleDependencyGraph& graph)
  {
    auto modules = graph.modules();
    if (modules.size() <= 1)
      return modules;

    std::ranges::sort(modules, [&graph](const auto& lhs, const auto& rhs) {
      const auto lhsDependencies = graph.dependenciesOf(lhs).size();
      const auto rhsDependencies = graph.dependenciesOf(rhs).size();
      if (lhsDependencies != rhsDependencies)
        return lhsDependencies > rhsDependencies;
      return lhs < rhs;
    });

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Import Verilog Modules"));
    dialog.setModal(true);
    dialog.resize(560, 420);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QObject::tr("Select one or more modules. Their dependencies are imported "
                    "automatically."),
        &dialog));

    auto* tree = new QTreeWidget(&dialog);
    tree->setColumnCount(1);
    tree->setHeaderLabels({QObject::tr("Module")});
    tree->setRootIsDecorated(true);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    constexpr int explicitlySelectedRole = Qt::UserRole;

    const auto addDependencies = [&graph](this auto&& addDependencies,
                                          QTreeWidgetItem* parentItem,
                                          const std::string& module) -> void {
      for (const auto& dependency : graph.dependenciesOf(module)) {
        auto* dependencyItem = new QTreeWidgetItem(parentItem);
        dependencyItem->setText(0, QString::fromStdString(dependency));
        dependencyItem->setFlags(dependencyItem->flags() & ~Qt::ItemIsUserCheckable);
        addDependencies(dependencyItem, dependency);
      }
    };

    for (const auto& module : modules) {
      auto* item = new QTreeWidgetItem(tree);
      item->setText(0, QString::fromStdString(module));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      item->setData(0, explicitlySelectedRole, false);
      addDependencies(item, module);
    }
    tree->collapseAll();
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(tree);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* importButton = buttons->button(QDialogButtonBox::Ok);
    importButton->setText(QObject::tr("Import"));
    importButton->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(
        tree, &QTreeWidget::itemChanged, &dialog,
        [tree, importButton, &graph, explicitlySelectedRole](QTreeWidgetItem* changedItem,
                                                             int) {
          const QSignalBlocker blocker(tree);
          if (!changedItem->parent()
              && changedItem->flags().testFlag(Qt::ItemIsEnabled)) {
            changedItem->setData(0, explicitlySelectedRole,
                                 changedItem->checkState(0) == Qt::Checked);
          }

          std::vector<std::string> roots;
          for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            const auto* item = tree->topLevelItem(index);
            if (item->data(0, explicitlySelectedRole).toBool())
              roots.push_back(item->text(0).toStdString());
          }

          importButton->setEnabled(!roots.empty());
          std::vector<std::string> dependencies;
          for (const auto& root : roots) {
            for (const auto& included :
                 graph.dependencyOrder(std::vector<std::string>{root})) {
              if (included != root && !std::ranges::contains(dependencies, included)) {
                dependencies.push_back(included);
              }
            }
          }

          for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            auto*      item       = tree->topLevelItem(index);
            const auto name       = item->text(0).toStdString();
            const bool selected   = item->data(0, explicitlySelectedRole).toBool();
            const bool dependency = std::ranges::contains(dependencies, name);

            auto flags = item->flags() | Qt::ItemIsUserCheckable;
            flags.setFlag(Qt::ItemIsEnabled, !dependency);
            item->setFlags(flags);
            item->setCheckState(0, selected || dependency ? Qt::Checked : Qt::Unchecked);

            auto font = item->font(0);
            font.setBold(selected && !dependency);
            font.setItalic(dependency);
            item->setFont(0, font);
          }
        });

    if (dialog.exec() != QDialog::Accepted)
      return std::nullopt;

    std::vector<std::string> roots;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      const auto* item = tree->topLevelItem(index);
      if (item->data(0, explicitlySelectedRole).toBool())
        roots.push_back(item->text(0).toStdString());
    }
    return roots;
  }

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
                                       const bool                      enabled)
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
  QJsonArray                     shortcutSequences;
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
  newAct         = makeAction(this, Icon("file"), tr("&New"), tr("Create a new file"));
  newCodeFileAct = makeAction(this, Icon("code"), tr("New Code File..."),
                              tr("Create an empty source-code document"));
  newBinaryFileAct = makeAction(this, Icon("file"), tr("New Binary File..."),
                                tr("Create a fixed-size raw binary document"));
  openAct        = makeAction(this, Icon("open"), tr("&Open..."),
                              tr("Open an existing logiFlow file"));
  saveAct = makeAction(this, Icon("save"), tr("&Save"), tr("Save the circuit to disk"));
  exportImageAct = makeAction(this, Icon("export"), tr("&Export..."),
                              tr("Export the circuit as an image"));
  exitAct = makeAction(this, Icon("xmark"), tr("E&xit"), tr("Exit the application"));
  cutAct  = makeAction(this, Icon("cut"), tr("Cu&t"),
                       tr("Cut the current selection's contents to the clipboard"));
  copyAct = makeAction(this, Icon("copy"), tr("&Copy"));
  pasteAct       = makeAction(this, Icon("paste"), tr("&Paste"),
                              tr("Paste the clipboard's contents into the current selection"));
  rotateAct    = makeAction(this, Icon("rotate"), tr("&Rotate"));
  autoPlaceAct = makeAction(this, Icon("rearrange"), tr("&Auto place"),
                            tr("Automatically place components and reroute wires"));
  deleteAct =
      makeAction(this, Icon("delete"), tr("&Delete"), tr("Delete selected components"));
  aboutAct    = makeAction(this, Icon("info"), tr("&About"),
                           tr("Show the application's about box"));
  settingsAct = makeAction(this, Icon("settings"), tr("&Settings..."),
                           tr("Edit application settings"));

  undoAct = makeAction(this, Icon("undo"), tr("&Undo"), tr("Undo the last operation"));
  undoAct->setIcon(Icon("undo"));
  undoAct->setStatusTip(tr("Undo the last operation"));

  redoAct = makeAction(this, Icon("redo"), tr("&Redo"), tr("Redo the last operation"));
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
  codeConversionAct = makeAction(this, Icon("code"), tr("Code"));
  codeConversionAct->setVisible(false);
  codeConversionAct->setEnabled(false);
  setComponentPlacingModeAct =
      makeAction(this, Icon("plus"), "", tr("Open quick component search"));

  connect(newAct, &QAction::triggered, this, &LogiFlowWindow::newFile);
  connect(newCodeFileAct, &QAction::triggered, this, &LogiFlowWindow::createCodeFile);
  connect(newBinaryFileAct, &QAction::triggered, this, &LogiFlowWindow::createBinaryFile);
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
  connect(undoAct, &QAction::triggered, this, [this] {
    if (isBinaryDocumentActive() && binaryEditor->history()->canUndo())
      binaryEditor->history()->undo();
    else if (isCodeDocumentActive() && codeEditor->document()->isUndoAvailable())
      codeEditor->undo();
    else
      undoStack->undo();
  });
  connect(redoAct, &QAction::triggered, this, [this] {
    if (isBinaryDocumentActive() && binaryEditor->history()->canRedo())
      binaryEditor->history()->redo();
    else if (isCodeDocumentActive() && codeEditor->document()->isRedoAvailable())
      codeEditor->redo();
    else
      undoStack->redo();
  });
  const auto updateHistoryActions = [this] {
    undoAct->setEnabled(
        undoStack->canUndo()
        || (isCodeDocumentActive() && codeEditor->document()->isUndoAvailable())
        || (isBinaryDocumentActive() && binaryEditor->history()->canUndo()));
    redoAct->setEnabled(
        undoStack->canRedo()
        || (isCodeDocumentActive() && codeEditor->document()->isRedoAvailable())
        || (isBinaryDocumentActive() && binaryEditor->history()->canRedo()));
  };
  connect(undoStack, &QUndoStack::canUndoChanged, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(undoStack, &QUndoStack::canRedoChanged, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(codeEditor, &QPlainTextEdit::undoAvailable, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(codeEditor, &QPlainTextEdit::redoAvailable, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(binaryEditor->history(), &QUndoStack::canUndoChanged, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(binaryEditor->history(), &QUndoStack::canRedoChanged, this,
          [updateHistoryActions](bool) { updateHistoryActions(); });
  connect(editorStack, &QStackedWidget::currentChanged, this,
          [updateHistoryActions](int) { updateHistoryActions(); });
  updateHistoryActions();

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
  connect(codeConversionAct, &QAction::triggered, this,
          &LogiFlowWindow::convertActiveDocument);
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
  fileMenu->addAction(newCodeFileAct);
  fileMenu->addAction(newBinaryFileAct);
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

  diagramToolsSeparator = toolBar->addSeparator();

  toolBar->addAction(setNormalModeAct);
  toolBar->addAction(setPanModeAct);
  toolBar->addAction(setWireCreationModeAct);
  toolBar->addAction(setSimulationModeAct);
  toolBar->addAction(toggleFstTraceAct);

  documentToolsSeparator = toolBar->addSeparator();
  toolBar->addAction(openComponentCatalogAct);
  toolBar->addAction(editSubcircuitShapeAct);
  toolBar->addAction(codeConversionAct);

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
  const bool active = SILICON::project::documentTypeForPath(activeDocumentPath)
                      == SILICON::project::DocumentType::Subcircuit;
  editSubcircuitShapeAct->setVisible(active);
  editSubcircuitShapeAct->setEnabled(active);
  updateCodeAction();
}

bool LogiFlowWindow::isCodeDocumentActive() const
{
  return SILICON::project::documentTypeForPath(activeDocumentPath)
         == SILICON::project::DocumentType::Code;
}

bool LogiFlowWindow::isBinaryDocumentActive() const
{
  return SILICON::project::documentTypeForPath(activeDocumentPath)
         == SILICON::project::DocumentType::Binary;
}

void LogiFlowWindow::updateCodeAction()
{
  if (!codeConversionAct)
    return;
  const auto* document =
      SILICON::project::DocumentStore::active().find(activeDocumentPath);
  const bool subcircuit =
      document && document->getType() == SILICON::project::DocumentType::Subcircuit;
  const bool verilog = document
                       && SILICON::project::codeFileTypeForPath(document->getPath())
                              == SILICON::project::CodeFileType::Verilog;
  codeConversionAct->setVisible(subcircuit || verilog);
  codeConversionAct->setText(subcircuit ? tr("Convert to Verilog")
                                        : tr("Convert to Subcircuit"));
#ifdef __EMSCRIPTEN__
  codeConversionAct->setEnabled(false);
  codeConversionAct->setToolTip(
      tr("Conversion requires Yosys and is unavailable in the web build"));
#else
  codeConversionAct->setEnabled(subcircuit || verilog);
#endif
  const bool code = isCodeDocumentActive();
  const bool binary       = isBinaryDocumentActive();
  const bool nonGraphical = code || binary;
  setActionsEnabled({setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                     setSimulationModeAct, toggleFstTraceAct, openComponentCatalogAct,
                     setComponentPlacingModeAct, autoPlaceAct},
                    !nonGraphical);

  if (toolBar) {
    for (auto* action :
         {setNormalModeAct, setPanModeAct, setWireCreationModeAct, setSimulationModeAct,
          toggleFstTraceAct, openComponentCatalogAct}) {
      if (auto* widget = toolBar->widgetForAction(action))
        widget->setVisible(!nonGraphical);
    }
    if (auto* widget = toolBar->widgetForAction(diagramToolsSeparator))
      widget->setVisible(!nonGraphical);
    if (auto* widget = toolBar->widgetForAction(documentToolsSeparator))
      widget->setVisible(!nonGraphical || codeConversionAct->isEnabled());
    if (auto* widget = toolBar->widgetForAction(codeConversionAct))
      widget->setVisible(codeConversionAct->isVisible()
                         && codeConversionAct->isEnabled());
  }
}

void LogiFlowWindow::commitConvertedDocument(SILICON::project::Document document,
                                             const std::string&         sourcePath,
                                             const QString&             commandText)
{
  const auto targetPath = document.getPath();
  std::vector<SILICON::project::Document> documents;
  documents.push_back(std::move(document));
  commitConvertedDocuments(std::move(documents), sourcePath, targetPath, commandText);
}

void LogiFlowWindow::commitConvertedDocuments(
    std::vector<SILICON::project::Document> documents, const std::string& sourcePath,
    const std::string& activatePath, const QString& commandText)
{
  auto& store = SILICON::project::DocumentStore::active();
  auto  beforeDocuments = store.getDocuments();
  auto  afterDocuments  = beforeDocuments;

  for (auto& document : documents) {
    const auto existing = std::ranges::find(afterDocuments, document.getPath(),
                                            &SILICON::project::Document::getPath);
    if (existing == afterDocuments.end())
      afterDocuments.push_back(std::move(document));
    else
      *existing = std::move(document);
  }

  SILICON::project::ProjectDependencyGraph beforeGraph;
  beforeGraph.rebuildFromProject(beforeDocuments);
  SILICON::project::ProjectDependencyGraph afterGraph;
  afterGraph.rebuildFromProject(afterDocuments);

  auto apply = [this, afterDocuments, afterGraph, activatePath] {
    SILICON::project::DocumentStore::active().setDocuments(afterDocuments);
    dependencyGraph = afterGraph;
    rebuildProjectTree();
    switchToDocument(activatePath, true);
  };
  auto restore = [this, beforeDocuments, beforeGraph, sourcePath] {
    if (SILICON::project::DocumentStore::active().contains(sourcePath))
      switchToDocument(sourcePath, false);
    SILICON::project::DocumentStore::active().setDocuments(beforeDocuments);
    dependencyGraph = beforeGraph;
    rebuildProjectTree();
    switchToDocument(sourcePath, true);
  };
  undoStack->push(
      new ConversionCommand(commandText, std::move(restore), std::move(apply)));
}

void LogiFlowWindow::convertActiveSubcircuitToVerilog()
{
  saveActiveDocumentPayload();
  const auto  sourcePath = activeDocumentPath;
  const auto  slug       = activeProjectSubcircuitSlug();
  const auto* existing   = SILICON::project::DocumentStore::active().find(sourcePath);
  if (!existing || slug.empty())
    throw std::runtime_error("Only subcircuits can be converted to Verilog");
  auto circuit =
      Circuit::deserialize(SILICON::core::extractCoreCircuitJson(existing->getContents()),
                           ComponentRegistry::instance());
  circuit.setName(slug);
  const auto source = SILICON::yosys::exportVerilog(circuit);
  const auto path =
      SILICON::project::codeFilePath(slug, SILICON::project::CodeFileType::Verilog);
  const SILICON::project::Document result(path, source);
  auto commit = [this, result, sourcePath] {
    commitConvertedDocument(result, sourcePath, tr("Convert to Verilog"));
  };
  if (hasDocument(path)) {
    SILICON::ui::inputDialog::question(
        this, tr("Replace Code File"),
        tr("'%1' already exists. Replace it?").arg(QString::fromStdString(path)),
        std::move(commit));
  } else {
    commit();
  }
}

void LogiFlowWindow::convertActiveVerilogToSubcircuit()
{
  saveActiveDocumentPayload();
  const auto  sourcePath = activeDocumentPath;
  const auto* existing   = SILICON::project::DocumentStore::active().find(sourcePath);
  if (!existing
      || SILICON::project::codeFileTypeForPath(existing->getPath())
             != SILICON::project::CodeFileType::Verilog)
    throw std::runtime_error("Only Verilog code files can be converted to subcircuits");

  const auto designJson = SILICON::yosys::elaborateHierarchy(
      SILICON::yosys::readVerilog(existing->getContents()));
  const auto modules = SILICON::yosys::moduleDependencyGraph(designJson);
  if (modules.modules().empty())
    throw std::runtime_error("Verilog source must declare at least one module");
  // Validate the complete design before opening the selector so recursive source
  // hierarchies fail with a normal conversion error instead of from a UI callback.
  static_cast<void>(modules.dependencyOrder(modules.modules()));

  const auto selectedRoots = selectVerilogModules(this, modules);
  if (!selectedRoots || selectedRoots->empty())
    return;
  const auto exportOrder = modules.dependencyOrder(*selectedRoots);

  for (const auto& module : exportOrder) {
    if (!SILICON::project::isValidSubcircuitSlug(module))
      throw std::runtime_error(std::format(
          "Verilog module '{}' cannot be used as a project subcircuit name", module));
  }

  std::vector<SILICON::project::Document> generated;
  generated.reserve(exportOrder.size());
  for (const auto& module : exportOrder) {
    auto circuit =
        std::make_shared<Circuit>(SILICON::yosys::deserialize(designJson, module));
    DiagramScene generatedScene;
    generatedScene.setSubcircuitDocumentMode(true);
    generatedScene.loadCircuit(std::move(circuit), GUIComponentFactory::instance(),
                               false);

    auto sceneJson = generatedScene.serialize();
    auto completed = nlohmann::ordered_json::parse(sceneJson);
    completed["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
        synchronizeGraphicalSubcircuitMetadata(sceneJson, GraphicalSubcircuitMetadata{}));
    generated.push_back(preparedSubcircuitDocument(
        SILICON::project::subcircuitPathForSlug(module), completed.dump(2)));
  }

  const auto activatePath =
      SILICON::project::subcircuitPathForSlug(selectedRoots->front());
  auto commit = [this, generated, sourcePath, activatePath]() mutable {
    commitConvertedDocuments(std::move(generated), sourcePath, activatePath,
                             tr("Convert Verilog Modules to Subcircuits"));
  };

  QStringList conflicts;
  for (const auto& document : generated)
    if (hasDocument(document.getPath()))
      conflicts.push_back(QString::fromStdString(document.getPath()));

  if (!conflicts.empty()) {
    SILICON::ui::inputDialog::question(
        this, tr("Replace Subcircuits"),
        tr("The following subcircuits already exist and will be replaced:\n\n%1")
            .arg(conflicts.join('\n')),
        std::move(commit));
  } else {
    commit();
  }
}

void LogiFlowWindow::convertActiveDocument()
{
#ifndef __EMSCRIPTEN__
  try {
    const auto type = SILICON::project::documentTypeForPath(activeDocumentPath);
    if (type == SILICON::project::DocumentType::Subcircuit)
      convertActiveSubcircuitToVerilog();
    else if (type == SILICON::project::DocumentType::Code)
      convertActiveVerilogToSubcircuit();
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::critical(
        this, tr("Code Conversion Error"),
        tr("Failed to convert the active document:\n%1").arg(error.what()));
  }
#endif
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
