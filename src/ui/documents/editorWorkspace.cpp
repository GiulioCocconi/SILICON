/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#include "editorWorkspace.hpp"

#include <stdexcept>

#include <QApplication>
#include <QClipboard>
#include <QStackedWidget>
#include <QUndoStack>
#include <QVBoxLayout>

#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/documents/documentEditors.hpp>
#include <ui/project/projectSession.hpp>

namespace SILICON::ui {
class EditorWorkspace::PreparedDocument {
public:
  PreparedDocument(DocumentEditor&                         editor,
                   std::shared_ptr<PreparedEditorDocument> payload)
    : editor(&editor), payload(std::move(payload))
  {
  }
  DocumentEditor*                         editor;
  std::shared_ptr<PreparedEditorDocument> payload;
};

EditorWorkspace::EditorWorkspace(ProjectSession& session, QWidget* parent)
  : QWidget(parent), session(session), stack(new QStackedWidget(this))
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack);
  activeDocumentEditor = &circuitEditor();
  connect(this, &EditorWorkspace::editorCreated, this,
          [this](DocumentEditor* editor) { editor->setProjectHistory(projectHistory); });
  // The clipboard is global while the editors answering editState() are local, so it is
  // observed exactly once here. The active editor still decides what its contents mean.
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
          &EditorWorkspace::editStateChanged);
}
EditorWorkspace::~EditorWorkspace() = default;

EditorNotifications EditorWorkspace::notifications()
{
  return {[this] { emit historyStateChanged(); }, [this] { emit editStateChanged(); }};
}

CircuitEditor& EditorWorkspace::circuitEditor()
{
  if (!circuit) {
    circuit = std::make_unique<CircuitEditor>(session, stack, notifications());
    stack->addWidget(circuit->widget());
    emit editorCreated(circuit.get());
  }
  return *circuit;
}
CodeDocumentEditor& EditorWorkspace::ensureCodeEditor()
{
  if (!code) {
    code = std::make_unique<CodeDocumentEditor>(session, stack, notifications());
    stack->addWidget(code->widget());
    emit editorCreated(code.get());
  }
  return *code;
}
BinaryDocumentEditor& EditorWorkspace::ensureBinaryEditor()
{
  if (!binary) {
    binary = std::make_unique<BinaryDocumentEditor>(session, stack, notifications());
    stack->addWidget(binary->widget());
    emit editorCreated(binary.get());
  }
  return *binary;
}
ArchitectureDocumentEditor& EditorWorkspace::ensureArchitectureEditor()
{
  if (!architecture) {
    architecture =
        std::make_unique<ArchitectureDocumentEditor>(session, stack, notifications());
    stack->addWidget(architecture->widget());
    emit editorCreated(architecture.get());
  }
  return *architecture;
}
DocumentEditor& EditorWorkspace::editorFor(SILICON::project::DocumentType type)
{
  using SILICON::project::DocumentCategory;
  switch (SILICON::project::categoryOf(type)) {
    case DocumentCategory::Diagram: return circuitEditor();
    case DocumentCategory::Code: return ensureCodeEditor();
    case DocumentCategory::Binary: return ensureBinaryEditor();
    case DocumentCategory::Architecture: return ensureArchitectureEditor();
  }
  throw std::logic_error("Unsupported project document type");
}
void EditorWorkspace::setProjectHistory(QUndoStack* history) noexcept
{
  if (projectHistory == history)
    return;
  if (projectHistory)
    disconnect(projectHistory, nullptr, this, nullptr);

  projectHistory = history;
  if (projectHistory) {
    // Every editor falls back to the project history when its own document has nothing
    // left to undo, so its state has to be part of the workspace notifications.
    connect(projectHistory, &QUndoStack::canUndoChanged, this,
            &EditorWorkspace::historyStateChanged);
    connect(projectHistory, &QUndoStack::canRedoChanged, this,
            &EditorWorkspace::historyStateChanged);
  }
  if (circuit)
    circuit->setProjectHistory(projectHistory);
  if (code)
    code->setProjectHistory(projectHistory);
  if (binary)
    binary->setProjectHistory(projectHistory);
  if (architecture)
    architecture->setProjectHistory(projectHistory);
}
EditorEditState EditorWorkspace::activeEditState() const
{
  return activeDocumentEditor ? activeDocumentEditor->editState() : EditorEditState{};
}
bool EditorWorkspace::hasUnsavedChanges() const
{
  return (circuit && circuit->isDirty()) || (code && code->isDirty())
         || (binary && binary->isDirty()) || (architecture && architecture->isDirty());
}
bool EditorWorkspace::canUndoActiveDocument() const
{
  return activeDocumentEditor && activeDocumentEditor->canUndo();
}
bool EditorWorkspace::canRedoActiveDocument() const
{
  return activeDocumentEditor && activeDocumentEditor->canRedo();
}
void EditorWorkspace::resetEditorDirtyState() noexcept
{
  if (circuit)
    circuit->resetDirtyState();
  if (code)
    code->resetDirtyState();
  if (binary)
    binary->resetDirtyState();
  if (architecture)
    architecture->resetDirtyState();
}
void EditorWorkspace::reset()
{
  if (architecture)
    architecture->reset();
  if (code)
    code->reset();
  if (binary)
    binary->reset();
  circuitEditor().reset();
  stack->setCurrentWidget(circuit->widget());
  activeDocumentEditor = circuit.get();
  emit activeEditorChanged();
}
void EditorWorkspace::undoActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->undo();
}
void EditorWorkspace::redoActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->redo();
}
void EditorWorkspace::flushActiveDocument()
{
  if (session.activeDocumentPath.empty()) {
    const auto fallback = session.firstCircuitPath();
    if (!fallback)
      throw std::runtime_error("Project has no circuit document");
    session.activeDocumentPath = *fallback;
  }
  const auto* document =
      session.projectContext.documents().find(session.activeDocumentPath);
  if (!document)
    throw std::runtime_error("Active project document is missing");
  if (!activeDocumentEditor)
    throw std::logic_error("No active editor");
  activeDocumentEditor->flush(session.activeDocumentPath);
}
std::shared_ptr<EditorWorkspace::PreparedDocument>
EditorWorkspace::prepareDocument(const SILICON::project::Document& document)
{
  auto& editor = editorFor(document.getType());
  return std::make_shared<PreparedDocument>(editor, editor.prepare(document));
}
void EditorWorkspace::activateDocument(const PreparedDocument& prepared)
{
  if (!prepared.editor || !prepared.payload)
    throw std::logic_error("Prepared document has no editor payload");
  prepared.editor->apply(*prepared.payload);
  stack->setCurrentWidget(prepared.editor->widget());
  activeDocumentEditor = prepared.editor;
  emit activeEditorChanged();
}
void EditorWorkspace::copyActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->copy();
}
void EditorWorkspace::cutActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->cut();
}
void EditorWorkspace::pasteActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->paste();
}
void EditorWorkspace::deleteInActiveDocument()
{
  if (activeDocumentEditor)
    activeDocumentEditor->deleteSelection();
}
void EditorWorkspace::loadDocument(const SILICON::project::Document& document)
{
  activateDocument(*prepareDocument(document));
}
}  // namespace SILICON::ui
