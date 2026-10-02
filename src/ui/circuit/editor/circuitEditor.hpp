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

class DiagramScene;
class DiagramView;
struct ProjectSession;

/** Owns the circuit scene and its view. */
class CircuitEditor final : public DocumentEditor {
public:
  CircuitEditor(ProjectSession& session, QWidget* parent,
                EditorNotifications notifications);
  ~CircuitEditor() override;

  [[nodiscard]] DiagramScene* scene() const noexcept { return diagramScene; }
  [[nodiscard]] DiagramView*  view() const noexcept { return diagramView; }

  [[nodiscard]] QWidget* widget() const noexcept override;
  [[nodiscard]] std::shared_ptr<PreparedEditorDocument>
                     prepare(const SILICON::project::Document& document) override;
  void               apply(const PreparedEditorDocument& prepared) override;
  void               flush(const std::string& path) override;
  [[nodiscard]] bool isDirty() const override;
  void               resetDirtyState() noexcept override;
  void               reset() override;
  void               undo() override;
  void               redo() override;
  [[nodiscard]] bool canUndo() const override;
  [[nodiscard]] bool canRedo() const override;
  void               copy() override;
  void               cut() override;
  void               paste() override;
  void               deleteSelection() override;

  /**
   * @brief Reports which editing commands the diagram currently accepts.
   *
   * Availability follows the scene: the interaction mode, the current selection, and
   * the clipboard contents this editor put there or can deserialize.
   */
  [[nodiscard]] EditorEditState editState() const override;

private:
  bool copySelectionToClipboard();

  ProjectSession& session;
  DiagramScene*   diagramScene;
  DiagramView*    diagramView;
};

}  // namespace SILICON::ui
