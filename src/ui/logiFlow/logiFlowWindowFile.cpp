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

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPoint>
#include <QPointF>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QUndoCommand>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/component_registry.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/binaryEditor.hpp>
#include <ui/common/codeEditor.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
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

  [[nodiscard]] SILICON::project::Document
  preparedCircuitDocument(std::string path, std::string sceneJson)
  {
    try {
      auto json = nlohmann::ordered_json::parse(sceneJson);
      if (!subcircuitHasGraphicalMetadata(sceneJson)) {
        json["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
            synchronizeGraphicalSubcircuitMetadata(
                sceneJson, GraphicalSubcircuitMetadata{}));
        sceneJson = json.dump(2);
      }
    } catch (const nlohmann::json::exception&) {
    }
    return preparedSubcircuitDocument(std::move(path), std::move(sceneJson));
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
  binaryDocumentsDirty = false;
  codeEditor->clearFileType();
  binaryEditor->setData({});
  editorStack->setCurrentWidget(diagramView);
  diagramScene->clear();
  diagramScene->setCircuit(std::make_shared<Circuit>());
  diagramScene->setSubcircuitDocumentMode(true);
  auto document = defaultCircuitDocument();
  document = preparedCircuitDocument(document.getPath(), diagramScene->serialize());
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
  const auto type = activeDocumentType();
  if (type && SILICON::project::categoryOf(*type)
                  == SILICON::project::DocumentCategory::Code) {
    codeEditor->copy();
    return;
  }
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)
    return;

  copySelectionToClipboard();
}

void LogiFlowWindow::cut()
{
  const auto type = activeDocumentType();
  if (type && SILICON::project::categoryOf(*type)
                  == SILICON::project::DocumentCategory::Code) {
    codeEditor->cut();
    return;
  }
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)
    return;

  if (copySelectionToClipboard())
    del();
}

void LogiFlowWindow::paste()
{
  const auto type = activeDocumentType();
  if (type && SILICON::project::categoryOf(*type)
                  == SILICON::project::DocumentCategory::Code) {
    codeEditor->paste();
    return;
  }
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)
    return;

  const QMimeData* mimeData = QApplication::clipboard()->mimeData();
  if (!mimeData || !mimeData->hasFormat(LogiFlowSelectionMimeType))
    return;

  const QByteArray bytes = mimeData->data(LogiFlowSelectionMimeType);
  if (bytes.isEmpty())
    return;

  try {
    auto& guiFactory   = GUIComponentFactory::instance();
    auto& coreRegistry = ComponentRegistry::instance();
    const auto payload = nlohmann::json::from_bson(
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()));

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
  } catch (const std::exception&) {
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
  const auto type = activeDocumentType();
  if (type && SILICON::project::categoryOf(*type)
                  == SILICON::project::DocumentCategory::Code) {
    auto cursor = codeEditor->textCursor();
    if (cursor.hasSelection())
      cursor.removeSelectedText();
    else
      cursor.deleteChar();
    codeEditor->setTextCursor(cursor);
    return;
  }
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)
    return;

  auto itemsToDelete = diagramScene->selectedItems() | std::views::filter([](auto* item) {
                         return item->type() > UNKNOWN;
                       })
                       | std::ranges::to<std::vector>();
  if (itemsToDelete.empty())
    return;

  const auto payload = diagramScene->serializeItems(itemsToDelete);
  diagramScene->removeItems(itemsToDelete);
  undoStack->push(new SceneSelectionCommand(
      diagramScene, payload, SceneSelectionCommand::Operation::Remove, true));
}

void LogiFlowWindow::loadCircuitContent(const QString& fileName,
                                        const QByteArray& fileContent)
{
  uiLog.info(std::format("Opening {}", fileName.toStdString()));

  try {
    QTemporaryFile archive;
    if (!archive.open()
        || archive.write(fileContent) != static_cast<qint64>(fileContent.size())
        || !archive.flush())
      throw std::runtime_error("Cannot stage the selected project archive");

    const QString archivePath = archive.fileName();
    archive.close();

    auto projectFile = SILICON::project::readProjectFile(archivePath.toStdString());

    std::vector<SILICON::project::Document> documents;
    documents.reserve(projectFile.documents.size());
    for (auto& document : projectFile.documents) {
      if (document.getType() == SILICON::project::DocumentType::Circuit)
        documents.push_back(
            preparedCircuitDocument(document.getPath(), document.getContents()));
      else
        documents.push_back(std::move(document));
    }

    SILICON::project::ProjectDependencyGraph dependencies;
    dependencies.rebuildFromProject(documents);

    const auto mainPath = projectFile.project.mainCircuit;
    const auto main = std::ranges::find(
        documents, mainPath, &SILICON::project::Document::getPath);
    if (main == documents.end())
      throw std::runtime_error("Main circuit payload is missing");

    currentProjectMetadata = std::move(projectFile.metadata);
    currentProjectInfo     = std::move(projectFile.project);
    codeDocumentsDirty     = false;
    binaryDocumentsDirty   = false;
    activeDocumentPath     = mainPath;
    dependencyGraph        = std::move(dependencies);
    SILICON::project::DocumentStore::active().setDocuments(std::move(documents));

    const auto* document =
        SILICON::project::DocumentStore::active().find(activeDocumentPath);
    if (!document)
      throw std::runtime_error("Main circuit payload is missing");

    loadDocumentPayload(*document);
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
    rebuildProjectTree();
    undoStack->setClean();
    codeDocumentsDirty = false;
    binaryDocumentsDirty = false;
    return true;
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        this, tr("Save Error"), tr("Failed to save the circuit:\n%1").arg(e.what()));
    return false;
  }
}

void LogiFlowWindow::confirmSaveIfDirty(std::function<void()> continuation)
{
  if (!hasUnsavedChanges()) {
    continuation();
    return;
  }

  SILICON::ui::inputDialog::warningChoice(
      this, tr("Unsaved Changes"),
      tr("The current project has unsaved changes. Do you want to save them?"),
      tr("Save"), tr("Discard"),
      [this, continuation =
                 std::move(continuation)](const SILICON::ui::inputDialog::Choice choice) {
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
  const auto type = activeDocumentType();
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram)
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

void LogiFlowWindow::toggleFstTracing(const bool enabled)
{
  if (!waveformWindow)
    return;

  const auto type = activeDocumentType();
  if (enabled
      && (!type || SILICON::project::categoryOf(*type)
                       != SILICON::project::DocumentCategory::Diagram)) {
    const QSignalBlocker blocker(toggleFstTraceAct);
    toggleFstTraceAct->setChecked(false);
    return;
  }

  waveformWindow->setVisible(enabled);
  if (!enabled)
    return;

  diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
  waveformWindow->raise();
  waveformWindow->activateWindow();
}

void LogiFlowWindow::updateStatus() const
{
  statusBar()->showMessage(
      tr("Interaction Mode: %1")
          .arg(interactionModeName(diagramScene->getInteractionMode())));
}

void LogiFlowWindow::selectionChanged()
{
  const auto type = activeDocumentType();
  if (!type || SILICON::project::categoryOf(*type)
                   != SILICON::project::DocumentCategory::Diagram) {
    rotateAct->setEnabled(false);
    const bool editableText = type && SILICON::project::categoryOf(*type)
        == SILICON::project::DocumentCategory::Code;
    setActionsEnabled({cutAct, copyAct, pasteAct, deleteAct}, editableText);
    updatePropertyDock();
    return;
  }

  const auto interactionMode = diagramScene->getInteractionMode();
  const auto selected        = diagramScene->selectedItems();
  const bool hasSelection    = !selected.empty();

  rotateAct->setEnabled(
      (interactionMode == InteractionMode::NORMAL_MODE && selected.size() == 1)
      || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  const bool canEditSelection =
      interactionMode == InteractionMode::NORMAL_MODE && hasSelection;
  setActionsEnabled({cutAct, copyAct, deleteAct}, canEditSelection);

  if (hasSelection && projectTree)
    projectTree->clearDocumentSelection();

  updatePropertyDock();
}

void LogiFlowWindow::projectTreeSelectionChanged()
{
  const QSignalBlocker blocker(diagramScene);
  diagramScene->clearSelection();

  if (const auto selection = projectTree ? projectTree->selectedDocument()
                                         : std::nullopt) {
    switchToDocument(selection->path, false);
    return;
  }

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
  menu->addAction(Icon("code"), tr("New Code File"), this,
                  &LogiFlowWindow::createCodeFile);
  menu->addAction(Icon("file"), tr("New Binary File"), this,
                  &LogiFlowWindow::createBinaryFile);

  if (const auto selection = projectTree->selectedDocument()) {
    auto* deleteAction = menu->addAction(
        Icon("delete"), tr("Delete %1").arg(documentTypeName(selection->type)),
        this, &LogiFlowWindow::deleteSelectedDocument);
    deleteAction->setEnabled(selection->path != projectMainCircuitPath());
  }

#ifdef __EMSCRIPTEN__
  menu->popup(projectTree->viewport()->mapToGlobal(position));
#else
  menu->exec(projectTree->viewport()->mapToGlobal(position));
#endif
}

void LogiFlowWindow::pushCreateDocumentCommand(
    SILICON::project::Document document, const QString& commandText)
{
  const auto path = document.getPath();

  auto addDocument = [this, document] {
    try {
      saveActiveDocumentPayload();
    } catch (const std::exception&) {
    }
    insertDocument(document, std::nullopt, true);
  };

  auto removeDocumentCommand = [this, path] { removeDocument(path); };
  undoStack->push(new ProjectStateCommand(commandText, removeDocumentCommand,
                                          addDocument));
}

void LogiFlowWindow::createCircuit()
{
  createDocument(SILICON::project::DocumentType::Circuit);
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

  std::vector<SILICON::project::DocumentType> codeTypes;
  for (const auto& info : SILICON::project::DOCUMENT_TYPE_INFO) {
    if (SILICON::project::categoryOf(info.type)
        == SILICON::project::DocumentCategory::Code) {
      codeTypes.push_back(info.type);
      typeBox->addItem(QString::fromUtf8(info.displayName));
    }
  }

  form->addRow(tr("Name"), nameEdit);
  form->addRow(tr("File type"), typeBox);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog,
          [this, dialog, nameEdit, typeBox, codeTypes = std::move(codeTypes)] {
            const auto index = typeBox->currentIndex();
            if (index < 0 || index >= static_cast<int>(codeTypes.size()))
              return;

            const auto type = codeTypes[static_cast<std::size_t>(index)];
            const auto name = nameEdit->text().trimmed().toStdString();
            if (!SILICON::project::isValidDocumentSlug(name)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Code File"),
                  tr("The name must be non-empty and cannot contain path separators."));
              return;
            }
            const auto path = SILICON::project::documentPathForSlug(type, name);

            if (SILICON::project::documentTypeForPath(path) != type) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Code File"),
                  tr("The name must be non-empty and cannot contain path separators."));
              return;
            }

            if (SILICON::project::DocumentStore::active().contains(path)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Code File"),
                  tr("A code file named '%1' already exists.")
                      .arg(QString::fromStdString(path)));
              return;
            }

            pushCreateDocumentCommand({path, ""}, tr("Create Code File"));
            dialog->accept();
          });

  nameEdit->selectAll();
  nameEdit->setFocus();
  dialog->open();
}

void LogiFlowWindow::createBinaryFile()
{
  constexpr int MaximumBinarySize = 256 * 1024 * 1024;

  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("New Binary File"));
  dialog->setModal(true);

  auto* form     = new QFormLayout(dialog);
  auto* nameEdit = new QLineEdit(tr("untitled"), dialog);
  auto* sizeEdit = new QSpinBox(dialog);
  sizeEdit->setRange(1, MaximumBinarySize);
  sizeEdit->setValue(256);
  sizeEdit->setSuffix(tr(" bytes"));

  form->addRow(tr("Name"), nameEdit);
  form->addRow(tr("Size"), sizeEdit);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog,
          [this, dialog, nameEdit, sizeEdit] {
            const auto slug = nameEdit->text().trimmed().toStdString();
            if (!SILICON::project::isValidDocumentSlug(slug)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Binary File"),
                  tr("The name must be non-empty and cannot contain path separators."));
              return;
            }

            const auto path = SILICON::project::documentPathForSlug(
                SILICON::project::DocumentType::RawBinary, slug);
            if (SILICON::project::DocumentStore::active().contains(path)) {
              SILICON::ui::inputDialog::warning(
                  this, tr("New Binary File"),
                  tr("A binary file named '%1' already exists.")
                      .arg(QString::fromStdString(path)));
              return;
            }

            pushCreateDocumentCommand(
                {path, std::string(static_cast<std::size_t>(sizeEdit->value()), '\0')},
                tr("Create Binary File"));
            dialog->accept();
          });

  nameEdit->selectAll();
  nameEdit->setFocus();
  dialog->open();
}

void LogiFlowWindow::createDocument(const SILICON::project::DocumentType type)
{
  if (SILICON::project::categoryOf(type)
      != SILICON::project::DocumentCategory::Diagram)
    throw std::invalid_argument("createDocument requires a graphical document type");

  const auto noun = documentTypeName(type);
  SILICON::ui::inputDialog::getText(
      this, tr("New %1").arg(noun), tr("%1 name").arg(noun),
      noun,
      [this, type, noun](const QString& requestedName) {
        const auto trimmed = requestedName.trimmed();
        const auto displayName = trimmed.isEmpty() ? noun : trimmed;
        const auto path = uniqueDocumentPath(type, displayName);
        const auto sceneJson =
            emptyGraphicalDocumentJson(type, displayName.toStdString());

        auto document = preparedSubcircuitDocument(path, sceneJson);

        pushCreateDocumentCommand(std::move(document),
                                  tr("Create %1").arg(noun));
      });
}

void LogiFlowWindow::deleteSelectedDocument()
{
  const auto selection =
      projectTree ? projectTree->selectedDocument() : std::nullopt;
  auto* item = projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selection || !item || selection->path == projectMainCircuitPath())
    return;

  const auto noun  = documentTypeName(selection->type);
  const auto title = tr("Delete %1").arg(noun);

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::warning(
        this, title,
        tr("Failed to save the active document before deleting it:\n%1").arg(e.what()));
    return;
  }

  if (SILICON::project::categoryOf(selection->type)
      == SILICON::project::DocumentCategory::Diagram) {
    const auto dependents = dependencyGraph.dependentsOf(selection->path);
    if (!dependents.empty()) {
      QStringList names;
      for (const auto& dependent : dependents)
        names.push_back(QString::fromStdString(dependent));

      SILICON::ui::inputDialog::warning(
          this, title,
          tr("This circuit is still used by:\n%1").arg(names.join('\n')));
      return;
    }
  }

  SILICON::ui::inputDialog::question(
      this, title,
      tr("Delete %1 \"%2\"?").arg(noun.toLower(), item->text(0)),
      [this, path = selection->path, title] {
        auto& store = SILICON::project::DocumentStore::active();
        const auto* storedDocument = store.find(path);
        const auto storedIndex = store.indexOf(path);
        if (!storedDocument || !storedIndex)
          return;

        const auto document = *storedDocument;
        const auto index = static_cast<std::ptrdiff_t>(*storedIndex);

        auto removeStored = [this, path] { removeDocument(path); };
        auto restoreDocument = [this, document, index] {
          insertDocument(document, index, true);
        };

        undoStack->push(
            new ProjectStateCommand(title, restoreDocument, removeStored));
      });
}

}  // namespace ui
}  // namespace SILICON
