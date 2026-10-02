/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "projectDocumentController.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <QSignalBlocker>
#include <QWidget>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/circuit/editor/componentCatalogOverlay.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentPolicy.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/project/projectTree.hpp>
#include <ui/shell/inputDialogUtils.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

ProjectDocumentController::ProjectDocumentController(
    ProjectSession& session, EditorWorkspace& workspace, ProjectTree& tree,
    ComponentCatalogOverlay& catalog, QUndoStack& history, QWidget* dialogParent)
  : QObject(dialogParent),
    session(session),
    workspace(workspace),
    projectTree(&tree),
    catalog(catalog),
    undoStack(&history),
    dialogParent(dialogParent)
{
}

const std::string& ProjectDocumentController::currentDocumentPath() const noexcept
{
  return session.activeDocumentPath;
}

bool ProjectDocumentController::activateDocument(const std::string& documentPath)
{
  if (!SILICON::project::documentTypeForPath(documentPath)
      || !session.projectContext.documents().contains(documentPath))
    return false;

  if (documentPath == session.activeDocumentPath) {
    selectDocument(documentPath);
    return true;
  }

  return switchToDocument(documentPath, true);
}

void ProjectDocumentController::restoreProjectDocuments(
    const std::vector<SILICON::project::Document>& documents,
    const std::string&                             activePath)
{
  const auto active =
      std::ranges::find(documents, activePath, &SILICON::project::Document::getPath);
  if (active == documents.end())
    throw std::runtime_error("Project-state command has no active document");

  // Circuit payloads resolve their subcircuits against the project store, so the snapshot
  // has to be published before it is deserialized. Both the store and the active path are
  // put back when the snapshot cannot be loaded, leaving the workspace on its document.
  const auto previousDocuments = session.projectContext.documents().getDocuments();
  const auto previousActive    = session.activeDocumentPath;

  session.projectContext.setDocuments(documents);
  session.activeDocumentPath = activePath;
  try {
    const auto prepared = workspace.prepareDocument(*active);
    workspace.activateDocument(*prepared);
  } catch (...) {
    session.projectContext.setDocuments(previousDocuments);
    session.activeDocumentPath = previousActive;
    throw;
  }

  rebuildTree();
  selectDocument(activePath);
  emit projectDocumentsChanged();
  notifyActiveDocumentActivated();
}

void ProjectDocumentController::notifyActiveDocumentActivated()
{
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  const auto category = type ? SILICON::project::categoryOf(*type)
                             : SILICON::project::DocumentCategory::Code;
  emit       activeDocumentChanged(QString::fromStdString(session.activeDocumentPath),
                                   category);
}

void ProjectDocumentController::selectDocument(const std::string& path)
{
  projectTree->selectDocument(path);
}

void ProjectDocumentController::reportLoadFailure(
    const SILICON::project::DocumentType type, const std::string& reason)
{
  const auto noun = documentTypeName(type);
  SILICON::ui::inputDialog::critical(
      dialogParent, tr("%1 Switch Error").arg(noun),
      tr("Failed to load the selected %1:\n%2")
          .arg(noun.toLower(), QString::fromStdString(reason)));
}

bool ProjectDocumentController::switchToDocument(const std::string& path,
                                                 const bool         selectInTree)
{
  const auto& store  = session.projectContext.documents();
  const auto* target = store.find(path);
  if (!target)
    return false;

  if (path == session.activeDocumentPath) {
    if (selectInTree)
      selectDocument(path);
    return true;
  }

  const auto type = target->getType();
  catalog.hide();

  if (workspace.circuitEditor().scene()->getInteractionMode()
      != InteractionMode::NORMAL_MODE)
    workspace.circuitEditor().scene()->setInteractionMode(InteractionMode::NORMAL_MODE);

  try {
    workspace.flushActiveDocument();
  } catch (const std::exception& e) {
    const auto noun = documentTypeName(type);
    SILICON::ui::inputDialog::warning(
        dialogParent, tr("%1 Switch Error").arg(noun),
        tr("Failed to save the current document before switching:\n%1").arg(e.what()));
    return false;
  }

  target = store.find(path);
  if (!target)
    return false;

  // The incoming document is deserialized while detached from the workspace, so a
  // malformed payload leaves the current document loaded and the active path untouched.
  std::shared_ptr<EditorWorkspace::PreparedDocument> prepared;
  try {
    prepared = workspace.prepareDocument(*target);
  } catch (const std::exception& e) {
    reportLoadFailure(type, e.what());
    return false;
  }

  const auto previousPath     = session.activeDocumentPath;
  session.activeDocumentPath = path;
  try {
    workspace.activateDocument(*prepared);
  } catch (const std::exception& e) {
    session.activeDocumentPath = previousPath;
    reportLoadFailure(type, e.what());
    return false;
  }

  if (selectInTree)
    selectDocument(path);

  notifyActiveDocumentActivated();
  return true;
}

void ProjectDocumentController::removeDocument(const std::string& path)
{
  const auto& store    = session.projectContext.documents();
  const auto* document = store.find(path);
  if (!document)
    return;

  if (session.activeDocumentPath == path) {
    const auto fallback = session.firstCircuitPath(path);
    if (!fallback || !switchToDocument(*fallback, true))
      return;
  }

  session.projectContext.removeDocument(path);
  rebuildTree();
  selectDocument(session.activeDocumentPath);
  emit projectDocumentsChanged();
}

void ProjectDocumentController::insertDocument(
    SILICON::project::Document document, const std::optional<std::ptrdiff_t> insertAt,
    const bool activate)
{
  const auto& store = session.projectContext.documents();
  const auto  path  = document.getPath();
  if (store.contains(path))
    return;

  if (insertAt)
    session.projectContext.insertDocument(
        std::move(document),
        static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, *insertAt)));
  else
    session.projectContext.upsertDocument(std::move(document));

  rebuildTree();
  emit projectDocumentsChanged();
  if (activate)
    switchToDocument(path, true);
}

void ProjectDocumentController::rebuildTree()
{
  projectDocumentPolicy::ensureProjectDocuments(session.projectContext);
  const auto project = session.currentProjectInfo.value_or(
      projectDocumentPolicy::defaultProjectInfo(session.currentFileName));
  const auto& store = session.projectContext.documents();
  projectTree->rebuild(project, store.getDocuments(), session.activeDocumentPath);
}

}  // namespace SILICON::ui
