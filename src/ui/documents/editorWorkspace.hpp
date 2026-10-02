/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#pragma once

#include <memory>

#include <QWidget>

#include <core/projectDocument.hpp>

class QStackedWidget;
class QUndoStack;

namespace SILICON::ui {
class ArchitectureDocumentEditor;
class BinaryDocumentEditor;
class CircuitEditor;
class CodeDocumentEditor;
class DocumentEditor;
struct EditorEditState;
struct EditorNotifications;
struct PreparedEditorDocument;
struct ProjectSession;

/**
 * @brief Selects and hosts editors while preserving detached document preparation.
 *
 * Single source of truth for the active editor's action state: editor-local changes
 * arrive through EditorNotifications, and the global clipboard is observed once here,
 * so consumers only ever observe this workspace's signals.
 */
class EditorWorkspace : public QWidget {
  Q_OBJECT
public:
  class PreparedDocument;

  explicit EditorWorkspace(ProjectSession& session, QWidget* parent = nullptr);
  ~EditorWorkspace() override;

  CircuitEditor&                circuitEditor();
  [[nodiscard]] DocumentEditor* activeEditor() const noexcept
  {
    return activeDocumentEditor;
  }
  [[nodiscard]] bool hasUnsavedChanges() const;

  /**
   * @brief Applies the project-wide history shared by every hosted editor.
   *
   * Replacing the history re-points every editor and drops the previous stack
   * subscriptions; passing nullptr detaches the editors from any history.
   */
  void setProjectHistory(QUndoStack* history) noexcept;

  /** @brief Which generic editing commands the active editor currently accepts. */
  [[nodiscard]] EditorEditState activeEditState() const;

  [[nodiscard]] bool canUndoActiveDocument() const;
  [[nodiscard]] bool canRedoActiveDocument() const;
  void               resetEditorDirtyState() noexcept;
  void               reset();
  void               undoActiveDocument();
  void               redoActiveDocument();
  void               flushActiveDocument();
  void               copyActiveDocument();
  void               cutActiveDocument();
  void               pasteActiveDocument();
  void               deleteInActiveDocument();
  [[nodiscard]] std::shared_ptr<PreparedDocument>
       prepareDocument(const SILICON::project::Document& document);
  void activateDocument(const PreparedDocument& prepared);
  void loadDocument(const SILICON::project::Document& document);

signals:
  void activeEditorChanged();

  /**
   * @brief Emitted when the active editor's undo/redo availability may have changed.
   *
   * Aggregates the history notifications of every hosted editor, of the project-wide
   * undo stack, and of the architecture tabs, so observers never have to subscribe to
   * an individual editor.
   */
  void historyStateChanged();

  /**
   * @brief Emitted when the active editor's editing availability may have changed.
   *
   * Aggregates the selection-driven and mode-driven changes reported by every hosted
   * editor through EditorNotifications, plus the clipboard changes observed above, so
   * observers never have to subscribe to an individual editor or to the clipboard.
   */
  void editStateChanged();

  void editorCreated(DocumentEditor* editor);

private:
  [[nodiscard]] DocumentEditor&     editorFor(SILICON::project::DocumentType type);
  CodeDocumentEditor&               ensureCodeEditor();
  BinaryDocumentEditor&             ensureBinaryEditor();
  ArchitectureDocumentEditor&       ensureArchitectureEditor();
  [[nodiscard]] EditorNotifications notifications();

  ProjectSession&                             session;
  QStackedWidget*                             stack;
  DocumentEditor*                             activeDocumentEditor = nullptr;
  QUndoStack*                                 projectHistory       = nullptr;
  std::unique_ptr<CircuitEditor>              circuit;
  std::unique_ptr<CodeDocumentEditor>         code;
  std::unique_ptr<BinaryDocumentEditor>       binary;
  std::unique_ptr<ArchitectureDocumentEditor> architecture;
};
}  // namespace SILICON::ui
