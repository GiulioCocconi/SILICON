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

}  // namespace

void LogiFlowWindow::newFile()
{
  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::critical(
        this, tr("HDL Error"),
        tr("Compile the active HDL before creating a new project:\n%1")
            .arg(error.what()));
    return;
  }
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
  projectAssets.clear();
  hdlCodeMode = false;
  hdlEditor->clear();
  hdlEditor->setReadOnly(true);
  editorStack->setCurrentWidget(diagramView);
  diagramScene->clear();
  diagramScene->setCircuit(std::make_shared<Circuit>());
  diagramScene->setSubcircuitDocumentMode(false);
  auto document = defaultCircuitDocument();
  document.setSceneJson(diagramScene->serialize());
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

void LogiFlowWindow::autoPlace()
{
  diagramScene->autoPlaceCircuit(true);
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

    auto projectFile = SILICON::project::readProjectFile(archivePath.toStdString());

    // Clear the current scene items to prepare for the new circuit.
    diagramScene->clear();

    // 3. Update application state on success
    currentProjectMetadata = std::move(projectFile.metadata);
    currentProjectInfo     = std::move(projectFile.project);
    projectAssets          = std::move(projectFile.assets);
    activeDocumentPath     = projectMainCircuitPath();
    std::vector<SILICON::project::Document> documents;
    documents.reserve(projectFile.documents.size());
    for (auto& document : projectFile.documents) {
      if (document.kind() == SILICON::project::DocumentKind::Subcircuit)
        documents.push_back(
            preparedSubcircuitDocument(document.getPath(), document.getSceneJson()));
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
    diagramScene->deserialize(document->getSceneJson(), guiFactory, coreRegistry);
    editorStack->setCurrentWidget(diagramView);
    hdlCodeMode = false;
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
    SILICON::ui::inputDialog::critical(this, tr("Load Error"),
                                 tr("Failed to load the circuit:\n%1").arg(e.what()));
  }
}

void LogiFlowWindow::open()
{
  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::critical(
        this, tr("HDL Error"),
        tr("Compile the active HDL before opening another project:\n%1")
            .arg(error.what()));
    return;
  }
  SILICON::ui::fileDialog::openFileContent(
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
    SILICON::ui::inputDialog::critical(
        this, tr("Save Error"),
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
    const auto* mainDocument =
        SILICON::project::DocumentStore::active().find(project.mainCircuit);
    if (!mainDocument)
      throw std::runtime_error("Main circuit payload is missing");

    SILICON::project::ProjectFile projectFile{.metadata  = metadata,
                                              .project   = project,
                                              .documents = documents,
                                              .assets    = projectAssets,
                                              .mainCircuitJson =
                                                  mainDocument->getSceneJson()};

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
      return;
    setFileName(*savedFileName);
#else
    SILICON::project::writeProjectFile(destinationFileName.toStdString(), projectFile);
    setFileName(destinationFileName);
#endif
    currentProjectMetadata = std::move(metadata);
    updateProjectTreeLabels();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(this, tr("Save Error"),
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
  if (hdlCodeMode)
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
  if (enabled && hdlCodeMode) {
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
  const auto interactionMode = diagramScene->getInteractionMode();
  const auto selected        = diagramScene->selectedItems();
  const bool hasSelection    = !selected.empty();

  // Enable rotation only when a single component is selected or when in component placing
  // mode
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
  auto* selectedProjectItem = projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selectedProjectItem) {
    updatePropertyDock();
    return;
  }

  const auto itemKind = ProjectTree::itemKind(selectedProjectItem);
  if (itemKind == ProjectTreeItemKind::Circuit
      || itemKind == ProjectTreeItemKind::Subcircuit) {
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
  menu->addAction(Icon("plus"), tr("New Circuit"), this, &LogiFlowWindow::createCircuit);
  menu->addAction(Icon("plus"), tr("New Subcircuit"), this,
                  &LogiFlowWindow::createSubcircuit);

  auto* selectedProjectItem = projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (selectedProjectItem) {
    const auto kind = ProjectTree::itemKind(selectedProjectItem);
    if (kind == ProjectTreeItemKind::Circuit || kind == ProjectTreeItemKind::Subcircuit) {
      const auto path         = ProjectTree::documentPath(selectedProjectItem);
      const bool circuit      = kind == ProjectTreeItemKind::Circuit;
      auto*      deleteAction = menu->addAction(
          Icon("delete"), circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"), this,
          &LogiFlowWindow::deleteSelectedDocument);
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
  createDocument(SILICON::project::DocumentKind::Circuit);
}

void LogiFlowWindow::createSubcircuit()
{
  createDocument(SILICON::project::DocumentKind::Subcircuit);
}

void LogiFlowWindow::createDocument(const SILICON::project::DocumentKind kind)
{
  const bool circuit = kind == SILICON::project::DocumentKind::Circuit;
  SILICON::ui::inputDialog::getText(
      this, circuit ? tr("New Circuit") : tr("New Subcircuit"),
      circuit ? tr("Circuit name") : tr("Subcircuit name"),
      circuit ? tr("Circuit") : tr("Subcircuit"),
      [this, kind](const QString& requestedName) {
        const bool    circuit     = kind == SILICON::project::DocumentKind::Circuit;
        const QString trimmedName = requestedName.trimmed();
        const QString displayName =
            trimmedName.isEmpty()
                ? (circuit ? QStringLiteral("Circuit") : QStringLiteral("Subcircuit"))
                : trimmedName;
        const auto displayNameString = displayName.toStdString();
        const auto path              = uniqueDocumentPath(kind, displayName);
        const auto sceneJson         = circuit ? emptyCircuitSceneJson(displayNameString)
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
  auto* selectedProjectItem = projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selectedProjectItem)
    return;

  const auto itemKind = ProjectTree::itemKind(selectedProjectItem);
  if (itemKind != ProjectTreeItemKind::Circuit
      && itemKind != ProjectTreeItemKind::Subcircuit)
    return;

  const auto path = ProjectTree::documentPath(selectedProjectItem);
  if (path == projectMainCircuitPath())
    return;
  const bool circuit = itemKind == ProjectTreeItemKind::Circuit;

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::warning(
        this, circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
        tr("Failed to save the active document before deleting it:\n%1").arg(e.what()));
    return;
  }

  if (!circuit) {
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
      this, circuit ? tr("Delete Circuit") : tr("Delete Subcircuit"),
      (circuit ? tr("Delete circuit \"%1\"?") : tr("Delete subcircuit \"%1\"?"))
          .arg(selectedProjectItem->text(0)),
      [this, path, circuit] {
        auto&       store          = SILICON::project::DocumentStore::active();
        const auto* storedDocument = store.find(path);
        const auto  storedIndex    = store.indexOf(path);
        if (!storedDocument || !storedIndex)
          return;

        const auto document = *storedDocument;
        const auto index    = static_cast<std::ptrdiff_t>(*storedIndex);
        std::optional<SILICON::project::ProjectAsset> hdlAsset;
        if (const auto descriptor =
                SILICON::project::parseHdlDescriptor(document.getSceneJson())) {
          if (const auto* asset = projectAsset(descriptor->path))
            hdlAsset = *asset;
        }

        auto removeStored    = [this, path] { removeDocument(path); };
        auto restoreDocument = [this, document, index, hdlAsset] {
          if (hdlAsset && !projectAsset(hdlAsset->path))
            projectAssets.push_back(*hdlAsset);
          insertDocument(document, index, true);
        };

        undoStack->push(new ProjectStateCommand(circuit ? tr("Delete Circuit")
                                                        : tr("Delete Subcircuit"),
                                                restoreDocument, removeStored));
      });
}

}  // namespace ui
}  // namespace SILICON
