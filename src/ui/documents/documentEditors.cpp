/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "documentEditors.hpp"

#include <stdexcept>

#include <QTabWidget>
#include <QTextDocument>
#include <QUndoStack>

#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/binary/binaryEditor.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/project/projectSession.hpp>

namespace SILICON::ui {
namespace {
  struct PreparedText final : PreparedEditorDocument {
    SILICON::project::DocumentType type;
    std::string                    contents;
  };
  struct PreparedBinary final : PreparedEditorDocument {
    std::string contents;
  };
  struct PreparedArchitecture final : PreparedEditorDocument {
    std::shared_ptr<ArchitectureLoadPlan> plan;
  };
}  // namespace

CodeDocumentEditor::CodeDocumentEditor(ProjectSession& session, QWidget* parent,
                                       std::function<void()> historyChanged)
  : session(session), codeEditor(new CodeEditor(parent))
{
  QObject::connect(codeEditor->document(), &QTextDocument::modificationChanged,
                   codeEditor, [this](bool modified) {
                     if (modified && codeEditor->fileType())
                       dirty = true;
                   });
  QObject::connect(codeEditor, &QPlainTextEdit::undoAvailable, codeEditor,
                   [historyChanged](bool) { historyChanged(); });
  QObject::connect(codeEditor, &QPlainTextEdit::redoAvailable, codeEditor,
                   [historyChanged](bool) { historyChanged(); });
}
QWidget* CodeDocumentEditor::widget() const noexcept
{
  return codeEditor;
}
std::shared_ptr<PreparedEditorDocument>
CodeDocumentEditor::prepare(const SILICON::project::Document& document)
{
  auto result      = std::make_shared<PreparedText>();
  result->type     = document.getType();
  result->contents = document.getContents();
  return result;
}
void CodeDocumentEditor::apply(const PreparedEditorDocument& prepared)
{
  const auto* text = dynamic_cast<const PreparedText*>(&prepared);
  if (!text)
    throw std::logic_error("Invalid prepared code document");
  codeEditor->setFileType(text->type);
  codeEditor->setPlainText(QString::fromStdString(text->contents));
  codeEditor->document()->setModified(false);
  codeEditor->setFocus();
}
void CodeDocumentEditor::flush(const std::string& path)
{
  session.projectContext.upsertDocument({path, codeEditor->toPlainText().toStdString()});
  codeEditor->document()->setModified(false);
}
bool CodeDocumentEditor::isDirty() const
{
  return dirty || codeEditor->document()->isModified();
}
void CodeDocumentEditor::resetDirtyState() noexcept
{
  dirty = false;
}
void CodeDocumentEditor::reset()
{
  codeEditor->clearFileType();
  resetDirtyState();
}
void CodeDocumentEditor::undo()
{
  if (codeEditor->document()->isUndoAvailable())
    codeEditor->undo();
  else if (projectHistory)
    projectHistory->undo();
}
void CodeDocumentEditor::redo()
{
  if (codeEditor->document()->isRedoAvailable())
    codeEditor->redo();
  else if (projectHistory)
    projectHistory->redo();
}
bool CodeDocumentEditor::canUndo() const
{
  return codeEditor->document()->isUndoAvailable()
         || (projectHistory && projectHistory->canUndo());
}
bool CodeDocumentEditor::canRedo() const
{
  return codeEditor->document()->isRedoAvailable()
         || (projectHistory && projectHistory->canRedo());
}
void CodeDocumentEditor::copy()
{
  codeEditor->copy();
}
void CodeDocumentEditor::cut()
{
  codeEditor->cut();
}
void CodeDocumentEditor::paste()
{
  codeEditor->paste();
}
void CodeDocumentEditor::deleteSelection()
{
  auto cursor = codeEditor->textCursor();
  if (cursor.hasSelection())
    cursor.removeSelectedText();
  else
    cursor.deleteChar();
  codeEditor->setTextCursor(cursor);
}
bool CodeDocumentEditor::hasTextEditingCommands() const
{
  return true;
}

BinaryDocumentEditor::BinaryDocumentEditor(ProjectSession& session, QWidget* parent,
                                           std::function<void()> historyChanged)
  : session(session), binaryEditor(new BinaryEditor(parent))
{
  QObject::connect(binaryEditor->history(), &QUndoStack::cleanChanged, binaryEditor,
                   [this](bool clean) {
                     const auto type = SILICON::project::documentTypeForPath(
                         this->session.activeDocumentPath);
                     if (!clean && type
                         && SILICON::project::categoryOf(*type)
                                == SILICON::project::DocumentCategory::Binary)
                       dirty = true;
                   });
  QObject::connect(binaryEditor->history(), &QUndoStack::canUndoChanged, binaryEditor,
                   [historyChanged](bool) { historyChanged(); });
  QObject::connect(binaryEditor->history(), &QUndoStack::canRedoChanged, binaryEditor,
                   [historyChanged](bool) { historyChanged(); });
}
QWidget* BinaryDocumentEditor::widget() const noexcept
{
  return binaryEditor;
}
std::shared_ptr<PreparedEditorDocument>
BinaryDocumentEditor::prepare(const SILICON::project::Document& document)
{
  auto result      = std::make_shared<PreparedBinary>();
  result->contents = document.getContents();
  return result;
}
void BinaryDocumentEditor::apply(const PreparedEditorDocument& prepared)
{
  const auto* binary = dynamic_cast<const PreparedBinary*>(&prepared);
  if (!binary)
    throw std::logic_error("Invalid prepared binary document");
  binaryEditor->setData(QByteArray(binary->contents.data(),
                                   static_cast<qsizetype>(binary->contents.size())));
  binaryEditor->setFocus();
}
void BinaryDocumentEditor::flush(const std::string& path)
{
  const auto& data = binaryEditor->data();
  session.projectContext.upsertDocument(
      {path, std::string(data.constData(), static_cast<std::size_t>(data.size()))});
  binaryEditor->setModified(false);
}
bool BinaryDocumentEditor::isDirty() const
{
  return dirty || binaryEditor->isModified();
}
void BinaryDocumentEditor::resetDirtyState() noexcept
{
  dirty = false;
}
void BinaryDocumentEditor::reset()
{
  binaryEditor->setData({});
  resetDirtyState();
}
void BinaryDocumentEditor::undo()
{
  if (binaryEditor->history()->canUndo())
    binaryEditor->history()->undo();
  else if (projectHistory)
    projectHistory->undo();
}
void BinaryDocumentEditor::redo()
{
  if (binaryEditor->history()->canRedo())
    binaryEditor->history()->redo();
  else if (projectHistory)
    projectHistory->redo();
}
bool BinaryDocumentEditor::canUndo() const
{
  return binaryEditor->history()->canUndo()
         || (projectHistory && projectHistory->canUndo());
}
bool BinaryDocumentEditor::canRedo() const
{
  return binaryEditor->history()->canRedo()
         || (projectHistory && projectHistory->canRedo());
}

ArchitectureDocumentEditor::ArchitectureDocumentEditor(
    ProjectSession& session, QWidget* parent, std::function<void()> historyChanged)
  : session(session), architectureWorkspace(new ArchitectureWorkspace(parent))
{
  QObject::connect(architectureWorkspace, &ArchitectureWorkspace::documentModified,
                   architectureWorkspace, [this] { dirty = true; });
  for (const auto& [type, editor] : architectureWorkspace->editors()) {
    QObject::connect(editor, &QPlainTextEdit::undoAvailable, architectureWorkspace,
                     [historyChanged](bool) { historyChanged(); });
    QObject::connect(editor, &QPlainTextEdit::redoAvailable, architectureWorkspace,
                     [historyChanged](bool) { historyChanged(); });
  }
  // Selecting a tab swaps the editor owning the architecture document, and the visualizer
  // tab replaces editing altogether, so both can change what can be undone.
  QObject::connect(architectureWorkspace, &QTabWidget::currentChanged,
                   architectureWorkspace, [historyChanged](int) { historyChanged(); });
}
QWidget* ArchitectureDocumentEditor::widget() const noexcept
{
  return architectureWorkspace;
}
std::shared_ptr<PreparedEditorDocument>
ArchitectureDocumentEditor::prepare(const SILICON::project::Document& document)
{
  auto result  = std::make_shared<PreparedArchitecture>();
  result->plan = architectureWorkspace->prepareLoadPlan(
      document, session.projectContext.documents());
  return result;
}
void ArchitectureDocumentEditor::apply(const PreparedEditorDocument& prepared)
{
  const auto* architecture = dynamic_cast<const PreparedArchitecture*>(&prepared);
  if (!architecture || !architecture->plan)
    throw std::logic_error("Prepared architecture document has no load plan");
  architectureWorkspace->applyLoadPlan(*architecture->plan);
}
void ArchitectureDocumentEditor::flush(const std::string& path)
{
  const auto type   = SILICON::project::documentTypeForPath(path);
  auto*      editor = type ? architectureWorkspace->editor(*type) : nullptr;
  if (!editor)
    throw std::invalid_argument("Architecture component has no editor");
  session.projectContext.upsertDocument({path, editor->toPlainText().toStdString()});
  editor->document()->setModified(false);
}
bool ArchitectureDocumentEditor::isDirty() const
{
  return dirty || architectureWorkspace->hasModifiedEditors();
}
void ArchitectureDocumentEditor::resetDirtyState() noexcept
{
  dirty = false;
}
void ArchitectureDocumentEditor::reset()
{
  architectureWorkspace->resetLoadedArchitecture();
  resetDirtyState();
}
CodeEditor* ArchitectureDocumentEditor::activeCodeEditor(
    SILICON::project::DocumentType type) const noexcept
{
  return architectureWorkspace->editor(type);
}
bool ArchitectureDocumentEditor::isVisualizerActive() const noexcept
{
  return architectureWorkspace->isVisualizerActive();
}
bool ArchitectureDocumentEditor::isEditable() const
{
  return !isVisualizerActive();
}
bool ArchitectureDocumentEditor::hasTextEditingCommands() const
{
  return !isVisualizerActive();
}
void ArchitectureDocumentEditor::undo()
{
  if (isVisualizerActive())
    return;
  const auto type   = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  auto*      editor = type ? activeCodeEditor(*type) : nullptr;
  if (editor && editor->document()->isUndoAvailable())
    editor->undo();
  else if (projectHistory)
    projectHistory->undo();
}
void ArchitectureDocumentEditor::redo()
{
  if (isVisualizerActive())
    return;
  const auto type   = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  auto*      editor = type ? activeCodeEditor(*type) : nullptr;
  if (editor && editor->document()->isRedoAvailable())
    editor->redo();
  else if (projectHistory)
    projectHistory->redo();
}
bool ArchitectureDocumentEditor::canUndo() const
{
  const auto type   = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  auto*      editor = type ? activeCodeEditor(*type) : nullptr;
  return !isVisualizerActive()
         && ((editor && editor->document()->isUndoAvailable())
             || (projectHistory && projectHistory->canUndo()));
}
bool ArchitectureDocumentEditor::canRedo() const
{
  const auto type   = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  auto*      editor = type ? activeCodeEditor(*type) : nullptr;
  return !isVisualizerActive()
         && ((editor && editor->document()->isRedoAvailable())
             || (projectHistory && projectHistory->canRedo()));
}
void ArchitectureDocumentEditor::copy()
{
  if (!isVisualizerActive())
    if (auto* editor = architectureWorkspace->currentWidget())
      if (auto* code = dynamic_cast<CodeEditor*>(editor))
        code->copy();
}
void ArchitectureDocumentEditor::cut()
{
  if (!isVisualizerActive())
    if (auto* code = dynamic_cast<CodeEditor*>(architectureWorkspace->currentWidget()))
      code->cut();
}
void ArchitectureDocumentEditor::paste()
{
  if (!isVisualizerActive())
    if (auto* code = dynamic_cast<CodeEditor*>(architectureWorkspace->currentWidget()))
      code->paste();
}
void ArchitectureDocumentEditor::deleteSelection()
{
  if (isVisualizerActive())
    return;
  auto* editor = dynamic_cast<CodeEditor*>(architectureWorkspace->currentWidget());
  if (!editor)
    return;
  auto cursor = editor->textCursor();
  if (cursor.hasSelection())
    cursor.removeSelectedText();
  else
    cursor.deleteChar();
  editor->setTextCursor(cursor);
}

}  // namespace SILICON::ui
