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
#include <QUndoStack>
#include <QVBoxLayout>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/simulator.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/binaryEditor.hpp>
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
#include <ui/logiFlow/code/codeEditor.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/componentShapeEditor.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/metadataDescriptionEdit.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/document_conversion.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON {
namespace ui {
  using namespace SILICON::core;

  namespace {

    const SILICON::logging::Logger uiLog("ui");

    [[nodiscard]] QIcon categoryIcon(const SILICON::project::DocumentType type)
    {
      std::string_view iconName;
      switch (SILICON::project::categoryOf(type)) {
        case SILICON::project::DocumentCategory::Diagram:
          iconName = "circuit-board";
          break;
        case SILICON::project::DocumentCategory::Code: iconName = "code"; break;
        case SILICON::project::DocumentCategory::Architecture: iconName = "cpu"; break;
        case SILICON::project::DocumentCategory::Binary: iconName = "file"; break;
      }
      return Icon(
          QString::fromUtf8(iconName.data(), static_cast<qsizetype>(iconName.size())));
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

  }  // namespace

  void LogiFlowWindow::setActionsEnabled(std::initializer_list<QAction*> actions,
                                         const bool                      enabled)
  {
    for (QAction* action : actions) {
      if (action)
        action->setEnabled(enabled);
    }
  }

  void LogiFlowWindow::createActions()
  {
    newAct = makeAction(this, Icon("file"), tr("&New"), tr("Create a new project"));
    newCircuitAct =
        makeAction(this, categoryIcon(SILICON::project::DocumentType::Circuit),
                   tr("Circuit"), tr("Create a new circuit"));
    newCodeFileAct =
        makeAction(this, categoryIcon(SILICON::project::DocumentType::Verilog),
                   tr("Code File..."), tr("Create an empty source-code document"));
    newArchitectureAct =
        makeAction(this, Icon("cpu"), tr("ISA Architecture..."),
                   tr("Create a SISL instruction format"));
    newBinaryFileAct =
        makeAction(this, categoryIcon(SILICON::project::DocumentType::RawBinary),
                   tr("Binary File..."), tr("Create a fixed-size raw binary document"));
    openAct = makeAction(this, Icon("open"), tr("&Open..."),
                         tr("Open an existing logiFlow file"));
    saveAct = makeAction(this, Icon("save"), tr("&Save"), tr("Save the circuit to disk"));
    exportImageAct = makeAction(this, Icon("export"), tr("&Export..."),
                                tr("Export the circuit as an image"));
    exitAct = makeAction(this, Icon("xmark"), tr("E&xit"), tr("Exit the application"));
    cutAct  = makeAction(this, Icon("cut"), tr("Cu&t"),
                         tr("Cut the current selection's contents to the clipboard"));
    copyAct = makeAction(this, Icon("copy"), tr("&Copy"));
    pasteAct =
        makeAction(this, Icon("paste"), tr("&Paste"),
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
    toggleWaveformViewerAct =
        makeAction(this, Icon("chart"), tr("Trace"), tr("Show waveform viewer"));
    toggleWaveformViewerAct->setCheckable(true);
    cancelInteractionAct =
        makeAction(this, QString(), tr("Cancel the current interaction"));

    openComponentCatalogAct =
        makeAction(this, Icon("plus"), "", tr("Open the component catalog"));
    editSubcircuitShapeAct = makeAction(this, Icon("circuit-board"), tr("Edit Shape"),
                                        tr("Edit the active circuit shape"));
    editSubcircuitShapeAct->setVisible(false);
    editSubcircuitShapeAct->setEnabled(false);
    codeConversionAct = makeAction(this, Icon("code"), tr("Code"));
    codeConversionAct->setVisible(false);
    codeConversionAct->setEnabled(false);
    buildArchitectureAct = makeAction(this, Icon("build"), tr("Build"),
                                      tr("Compile the current SISL instruction format"));
    visualizeArchitectureAct = makeAction(this, Icon("diagram"),
                                          tr("Visualize"), tr("Visualize SISL instruction formats"));
    setComponentPlacingModeAct =
        makeAction(this, Icon("plus"), "", tr("Open quick component search"));

    connect(newAct, &QAction::triggered, this, &LogiFlowWindow::newFile);
    connect(newCircuitAct, &QAction::triggered, this, &LogiFlowWindow::createCircuit);
    connect(newCodeFileAct, &QAction::triggered, this, &LogiFlowWindow::createCodeFile);
    connect(newArchitectureAct, &QAction::triggered, this,
            &LogiFlowWindow::createArchitecture);
    connect(buildArchitectureAct, &QAction::triggered, this,
            &LogiFlowWindow::buildActiveArchitecture);
    connect(visualizeArchitectureAct, &QAction::triggered, this,
            &LogiFlowWindow::visualizeActiveArchitecture);
    connect(newBinaryFileAct, &QAction::triggered, this,
            &LogiFlowWindow::createBinaryFile);
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
      if (isVisualizerActive()) return;
      const auto type = activeDocumentType();
      if (type
          && SILICON::project::categoryOf(*type)
                 == SILICON::project::DocumentCategory::Binary
          && binaryEditor->history()->canUndo())
        binaryEditor->history()->undo();
      else if (type
               && SILICON::project::isCodeDocument(*type)
               && activeCodeEditor()->document()->isUndoAvailable())
        activeCodeEditor()->undo();
      else
        undoStack->undo();
    });
    connect(redoAct, &QAction::triggered, this, [this] {
      if (isVisualizerActive()) return;
      const auto type = activeDocumentType();
      if (type
          && SILICON::project::categoryOf(*type)
                 == SILICON::project::DocumentCategory::Binary
          && binaryEditor->history()->canRedo())
        binaryEditor->history()->redo();
      else if (type
               && SILICON::project::isCodeDocument(*type)
               && activeCodeEditor()->document()->isRedoAvailable())
        activeCodeEditor()->redo();
      else
        undoStack->redo();
    });
    connect(undoStack, &QUndoStack::canUndoChanged, this,
            [this](bool) { updateHistoryActions(); });
    connect(undoStack, &QUndoStack::canRedoChanged, this,
            [this](bool) { updateHistoryActions(); });
    connect(codeEditor, &QPlainTextEdit::undoAvailable, this,
            [this](bool) { updateHistoryActions(); });
    connect(codeEditor, &QPlainTextEdit::redoAvailable, this,
            [this](bool) { updateHistoryActions(); });
    for (const auto& [type, editor] : architectureEditors) {
      connect(editor, &QPlainTextEdit::undoAvailable, this,
              [this](bool) { updateHistoryActions(); });
      connect(editor, &QPlainTextEdit::redoAvailable, this,
              [this](bool) { updateHistoryActions(); });
    }
    connect(binaryEditor->history(), &QUndoStack::canUndoChanged, this,
            [this](bool) { updateHistoryActions(); });
    connect(binaryEditor->history(), &QUndoStack::canRedoChanged, this,
            [this](bool) { updateHistoryActions(); });
    connect(editorStack, &QStackedWidget::currentChanged, this,
            [this](int) { updateHistoryActions(); });
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
    connect(toggleWaveformViewerAct, &QAction::toggled, this,
            &LogiFlowWindow::toggleFstTracing);

    addAction(setComponentPlacingModeAct);
    addAction(cancelInteractionAct);
  }

  void LogiFlowWindow::updateHistoryActions()
  {
    if (!undoAct || !redoAct)
      return;
    if (isVisualizerActive()) {
      undoAct->setEnabled(false);
      redoAct->setEnabled(false);
      return;
    }
    const auto type = activeDocumentType();
    const auto* editor = type && SILICON::project::isCodeDocument(*type)
                             ? activeCodeEditor()
                             : nullptr;
    const bool binary = type && SILICON::project::categoryOf(*type)
                                  == SILICON::project::DocumentCategory::Binary;
    undoAct->setEnabled(undoStack->canUndo()
                        || (editor && editor->document()->isUndoAvailable())
                        || (binary && binaryEditor->history()->canUndo()));
    redoAct->setEnabled(undoStack->canRedo()
                        || (editor && editor->document()->isRedoAvailable())
                        || (binary && binaryEditor->history()->canRedo()));
  }

  void LogiFlowWindow::createMenus()
  {
    fileMenu      = menuBar()->addMenu(tr("&File"));
    auto* newMenu = fileMenu->addMenu(Icon("file"), tr("&New"));
    newMenu->addAction(newCircuitAct);
    newMenu->addAction(newCodeFileAct);
    newMenu->addAction(newArchitectureAct);
    newMenu->addAction(newBinaryFileAct);
    fileMenu->addAction(openAct);
    fileMenu->addAction(saveAct);
    fileMenu->addAction(exportImageAct);
    fileMenu->addAction(toggleWaveformViewerAct);
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
    toolBar->addAction(toggleWaveformViewerAct);

    documentToolsSeparator = toolBar->addSeparator();
    toolBar->addAction(openComponentCatalogAct);
    toolBar->addAction(editSubcircuitShapeAct);
    toolBar->addAction(codeConversionAct);
    toolBar->addAction(buildArchitectureAct);
    toolBar->addAction(visualizeArchitectureAct);

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
      const QSignalBlocker blocker(toggleWaveformViewerAct);
      toggleWaveformViewerAct->setChecked(false);
      waveformViewer->setEditMode(false);
    });
    connect(diagramScene, &DiagramScene::waveformTraceReset, waveformViewer,
            &waveform::Viewer::resetTrace);
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
    const bool active = activeDocumentType() == SILICON::project::DocumentType::Circuit;
    editSubcircuitShapeAct->setVisible(active);
    editSubcircuitShapeAct->setEnabled(active);
    updateCodeAction();
  }

  std::optional<SILICON::project::DocumentType>
  LogiFlowWindow::activeDocumentType() const noexcept
  {
    return SILICON::project::documentTypeForPath(activeDocumentPath);
  }

  void LogiFlowWindow::updateCodeAction()
  {
    if (!codeConversionAct)
      return;
    const auto* document   = projectContext.documents().find(activeDocumentPath);
    const auto  converters = document ? documentConvertersFor(document->getType())
                                      : std::vector<const DocumentConverter*>{};
    const auto  available =
        std::ranges::count_if(converters, [](const DocumentConverter* converter) {
          return converter->available;
        });
    codeConversionAct->setVisible(!converters.empty());
    codeConversionAct->setEnabled(available != 0);
    codeConversionAct->setToolTip(
        !converters.empty() && available == 0
            ? QString::fromUtf8(
                  converters.front()->unavailableReason.data(),
                  static_cast<qsizetype>(converters.front()->unavailableReason.size()))
            : QString());
    if (converters.size() == 1) {
      codeConversionAct->setText(
          tr("Convert to %1").arg(documentTypeName(converters.front()->target)));
    } else if (!converters.empty()) {
      codeConversionAct->setText(tr("Convert..."));
    }
    const auto type         = activeDocumentType();
    const bool nonGraphical = type
                              && SILICON::project::categoryOf(*type)
                                     != SILICON::project::DocumentCategory::Diagram;
    setActionsEnabled({setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                       setSimulationModeAct, toggleWaveformViewerAct, openComponentCatalogAct,
                       setComponentPlacingModeAct, autoPlaceAct},
                      !nonGraphical);

    updateDocumentActionVisibility();
  }

  void LogiFlowWindow::updateDocumentActionVisibility()
  {
    if (!toolBar)
      return;

    const auto type    = activeDocumentType();
    const bool diagram = type
                         && SILICON::project::categoryOf(*type)
                                == SILICON::project::DocumentCategory::Diagram;

    const bool catalogVisible = diagram && openComponentCatalogAct->isEnabled();
    const bool shapeVisible =
        editSubcircuitShapeAct->isVisible() && editSubcircuitShapeAct->isEnabled();
    const bool conversionVisible =
        codeConversionAct->isVisible() && codeConversionAct->isEnabled();

    // QWidget visibility is not authoritative for toolbar actions: Qt may recreate or
    // show the widget again after the shared QAction changes state. Remove unavailable
    // actions from this toolbar and re-add the active group in its canonical order.
    for (auto* action :
         {diagramToolsSeparator, setNormalModeAct, setPanModeAct, setWireCreationModeAct,
          setSimulationModeAct, toggleWaveformViewerAct, documentToolsSeparator,
          openComponentCatalogAct, editSubcircuitShapeAct, codeConversionAct,
          buildArchitectureAct, visualizeArchitectureAct})
      toolBar->removeAction(action);

    if (diagram) {
      toolBar->addAction(diagramToolsSeparator);
      for (auto* action : {setNormalModeAct, setPanModeAct, setWireCreationModeAct,
                           setSimulationModeAct, toggleWaveformViewerAct}) {
        if (action->isEnabled())
          toolBar->addAction(action);
      }
    }

    const bool buildVisible = type == SILICON::project::DocumentType::Sisl;
    if (catalogVisible || shapeVisible || conversionVisible || buildVisible) {
      toolBar->addAction(documentToolsSeparator);
      if (catalogVisible)
        toolBar->addAction(openComponentCatalogAct);
      if (shapeVisible)
        toolBar->addAction(editSubcircuitShapeAct);
      if (conversionVisible)
        toolBar->addAction(codeConversionAct);
      if (buildVisible) {
        toolBar->addAction(buildArchitectureAct);
        toolBar->addAction(visualizeArchitectureAct);
      }
    }
  }

  void LogiFlowWindow::editActiveSubcircuitShape()
  {
    if (activeDocumentType() != SILICON::project::DocumentType::Circuit)
      return;

    const auto slug = SILICON::project::documentSlugForPath(activeDocumentPath);
    if (!slug)
      return;

    try {
      saveActiveDocumentPayload();
      editGraphicalSubcircuitShape(*slug, projectContext, undoStack, this);
    } catch (const std::exception& e) {
      SILICON::ui::inputDialog::warning(
          this, tr("Edit shape"),
          tr("Failed to save the active circuit before editing its shape:\n%1")
              .arg(e.what()));
    }
  }

  void LogiFlowWindow::about() const
  {
    aboutDialog->show();
  }



}  // namespace ui
}  // namespace SILICON
