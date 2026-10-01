/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <memory>
#include <string>

#include <core/projectDocument.hpp>

class QWidget;
class QUndoStack;

namespace SILICON::ui {

/** A detached payload which can be applied after preparation succeeds. */
struct PreparedEditorDocument {
  virtual ~PreparedEditorDocument() = default;
};

/** Operations shared by document editors hosted in EditorWorkspace. */
class DocumentEditor {
public:
  virtual ~DocumentEditor()                              = default;
  [[nodiscard]] virtual QWidget* widget() const noexcept = 0;
  [[nodiscard]] virtual std::shared_ptr<PreparedEditorDocument>
                             prepare(const SILICON::project::Document& document) = 0;
  virtual void               apply(const PreparedEditorDocument& prepared)       = 0;
  virtual void               flush(const std::string& path)                      = 0;
  [[nodiscard]] virtual bool isDirty() const                                     = 0;
  virtual void               resetDirtyState() noexcept                          = 0;
  virtual void               reset()                                             = 0;
  virtual void               undo()                                              = 0;
  virtual void               redo()                                              = 0;
  [[nodiscard]] virtual bool canUndo() const                                     = 0;
  [[nodiscard]] virtual bool canRedo() const                                     = 0;
  virtual void               copy() {}
  virtual void               cut() {}
  virtual void               paste() {}
  virtual void               deleteSelection() {}
  [[nodiscard]] virtual bool isEditable() const { return true; }
  void setProjectHistory(QUndoStack* history) noexcept { projectHistory = history; }

protected:
  QUndoStack* projectHistory = nullptr;
};

}  // namespace SILICON::ui
