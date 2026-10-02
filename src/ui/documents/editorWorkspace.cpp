/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#include "editorWorkspace.hpp"

#include <stdexcept>

#include <QStackedWidget>
#include <QUndoStack>
#include <QVBoxLayout>

#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/circuit/editor/diagramInteractionController.hpp>
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
}
EditorWorkspace::~EditorWorkspace() = default;

CircuitEditor& EditorWorkspace::circuitEditor()
{
  if (!circuit) {
    circuit = std::make_unique<CircuitEditor>(session, stack);
    stack->addWidget(circuit->widget());
    emit editorCreated(circuit.get());
  }
  return *circuit;
}
CodeDocumentEditor& EditorWorkspace::ensureCodeEditor()
{
  if (!code) {
    code = std::make_unique<CodeDocumentEditor>(session, stack,
                                                [this] { emit historyStateChanged(); });
    stack->addWidget(code->widget());
    emit editorCreated(code.get());
  }
  return *code;
}
BinaryDocumentEditor& EditorWorkspace::ensureBinaryEditor()
{
  if (!binary) {
    binary = std::make_unique<BinaryDocumentEditor>(
        session, stack, [this] { emit historyStateChanged(); });
    stack->addWidget(binary->widget());
    emit editorCreated(binary.get());
  }
  return *binary;
}
ArchitectureDocumentEditor& EditorWorkspace::ensureArchitectureEditor()
{
  if (!architecture) {
    architecture = std::make_unique<ArchitectureDocumentEditor>(
        session, stack, [this] { emit historyStateChanged(); });
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
  projectHistory = history;
  if (!history)
    return;

  // Every editor falls back to the project history when its own document has nothing
  // left to undo, so its state has to be part of the workspace notifications.
  connect(history, &QUndoStack::canUndoChanged, this,
          &EditorWorkspace::historyStateChanged);
  connect(history, &QUndoStack::canRedoChanged, this,
          &EditorWorkspace::historyStateChanged);
  if (circuit)
    circuit->setProjectHistory(history);
  if (code)
    code->setProjectHistory(history);
  if (binary)
    binary->setProjectHistory(history);
  if (architecture)
    architecture->setProjectHistory(history);
}
void EditorWorkspace::setCircuitEditingBackend(
    DiagramInteractionController& interaction) noexcept
{
  circuitEditing = &interaction;
}
EditorEditState EditorWorkspace::activeEditState() const
{
  if (editsActiveCircuit())
    return circuitEditing ? circuitEditing->editState() : EditorEditState{};

  const bool textCommands = activeDocumentEditor && activeDocumentEditor->isEditable()
                            && activeDocumentEditor->hasTextEditingCommands();
  return {.canEditSelection = textCommands, .canPaste = textCommands};
}
bool EditorWorkspace::editsActiveCircuit() const noexcept
{
  return activeDocumentEditor && activeDocumentEditor == circuit.get();
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
  if (!activeDocumentEditor)
    return;
  if (editsActiveCircuit()) {
    if (circuitEditing)
      circuitEditing->copy();
    return;
  }
  activeDocumentEditor->copy();
}
void EditorWorkspace::cutActiveDocument()
{
  if (!activeDocumentEditor)
    return;
  if (editsActiveCircuit()) {
    if (circuitEditing)
      circuitEditing->cut();
    return;
  }
  activeDocumentEditor->cut();
}
void EditorWorkspace::pasteActiveDocument()
{
  if (!activeDocumentEditor)
    return;
  if (editsActiveCircuit()) {
    if (circuitEditing)
      circuitEditing->paste();
    return;
  }
  activeDocumentEditor->paste();
}
void EditorWorkspace::deleteInActiveDocument()
{
  if (!activeDocumentEditor)
    return;
  if (editsActiveCircuit()) {
    if (circuitEditing)
      circuitEditing->del();
    return;
  }
  activeDocumentEditor->deleteSelection();
}
void EditorWorkspace::loadDocument(const SILICON::project::Document& document)
{
  activateDocument(*prepareDocument(document));
}
}  // namespace SILICON::ui
