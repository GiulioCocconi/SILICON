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
    session_(session),
    workspace_(workspace),
    documents_(documents),
    undoStack_(undoStack),
    dialogParent_(dialogParent)
{
}

void ProjectFileController::setFileName(const QString& fn)
{
  session_.currentFileName      = fn;
  const QString displayFileName = QFileInfo(session_.currentFileName).fileName();

  if (!displayFileName.isEmpty())
    dialogParent_->setWindowTitle(QString("SILICON - %1").arg(displayFileName));
  else
    dialogParent_->setWindowTitle("SILICON");
}

void ProjectFileController::newFile()
{
  confirmSaveIfDirty([this] {
    setFileName("");
    resetProjectState();
    documents_.rebuildTree();
    emit projectChanged();
  });
}

void ProjectFileController::resetProjectState()
{
  workspace_.reset();
  session_.currentProjectMetadata.reset();
  session_.currentProjectInfo =
      projectDocumentPolicy::defaultProjectInfo(session_.currentFileName);
  session_.activeDocumentPath = projectDocumentPolicy::defaultCircuitPath();
  auto document               = projectDocumentPolicy::defaultCircuitDocument();
  document.setContents(workspace_.scene()->serialize());
  session_.projectContext.setDocuments({std::move(document)});
  documents_.notifyActiveDocumentActivated();
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

    session_.currentProjectMetadata = std::move(projectFile.metadata);
    workspace_.reset();
    session_.currentProjectInfo = std::move(projectFile.project);
    session_.projectContext.setDocuments(std::move(projectFile.documents));
    const auto initialCircuit = session_.firstCircuitPath();
    if (!initialCircuit)
      throw std::runtime_error("Project has no circuit document");
    session_.activeDocumentPath = *initialCircuit;

    const auto* document =
        session_.projectContext.documents().find(session_.activeDocumentPath);
    if (!document)
      throw std::runtime_error("Initial circuit payload is missing");

    workspace_.loadDocument(*document);
    documents_.notifyActiveDocumentActivated();
    setFileName(fileName);
    documents_.rebuildTree();
    emit projectChanged();
  } catch (const nlohmann::json::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent_, tr("Corrupted File"),
        tr("The project contains invalid JSON data:\n%1").arg(e.what()));
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent_, tr("Load Error"),
        tr("Failed to load the project:\n%1").arg(e.what()));
  }
}

void ProjectFileController::open()
{
  confirmSaveIfDirty([this] {
    SILICON::ui::fileDialog::openFileContent(
        dialogParent_, tr("Open Project"), tr("SILICON Project (*.sil);;All Files (*)"),
        [this](const QString& fileName, const QByteArray& fileContent) {
          loadProjectContent(fileName, fileContent);
        });
  });
}

bool ProjectFileController::save()
{
  try {
    workspace_.flushActiveDocument();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent_, tr("Save Error"),
        tr("Failed to serialize the active circuit:\n%1").arg(e.what()));
    return false;
  }

  QString destinationFileName = session_.currentFileName;
#ifndef __EMSCRIPTEN__
  if (destinationFileName.isEmpty()) {
    destinationFileName =
        QFileDialog::getSaveFileName(dialogParent_, tr("Save Project"), QString(),
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
        session_.currentProjectMetadata.value_or(SILICON::project::metadataForNewFile());
    metadata.formatVersion  = SILICON::project::FORMAT_VERSION;
    metadata.siliconVersion = SILICON_VERSION;
    metadata.lastModify     = SILICON::project::currentUtcTimestamp();

    auto project = session_.currentProjectInfo.value_or(SILICON::project::ProjectInfo{});
    if (project.name.empty())
      project.name = QFileInfo(destinationFileName).baseName().toStdString();
    session_.currentProjectInfo = project;
    projectDocumentPolicy::ensureProjectDocuments(session_.projectContext);
    const auto documents = session_.projectContext.documents().getDocuments();
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
        dialogParent_, tr("Save Project"), destinationFileName,
        tr("SILICON Project (*.sil);;All Files (*)"), archiveFile.readAll());
    if (!savedFileName)
      return false;
    setFileName(*savedFileName);
#else
    SILICON::project::writeProjectFile(destinationFileName.toStdString(), projectFile);
    setFileName(destinationFileName);
#endif
    session_.currentProjectMetadata = std::move(metadata);
    documents_.rebuildTree();
    undoStack_.setClean();
    workspace_.resetEditorDirtyState();
    return true;
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::critical(
        dialogParent_, tr("Save Error"),
        tr("Failed to save the project:\n%1").arg(e.what()));
    return false;
  }
}

void ProjectFileController::confirmSaveIfDirty(std::function<void()> continuation)
{
  if (!workspace_.hasUnsavedChanges()) {
    continuation();
    return;
  }

  SILICON::ui::inputDialog::warningChoice(
      dialogParent_, tr("Unsaved Changes"),
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
