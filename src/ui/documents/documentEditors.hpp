/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <ui/documents/documentEditor.hpp>

namespace SILICON::ui {

class ArchitectureWorkspace;
class BinaryEditor;
class CodeEditor;
struct ProjectSession;

class CodeDocumentEditor final : public DocumentEditor {
public:
  CodeDocumentEditor(ProjectSession& session, QWidget* parent,
                     EditorNotifications notifications);
  [[nodiscard]] CodeEditor* editor() const noexcept { return codeEditor; }
  [[nodiscard]] QWidget*    widget() const noexcept override;
  [[nodiscard]] std::shared_ptr<PreparedEditorDocument>
                                prepare(const SILICON::project::Document&) override;
  void                          apply(const PreparedEditorDocument&) override;
  void                          flush(const std::string&) override;
  [[nodiscard]] bool            isDirty() const override;
  void                          resetDirtyState() noexcept override;
  void                          reset() override;
  void                          undo() override;
  void                          redo() override;
  [[nodiscard]] bool            canUndo() const override;
  [[nodiscard]] bool            canRedo() const override;
  void                          copy() override;
  void                          cut() override;
  void                          paste() override;
  void                          deleteSelection() override;
  [[nodiscard]] EditorEditState editState() const override;

private:
  ProjectSession& session;
  CodeEditor*     codeEditor;
  bool            dirty = false;
};

class BinaryDocumentEditor final : public DocumentEditor {
public:
  BinaryDocumentEditor(ProjectSession& session, QWidget* parent,
                       EditorNotifications notifications);
  [[nodiscard]] BinaryEditor* editor() const noexcept { return binaryEditor; }
  [[nodiscard]] QWidget*      widget() const noexcept override;
  [[nodiscard]] std::shared_ptr<PreparedEditorDocument>
                     prepare(const SILICON::project::Document&) override;
  void               apply(const PreparedEditorDocument&) override;
  void               flush(const std::string&) override;
  [[nodiscard]] bool isDirty() const override;
  void               resetDirtyState() noexcept override;
  void               reset() override;
  void               undo() override;
  void               redo() override;
  [[nodiscard]] bool canUndo() const override;
  [[nodiscard]] bool canRedo() const override;

private:
  ProjectSession& session;
  BinaryEditor*   binaryEditor;
  bool            dirty = false;
};

class ArchitectureDocumentEditor final : public DocumentEditor {
public:
  ArchitectureDocumentEditor(ProjectSession& session, QWidget* parent,
                             EditorNotifications notifications);
  [[nodiscard]] ArchitectureWorkspace* workspace() const noexcept
  {
    return architectureWorkspace;
  }
  [[nodiscard]] CodeEditor*
                     activeCodeEditor(SILICON::project::DocumentType type) const noexcept;
  [[nodiscard]] bool isVisualizerActive() const noexcept;
  [[nodiscard]] QWidget* widget() const noexcept override;
  [[nodiscard]] std::shared_ptr<PreparedEditorDocument>
                                prepare(const SILICON::project::Document&) override;
  void                          apply(const PreparedEditorDocument&) override;
  void                          flush(const std::string&) override;
  [[nodiscard]] bool            isDirty() const override;
  void                          resetDirtyState() noexcept override;
  void                          reset() override;
  void                          undo() override;
  void                          redo() override;
  [[nodiscard]] bool            canUndo() const override;
  [[nodiscard]] bool            canRedo() const override;
  void                          copy() override;
  void                          cut() override;
  void                          paste() override;
  void                          deleteSelection() override;
  [[nodiscard]] EditorEditState editState() const override;

private:
  ProjectSession&        session;
  ArchitectureWorkspace* architectureWorkspace;
  bool                   dirty = false;
};

}  // namespace SILICON::ui
