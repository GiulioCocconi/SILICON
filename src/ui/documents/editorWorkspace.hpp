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
class DiagramInteractionController;
class DocumentEditor;
struct EditorEditState;
struct PreparedEditorDocument;
struct ProjectSession;

/** Selects and hosts editors while preserving detached document preparation. */
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

  /** @brief Applies the project-wide history shared by every hosted editor. */
  void setProjectHistory(QUndoStack* history) noexcept;

  /**
   * @brief Attaches the backend implementing circuit-only editing commands.
   *
   * Circuit documents are edited through @p interaction instead of the generic
   * document editor interface, which only describes text-like editors.
   */
  void setCircuitEditingBackend(DiagramInteractionController& interaction) noexcept;

  /** @brief Availability of the clipboard, deletion, and rotation commands. */
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

  void editorCreated(DocumentEditor* editor);

private:
  [[nodiscard]] DocumentEditor& editorFor(SILICON::project::DocumentType type);
  CodeDocumentEditor&           ensureCodeEditor();
  BinaryDocumentEditor&         ensureBinaryEditor();
  ArchitectureDocumentEditor&   ensureArchitectureEditor();
  [[nodiscard]] bool            editsActiveCircuit() const noexcept;

  ProjectSession&                             session;
  QStackedWidget*                             stack;
  DocumentEditor*                             activeDocumentEditor = nullptr;
  DiagramInteractionController*               circuitEditing       = nullptr;
  QUndoStack*                                 projectHistory       = nullptr;
  std::unique_ptr<CircuitEditor>              circuit;
  std::unique_ptr<CodeDocumentEditor>         code;
  std::unique_ptr<BinaryDocumentEditor>       binary;
  std::unique_ptr<ArchitectureDocumentEditor> architecture;
};
}  // namespace SILICON::ui
