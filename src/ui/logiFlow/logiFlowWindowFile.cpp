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

#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QStackedWidget>
#include <QTemporaryFile>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/projectFile.hpp>
#include <logging/logger.hpp>
#include <ui/common/binaryEditor.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/logiFlow/code/codeEditor.hpp>

namespace SILICON {
namespace ui {
  using namespace SILICON::core;

  namespace {
    const SILICON::logging::Logger uiLog("ui");
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
    loadedArchitectureName.clear();
    currentProjectMetadata.reset();
    currentProjectInfo   = defaultProjectInfo(currentFileName);
    activeDocumentPath   = defaultCircuitPath();
    codeDocumentsDirty   = false;
    binaryDocumentsDirty = false;
    codeEditor->clearFileType();
    binaryEditor->setData({});
    editorStack->setCurrentWidget(diagramView);
    diagramScene->clear();
    auto circuit = std::make_shared<Circuit>();
    diagramScene->setDocumentCircuit(std::move(circuit));
    diagramScene->setSubcircuitDocumentMode(false);
    auto document = defaultCircuitDocument();
    document.setContents(diagramScene->serialize());
    projectContext.setDocuments({std::move(document)});
    updateSubcircuitShapeAction();
  }

  void LogiFlowWindow::loadCircuitContent(const QString&    fileName,
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

      currentProjectMetadata = std::move(projectFile.metadata);
      loadedArchitectureName.clear();
      currentProjectInfo     = std::move(projectFile.project);
      codeDocumentsDirty     = false;
      binaryDocumentsDirty   = false;
      binaryEditor->setData({});
      projectContext.setDocuments(std::move(projectFile.documents));
      const auto initialCircuit = firstCircuitPath();
      if (!initialCircuit)
        throw std::runtime_error("Project has no circuit document");
      activeDocumentPath = *initialCircuit;

      const auto* document = projectContext.documents().find(activeDocumentPath);
      if (!document)
        throw std::runtime_error("Initial circuit payload is missing");

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
      currentProjectInfo = project;
      ensureProjectDocuments();
      const auto                    documents = projectContext.documents().getDocuments();
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
      codeDocumentsDirty   = false;
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
        [this, continuation = std::move(continuation)](
            const SILICON::ui::inputDialog::Choice choice) {
          if (choice == SILICON::ui::inputDialog::Choice::Cancel)
            return;
          if (choice == SILICON::ui::inputDialog::Choice::Primary && !save())
            return;

          continuation();
        });
  }


}  // namespace ui
}  // namespace SILICON
