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

#include "projectFileController.hpp"

#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QTemporaryFile>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/projectFile.hpp>

#include <logging/logger.hpp>

#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/documents/binary/binaryEditor.hpp>
#include <ui/shell/fileDialogUtils.hpp>
#include <ui/shell/inputDialogUtils.hpp>

#include <ui/documents/code/codeEditor.hpp>

#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectDocumentPolicy.hpp>
#include <ui/project/projectSession.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {
  const SILICON::logging::Logger uiLog("ui");
}  // namespace

ProjectFileController::ProjectFileController(ProjectSession&            session,
                                             EditorWorkspace&           workspace,
                                             ProjectDocumentController& documents,
                                             QUndoStack& undoStack, QWidget* dialogParent)
  : QObject(dialogParent),
    session(session),
    workspace(workspace),
    documents(documents),
    undoStack(undoStack),
    dialogParent(dialogParent)
{
}

void ProjectFileController::setFileName(const QString& fn)
{
  session.currentFileName      = fn;
  const QString displayFileName = QFileInfo(session.currentFileName).fileName();

  if (!displayFileName.isEmpty())
    dialogParent->setWindowTitle(QString("SILICON - %1").arg(displayFileName));
  else
    dialogParent->setWindowTitle("SILICON");
}

void ProjectFileController::newFile()
{
  confirmSaveIfDirty([this] {
    setFileName("");
    resetProjectState();
    this->documents.rebuildTree();
    emit projectChanged();
  });
}

void ProjectFileController::resetProjectState()
{
  workspace.reset();
  session.currentProjectMetadata.reset();
  session.currentProjectInfo =
      projectDocumentPolicy::defaultProjectInfo(session.currentFileName);
  session.activeDocumentPath = projectDocumentPolicy::defaultCircuitPath();
  auto document               = projectDocumentPolicy::defaultCircuitDocument();
  document.setContents(workspace.circuitEditor().scene()->serialize());
  session.projectContext.setDocuments({std::move(document)});
  documents.notifyActiveDocumentActivated();
}

void ProjectFileController::loadProjectContent(const QString&    fileName,
                                               const QByteArray& fileContent)
{
  uiLog.info(std::format("Opening {}", fileName.toStdString()));

  try {
    QTemporaryFile archive;
    if (!archive.open() || archive.write(fileContent) != fileContent.size()
        || !archive.flush())
      throw std::runtime_error("Cannot stage the selected project archive");

    const QString archivePath = archive.fileName();
    archive.close();

    auto projectFile = SILICON::project::readProjectFile(archivePath.toStdString());

    session.currentProjectMetadata = std::move(projectFile.metadata);
    workspace.reset();
    session.currentProjectInfo = std::move(projectFile.project);
    session.projectContext.setDocuments(std::move(projectFile.documents));
    const auto initialCircuit = session.firstCircuitPath();
    if (!initialCircuit)
      throw std::runtime_error("Project has no circuit document");
    session.activeDocumentPath = *initialCircuit;

    const auto* document =
        session.projectContext.documents().find(session.activeDocumentPath);
    if (!document)
      throw std::runtime_error("Initial circuit payload is missing");

    workspace.loadDocument(*document);
    documents.notifyActiveDocumentActivated();
    setFileName(fileName);
    this->documents.rebuildTree();
    emit projectChanged();
  } catch (const nlohmann::json::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent, tr("Corrupted File"),
        tr("The project contains invalid JSON data:\n%1").arg(e.what()));
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent, tr("Load Error"),
        tr("Failed to load the project:\n%1").arg(e.what()));
  }
}

void ProjectFileController::open()
{
  confirmSaveIfDirty([this] {
    SILICON::ui::fileDialog::openFileContent(
        dialogParent, tr("Open Project"), tr("SILICON Project (*.sil);;All Files (*)"),
        [this](const QString& fileName, const QByteArray& fileContent) {
          loadProjectContent(fileName, fileContent);
        });
  });
}

bool ProjectFileController::save()
{
  try {
    workspace.flushActiveDocument();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent, tr("Save Error"),
        tr("Failed to serialize the active circuit:\n%1").arg(e.what()));
    return false;
  }

  QString destinationFileName = session.currentFileName;
#ifndef __EMSCRIPTEN__
  if (destinationFileName.isEmpty()) {
    destinationFileName =
        QFileDialog::getSaveFileName(dialogParent, tr("Save Project"), QString(),
                                     tr("SILICON Project (*.sil);;All Files (*)"));
    if (destinationFileName.isEmpty())
      return false;
  }
#else
  if (destinationFileName.isEmpty())
    destinationFileName = QStringLiteral("project.sil");
#endif

  try {
    auto metadata =
        session.currentProjectMetadata.value_or(SILICON::project::metadataForNewFile());
    metadata.formatVersion  = SILICON::project::FORMAT_VERSION;
    metadata.siliconVersion = SILICON_VERSION;
    metadata.lastModify     = SILICON::project::currentUtcTimestamp();

    auto project = session.currentProjectInfo.value_or(SILICON::project::ProjectInfo{});
    if (project.name.empty())
      project.name = QFileInfo(destinationFileName).baseName().toStdString();
    session.currentProjectInfo = project;
    projectDocumentPolicy::ensureProjectDocuments(session.projectContext);
    const auto documents = session.projectContext.documents().getDocuments();
    SILICON::project::ProjectFile projectFile{
        .metadata = metadata, .project = project, .documents = documents};

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
        dialogParent, tr("Save Project"), destinationFileName,
        tr("SILICON Project (*.sil);;All Files (*)"), archiveFile.readAll());
    if (!savedFileName)
      return false;
    setFileName(*savedFileName);
#else
    SILICON::project::writeProjectFile(destinationFileName.toStdString(), projectFile);
    setFileName(destinationFileName);
#endif
    session.currentProjectMetadata = std::move(metadata);
    this->documents.rebuildTree();
    undoStack.setClean();
    workspace.resetEditorDirtyState();
    return true;
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent, tr("Save Error"),
        tr("Failed to save the project:\n%1").arg(e.what()));
    return false;
  }
}

void ProjectFileController::confirmSaveIfDirty(std::function<void()> continuation)
{
  if (!workspace.hasUnsavedChanges()) {
    continuation();
    return;
  }

  SILICON::ui::inputDialog::warningChoice(
      dialogParent, tr("Unsaved Changes"),
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

}  // namespace SILICON::ui
