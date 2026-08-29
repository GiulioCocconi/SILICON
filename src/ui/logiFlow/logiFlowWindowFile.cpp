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

  // Clipboard data is intentionally BSON-only so partial JSON fallbacks cannot drift
  // from the native LogiFlow selection format.
  constexpr auto LogiFlowSelectionMimeType =
      "application/vnd.silicon.logiflow-selection+bson";

  const SILICON::logging::Logger uiLog("ui");

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

  class ProjectStateCommand : public QUndoCommand {
  public:
    using Fn = std::function<void()>;

    ProjectStateCommand(QString text, Fn undoFn, Fn redoFn,
                        QUndoCommand* parent = nullptr)
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

  bool hasClipboardItems(const nlohmann::json& payload)
  {
    if (!payload.contains("visual") || !payload["visual"].is_object())
      return false;

    const auto& visual        = payload["visual"];
    const bool  hasComponents = visual.contains("components")
                               && visual["components"].is_array()
                               && !visual["components"].empty();
    const bool hasWires = visual.contains("wires") && visual["wires"].is_array()
                          && !visual["wires"].empty();

    return hasComponents || hasWires;
  }

}  // namespace

void LogiFlowWindow::newFile()
{
  confirmSaveIfDirty([this] {
    setFileName("");
    resetProjectState();
    rebuildProjectTree();
    updatePropertyDock();
  });
}

void LogiFlowWindow::resetProjectState()
{
  currentProjectMetadata.reset();
  currentProjectInfo = defaultProjectInfo(currentFileName);
  activeDocumentPath = defaultMainCircuitPath();
  codeDocumentsDirty = false;
  codeEditor->clearFileType();
  editorStack->setCurrentWidget(diagramView);
  diagramScene->clear();
  diagramScene->setCircuit(std::make_shared<Circuit>());
  diagramScene->setSubcircuitDocumentMode(false);
  auto document = defaultCircuitDocument();
  document.setContents(diagramScene->serialize());
  SILICON::project::DocumentStore::active().setDocuments({std::move(document)});
  dependencyGraph.rebuildFromProject(
      SILICON::project::DocumentStore::active().getDocuments());
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
  if (isCodeDocumentActive()) {
    codeEditor->copy();
    return;
  }
  copySelectionToClipboard();
}

void LogiFlowWindow::cut()
{
  if (isCodeDocumentActive()) {
    codeEditor->cut();
    return;
  }
  if (copySelectionToClipboard())
    del();
}

void LogiFlowWindow::paste()
{
  if (isCodeDocumentActive()) {
    codeEditor->paste();
    return;
  }
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

void LogiFlowWindow::autoPlace()
{
  diagramScene->autoPlaceCircuit(true);
}

void LogiFlowWindow::del()
{
  if (isCodeDocumentActive()) {
    auto cursor = codeEditor->textCursor();
    if (cursor.hasSelection())
      cursor.removeSelectedText();
    else
      cursor.deleteChar();
    codeEditor->setTextCursor(cursor);
    return;
  }
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

    auto projectFile = SILICON::project::readProjectFile(archivePath.toStdString());

    // Clear the current scene items to prepare for the new circuit.
    diagramScene->clear();

    // 3. Update application state on success
    currentProjectMetadata = std::move(projectFile.metadata);
    currentProjectInfo     = std::move(projectFile.project);
    codeDocumentsDirty     = false;
    activeDocumentPath     = projectMainCircuitPath();
    std::vector<SILICON::project::Document> documents;
    documents.reserve(projectFile.documents.size());
    for (auto& document : projectFile.documents) {
      if (document.getType() == SILICON::project::DocumentType::Subcircuit)
        documents.push_back(
            preparedSubcircuitDocument(document.getPath(), document.getContents()));
      else
        documents.push_back(std::move(document));
    }

    SILICON::project::DocumentStore::active().setDocuments(std::move(documents));
    dependencyGraph.rebuildFromProject(
        SILICON::project::DocumentStore::active().getDocuments());

    auto&       guiFactory   = GUIComponentFactory::instance();
    auto&       coreRegistry = ComponentRegistry::instance();
    const auto* document =
        SILICON::project::DocumentStore::active().find(activeDocumentPath);
    if (!document)
      throw std::runtime_error("Main circuit payload is missing");
    diagramScene->deserialize(document->getContents(), guiFactory, coreRegistry);
    editorStack->setCurrentWidget(diagramView);
    diagramScene->setSubcircuitDocumentMode(false);
    updateSubcircuitShapeAction();

    setFileName(fileName);
    rebuildProjectTree();
    updatePropertyDock();

  } catch (const nlohmann::json::exception& e) {
    SILICON::ui::inputDialog::critical(
        this, tr("Corrupted File"),
        tr("The circuit file contains invalid JSON data:\n%1").arg(e.what()));
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        this, tr("Load Error"), tr("Failed to load the circuit:\n%1").arg(e.what()));
  }
}

void LogiFlowWindow::open()
{
  confirmSaveIfDirty([this] {
    SILICON::ui::fileDialog::openFileContent(
        this, tr("Open Circuit"), tr("Silicon Circuit (*.sil);;All Files (*)"),
        [this](const QString& fileName, const QByteArray& fileContent) {
          loadCircuitContent(fileName, fileContent);
        });
  });
}

bool LogiFlowWindow::save()
{
  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        this, tr("Save Error"),
        tr("Failed to serialize the active circuit:\n%1").arg(e.what()));
    return false;
  }

  QString destinationFileName = currentFileName;
#ifndef __EMSCRIPTEN__
  if (destinationFileName.isEmpty()) {
    destinationFileName =
        QFileDialog::getSaveFileName(this, tr("Save Circuit"), QString(),
                                     tr("Silicon Circuit (*.sil);;All Files (*)"));
    if (destinationFileName.isEmpty())
      return false;
  }
#else
  if (destinationFileName.isEmpty())
    destinationFileName = QStringLiteral("circuit.sil");
#endif

  try {
    auto metadata =
        currentProjectMetadata.value_or(SILICON::project::metadataForNewFile());
    metadata.formatVersion  = SILICON::project::FORMAT_VERSION;
    metadata.siliconVersion = SILICON_VERSION;
    metadata.lastModify     = SILICON::project::currentUtcTimestamp();

    auto project = currentProjectInfo.value_or(SILICON::project::ProjectInfo{});
    if (project.name.empty())
      project.name = QFileInfo(destinationFileName).baseName().toStdString();
    if (project.mainCircuit.empty())
      project.mainCircuit = defaultMainCircuitPath();
    currentProjectInfo = project;
    ensureProjectDocuments();
    const auto  documents = SILICON::project::DocumentStore::active().getDocuments();
    SILICON::project::ProjectFile projectFile{.metadata  = metadata,
                                              .project   = project,
                                              .documents = documents};

#ifdef __EMSCRIPTEN__
    QTemporaryFile archive;
    if (!archive.open())
      throw std::runtime_error("Cannot create a temporary project archive");
    const QString archivePath = archive.fileName();
    archive.close();

    SILICON::project::writeProjectFile(archivePath.toStdString(), projectFile);

    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::ReadOnly))
      throw std::runtime_error("Cannot read the temporary project archive");

    const auto savedFileName = SILICON::ui::fileDialog::saveFileContent(
        this, tr("Save Circuit"), destinationFileName,
        tr("Silicon Circuit (*.sil);;All Files (*)"), archiveFile.readAll());
    if (!savedFileName)
      return false;
    setFileName(*savedFileName);
#else
    SILICON::project::writeProjectFile(destinationFileName.toStdString(), projectFile);
    setFileName(destinationFileName);
#endif
    currentProjectMetadata = std::move(metadata);
    updateProjectTreeLabels();
    undoStack->setClean();
    codeDocumentsDirty = false;
    return true;
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        this, tr("Save Error"), tr("Failed to save the circuit:\n%1").arg(e.what()));
    return false;
  }
}

void LogiFlowWindow::confirmSaveIfDirty(std::function<void()> continuation)
{
  if ((!undoStack || undoStack->isClean()) && !codeDocumentsDirty
      && (!codeEditor || !codeEditor->document()->isModified())) {
    continuation();
    return;
  }

  SILICON::ui::inputDialog::warningChoice(
      this, tr("Unsaved Changes"),
      tr("The current project has unsaved changes. Do you want to save them?"),
      tr("Save"), tr("Discard"),
      [this, continuation = std::move(continuation)](
          const SILICON::ui::inputDialog::Choice choice) {
        if (choice == SILICON::ui::inputDialog::Choice::Cancel)
          return;
        if (choice == SILICON::ui::inputDialog::Choice::Primary && !save())
          return;

        continuation();
      });
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
  syncWasmShortcutCapture();
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
  if (isCodeDocumentActive())
    return;
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
  if (enabled && isCodeDocumentActive()) {
    const QSignalBlocker blocker(toggleFstTraceAct);
    toggleFstTraceAct->setChecked(false);
    return;
  }

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
      tr("Interaction Mode: %1")
          .arg(interactionModeName(diagramScene->getInteractionMode())));
}

void LogiFlowWindow::selectionChanged()
{
  if (isCodeDocumentActive()) {
    rotateAct->setEnabled(false);
    setActionsEnabled({cutAct, copyAct, pasteAct, deleteAct}, true);
    updatePropertyDock();
    return;
  }
  const auto interactionMode = diagramScene->getInteractionMode();
  const auto selected        = diagramScene->selectedItems();
  const bool hasSelection    = !selected.empty();

  // Enable rotation only when a single component is selected or when in component
  // placing mode
  rotateAct->setEnabled(
      (interactionMode == InteractionMode::NORMAL_MODE && selected.size() == 1)
      || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  // Enable cut, copy and delete only when in normal mode and some items are selected
  const bool cutCopyDelete =
      interactionMode == InteractionMode::NORMAL_MODE && hasSelection;
  setActionsEnabled({cutAct, copyAct, deleteAct}, cutCopyDelete);

  if (hasSelection)
    clearProjectTreeSelection();

  updatePropertyDock();
}

void LogiFlowWindow::projectTreeSelectionChanged()
{
  auto* selectedProjectItem =
      projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selectedProjectItem) {
    updatePropertyDock();
    return;
  }

  const auto itemKind = ProjectTree::itemKind(selectedProjectItem);
  if (itemKind == ProjectTreeItemKind::Circuit
      || itemKind == ProjectTreeItemKind::Subcircuit
      || itemKind == ProjectTreeItemKind::CodeFile) {
    const QSignalBlocker blocker(diagramScene);
    diagramScene->clearSelection();
    switchToDocument(ProjectTree::documentPath(selectedProjectItem), false);
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
  menu->addAction(Icon("code"), tr("New Code File"), this,
                  &LogiFlowWindow::createCodeFile);

  auto* selectedProjectItem =
      projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (selectedProjectItem) {
    const auto kind = ProjectTree::itemKind(selectedProjectItem);
    if (kind == ProjectTreeItemKind::Circuit || kind == ProjectTreeItemKind::Subcircuit
        || kind == ProjectTreeItemKind::CodeFile) {
      const auto path         = ProjectTree::documentPath(selectedProjectItem);
      const bool circuit      = kind == ProjectTreeItemKind::Circuit;
      const bool code         = kind == ProjectTreeItemKind::CodeFile;
      auto*      deleteAction = menu->addAction(
          Icon("delete"),
          circuit ? tr("Delete Circuit")
                       : (code ? tr("Delete Code File") : tr("Delete Subcircuit")),
          this, &LogiFlowWindow::deleteSelectedDocument);
      deleteAction->setEnabled(path != projectMainCircuitPath());
    }
  }

#ifdef __EMSCRIPTEN__
  menu->popup(projectTree->viewport()->mapToGlobal(position));
#else
  menu->exec(projectTree->viewport()->mapToGlobal(position));
#endif
}

void LogiFlowWindow::createCircuit()
{
  createDocument(SILICON::project::DocumentType::Circuit);
}

void LogiFlowWindow::createSubcircuit()
{
  createDocument(SILICON::project::DocumentType::Subcircuit);
}

void LogiFlowWindow::createCodeFile()
{
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("New Code File"));
  dialog->setModal(true);

  auto* form     = new QFormLayout(dialog);
  auto* nameEdit = new QLineEdit(tr("untitled"), dialog);
  auto* typeBox  = new QComboBox(dialog);
  for (const auto& info : SILICON::project::codeFileTypeRegistry())
    typeBox->addItem(QString::fromUtf8(info.displayName));
  form->addRow(tr("Name"), nameEdit);
  form->addRow(tr("File type"), typeBox);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog,
          [this, dialog, nameEdit, typeBox] {
            const auto& registry = SILICON::project::codeFileTypeRegistry();
            const auto  index    = typeBox->currentIndex();
            if (index < 0 || index >= static_cast<int>(registry.size()))
              return;

            const auto type = registry[static_cast<std::size_t>(index)].type;
            const auto baseName = nameEdit->text().trimmed().toStdString();
            const auto path     = SILICON::project::codeFilePath(baseName, type);
            if (!SILICON::project::isValidCodeFilePath(path, type)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Code File"),
                  tr("The name must be non-empty and cannot contain path separators."));
              return;
            }
            if (hasDocument(path)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Code File"),
                  tr("A code file named '%1' already exists.")
                      .arg(QString::fromStdString(path)));
              return;
            }

            const SILICON::project::Document document(path, "");
            auto addDocument = [this, document] {
              try {
                saveActiveDocumentPayload();
              } catch (const std::exception&) {
              }
              insertDocument(document, std::nullopt, true);
            };
            auto removeCreated = [this, path] { removeDocument(path); };
            undoStack->push(new ProjectStateCommand(tr("Create Code File"),
                                                    removeCreated, addDocument));
            dialog->accept();
          });
  nameEdit->selectAll();
  nameEdit->setFocus();
  dialog->open();
}

void LogiFlowWindow::createDocument(const SILICON::project::DocumentType type)
{
  const bool circuit = type == SILICON::project::DocumentType::Circuit;
  SILICON::ui::inputDialog::getText(
      this, circuit ? tr("New Circuit") : tr("New Subcircuit"),
      circuit ? tr("Circuit name") : tr("Subcircuit name"),
      circuit ? tr("Circuit") : tr("Subcircuit"),
      [this, type](const QString& requestedName) {
        const bool    circuit     = type == SILICON::project::DocumentType::Circuit;
        const QString trimmedName = requestedName.trimmed();
        const QString displayName =
            trimmedName.isEmpty()
                ? (circuit ? QStringLiteral("Circuit") : QStringLiteral("Subcircuit"))
                : trimmedName;
        const auto displayNameString = displayName.toStdString();
        const auto path              = uniqueDocumentPath(type, displayName);
        const auto sceneJson = circuit ? emptyCircuitSceneJson(displayNameString)
                                       : emptySubcircuitSceneJson(displayNameString);
        SILICON::project::Document document =
            circuit ? SILICON::project::Document(path, sceneJson)
                    : preparedSubcircuitDocument(path, sceneJson);

        auto addDocument = [this, document] {
          try {
            saveActiveDocumentPayload();
          } catch (const std::exception&) {
          }
          insertDocument(document, std::nullopt, true);
        };

        auto removeCreated = [this, path] { removeDocument(path); };

        undoStack->push(new ProjectStateCommand(circuit ? tr("Create Circuit")
                                                        : tr("Create Subcircuit"),
                                                removeCreated, addDocument));
      });
}

void LogiFlowWindow::deleteSelectedCircuit()
{
  deleteSelectedDocument();
}

void LogiFlowWindow::deleteSelectedSubcircuit()
{
  deleteSelectedDocument();
}

void LogiFlowWindow::deleteSelectedDocument()
{
  auto* selectedProjectItem =
      projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selectedProjectItem)
    return;

  const auto itemKind = ProjectTree::itemKind(selectedProjectItem);
  if (itemKind != ProjectTreeItemKind::Circuit
      && itemKind != ProjectTreeItemKind::Subcircuit
      && itemKind != ProjectTreeItemKind::CodeFile)
    return;

  const auto path = ProjectTree::documentPath(selectedProjectItem);
  if (path == projectMainCircuitPath())
    return;
  const bool circuit = itemKind == ProjectTreeItemKind::Circuit;
  const bool code    = itemKind == ProjectTreeItemKind::CodeFile;

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::warning(
        this, circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
        tr("Failed to save the active document before deleting it:\n%1").arg(e.what()));
    return;
  }

  if (!circuit && !code) {
    const auto dependents = dependencyGraph.dependentsOf(path);
    if (!dependents.empty()) {
      QStringList dependentNames;
      for (const auto& dependent : dependents)
        dependentNames.push_back(QString::fromStdString(dependent));
      SILICON::ui::inputDialog::warning(
          this, tr("Delete Subcircuit"),
          tr("This subcircuit is still used by:\n%1").arg(dependentNames.join('\n')));
      return;
    }
  }

  SILICON::ui::inputDialog::question(
      this,
      circuit ? tr("Delete Circuit")
              : (code ? tr("Delete Code File") : tr("Delete Subcircuit")),
      (circuit
           ? tr("Delete circuit \"%1\"?")
           : (code ? tr("Delete code file \"%1\"?") : tr("Delete subcircuit \"%1\"?")))
          .arg(selectedProjectItem->text(0)),
      [this, path, circuit, code] {
        auto&       store          = SILICON::project::DocumentStore::active();
        const auto* storedDocument = store.find(path);
        const auto  storedIndex    = store.indexOf(path);
        if (!storedDocument || !storedIndex)
          return;

        const auto document        = *storedDocument;
        const auto index           = static_cast<std::ptrdiff_t>(*storedIndex);
        auto       removeStored    = [this, path] { removeDocument(path); };
        auto       restoreDocument = [this, document, index] {
          insertDocument(document, index, true);
        };

        undoStack->push(new ProjectStateCommand(
            circuit ? tr("Delete Circuit")
                    : (code ? tr("Delete Code File") : tr("Delete Subcircuit")),
            restoreDocument, removeStored));
      });
}

}  // namespace ui
}  // namespace SILICON
