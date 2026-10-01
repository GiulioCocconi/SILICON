/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <memory>

#include <QStackedWidget>

#include <core/projectDocument.hpp>

class QUndoStack;

namespace SILICON::ui {

class ArchitectureWorkspace;
class BinaryEditor;
class CodeEditor;
class DiagramScene;
class DiagramView;
struct ProjectSession;

/** Owns the document editor widgets and the scene they share. */
class EditorWorkspace : public QStackedWidget {
public:
  /** @brief Document deserialized ahead of time, detached from the editor widgets. */
  class PreparedDocument;

  explicit EditorWorkspace(ProjectSession& session, QWidget* parent = nullptr);
  ~EditorWorkspace() override;

  [[nodiscard]] DiagramScene* scene() const noexcept { return diagramScene; }
  [[nodiscard]] DiagramView*  view() const noexcept { return diagramView; }
  [[nodiscard]] CodeEditor*   codeEditor() const noexcept { return codeEditor_; }
  [[nodiscard]] BinaryEditor* binaryEditor() const noexcept { return binaryEditor_; }
  [[nodiscard]] ArchitectureWorkspace* architectureWorkspace() const noexcept
  {
    return architectureWorkspace_;
  }
  void setUndoStack(QUndoStack* stack) noexcept { undoStack = stack; }
  [[nodiscard]] CodeEditor* activeCodeEditor() const noexcept;
  [[nodiscard]] bool        isVisualizerActive() const noexcept;
  [[nodiscard]] bool        hasUnsavedChanges() const;
  void                      resetEditorDirtyState() noexcept;
  void                      reset();
  void                      undoActiveDocument();
  void                      redoActiveDocument();
  void                      flushActiveDocument();

  /**
   * @brief Deserializes @p document without disturbing the current editor state.
   *
   * Circuit payloads are deserialized into a detached scene plan and architecture
   * documents into a detached tab plan, so an invalid document can be rejected while
   * the previous document stays loaded and visible.
   *
   * @param document Document that should become active
   * @return A plan consumed by @ref activateDocument
   * @throws std::exception when the document payload is invalid or unsupported
   */
  [[nodiscard]] std::shared_ptr<PreparedDocument>
  prepareDocument(const SILICON::project::Document& document);

  /**
   * @brief Replaces the editor contents with a document prepared beforehand.
   *
   * @param prepared Plan produced by @ref prepareDocument
   * @throws std::exception when the prepared document cannot be shown
   */
  void activateDocument(const PreparedDocument& prepared);

  void loadDocument(const SILICON::project::Document& document);

private:
  ProjectSession&        session;
  QUndoStack*            undoStack             = nullptr;
  DiagramScene*          diagramScene                 = nullptr;
  DiagramView*           diagramView                  = nullptr;
  CodeEditor*            codeEditor_            = nullptr;
  BinaryEditor*          binaryEditor_          = nullptr;
  ArchitectureWorkspace* architectureWorkspace_ = nullptr;
  bool                   codeDocumentsDirty_    = false;
  bool                   binaryDocumentsDirty_  = false;
};

}  // namespace SILICON::ui
