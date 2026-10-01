/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include <core/projectDocument.hpp>
#include <ui/documents/documentNavigator.hpp>
#include <ui/shell/uiUtils.hpp>

class QWidget;
class QUndoStack;
class QPoint;

namespace SILICON::ui {

class ComponentCatalogOverlay;
class EditorWorkspace;
struct ProjectSession;
class ProjectTree;

/** Coordinates document activation, editor payloads, and project tree state. */
class ProjectDocumentController : public QObject, public DocumentNavigator {
  Q_OBJECT

public:
  ProjectDocumentController(ProjectSession& session, EditorWorkspace& workspace,
                            ProjectTree& tree, ComponentCatalogOverlay& catalog,
                            QUndoStack& undoStack, QWidget* dialogParent);

  [[nodiscard]] const std::string& currentDocumentPath() const noexcept override;
  bool                             activateDocument(const std::string& path) override;
  bool switchToDocument(const std::string& path, bool selectInTree);
  void selectDocument(const std::string& path);
  void rebuildTree();

  /**
   * @brief Publishes the active document to observers.
   *
   * Callers that load a document into the workspace themselves use this to announce the
   * resulting activation.
   */
  void notifyActiveDocumentActivated();

  void restoreProjectDocuments(const std::vector<SILICON::project::Document>& documents,
                               const std::string&                             activePath);
  void removeDocument(const std::string& path);
  void insertDocument(SILICON::project::Document    document,
                      std::optional<std::ptrdiff_t> insertAt, bool activate);
  void showArchitectureTabContextMenu(const QPoint& position);
  void createCircuit();
  void createCodeFile();
  void createBinaryFile();
  void createArchitecture();
  void importProjectDocument();
  void exportSelectedDocument();
  void renameSelectedDocument();
  void deleteSelectedDocument();
  void convertActiveDocument();

signals:
  /**
   * @brief Emitted after a document was loaded and made visible.
   *
   * Never emitted for a rejected document, so receivers can rely on the editor showing
   * @p path when they react to it.
   *
   * @param path Project path of the active document
   * @param category Editor category that now owns the document
   */
  void activeDocumentChanged(const QString&                     path,
                             SILICON::project::DocumentCategory category);

  /** @brief Emitted when the set of project documents was added to, changed, or removed.
   */
  void projectDocumentsChanged();

private:
  void createDocument(SILICON::project::DocumentType type);
  void pushCreateDocumentCommand(SILICON::project::Document document,
                                 const QString&             commandText);
  void pushDocumentSnapshotCommand(QString                                 commandText,
                                   std::vector<SILICON::project::Document> before,
                                   std::string                             beforeActive,
                                   std::vector<SILICON::project::Document> after,
                                   std::string                             afterActive);
  void renameArchitecture(const std::string& name);
  void deleteArchitecture(const std::string& name);
  void deleteArchitectureComponent(const std::string& path);
  void removeProjectDocuments(const std::vector<std::string>& paths,
                              const QString&                  commandText);
  void commitDocumentChanges(std::vector<SILICON::project::Document> documents,
                             const std::string&                      sourcePath,
                             const std::string& activatePath, const QString& commandText,
                             const QString& errorTitle);
  void convertActiveDocumentTo(SILICON::project::DocumentType target);

  /** @brief Reports a rejected document activation to the user. */
  void reportLoadFailure(SILICON::project::DocumentType type, const std::string& reason);

  ProjectSession&          session;
  EditorWorkspace&         workspace;
  ProjectTree*             projectTree;
  ComponentCatalogOverlay& catalog;
  QUndoStack*              undoStack;
  QWidget*                 dialogParent;
};

}  // namespace SILICON::ui
