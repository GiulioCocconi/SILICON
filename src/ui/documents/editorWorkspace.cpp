/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "editorWorkspace.hpp"

#include <memory>
#include <stdexcept>

#include <QByteArray>
#include <QTextDocument>
#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>

#include <ui/circuit/components/subcircuit/metadata.hpp>
#include <ui/circuit/components/subcircuit/utils.hpp>
#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/scene/diagramSceneSerializer.hpp>
#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/binary/binaryEditor.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

/** @brief Document deserialized ahead of the editor widgets that will show it. */
class EditorWorkspace::PreparedDocument {
public:
  PreparedDocument(const SILICON::project::DocumentType documentType, std::string path)
    : type(documentType), path(std::move(path))
  {
  }

  /** @brief Document type selecting the editor that shows the payload. */
  SILICON::project::DocumentType type;
  /** @brief Project path the payload was read from. */
  std::string path;
  /** @brief Detached scene plan, set for circuit documents. */
  std::shared_ptr<SceneLoadPlan> scene;
  /** @brief Detached tab plan, set for architecture documents. */
  std::shared_ptr<ArchitectureLoadPlan> architecture;
};

EditorWorkspace::EditorWorkspace(ProjectSession& session, QWidget* parent)
  : QStackedWidget(parent), session_(session)
{
  scene_ = new DiagramScene(this);
  scene_->setDocumentStore(&session.projectContext.documents());
  scene_->setCircuitResolver(&session.circuitResolver);
  view_ = new DiagramView(this);
  view_->setScene(scene_);

  codeEditor_ = new CodeEditor(this);
  connect(codeEditor_->document(), &QTextDocument::modificationChanged, this,
          [this](const bool modified) {
            if (modified && codeEditor_->fileType())
              codeDocumentsDirty_ = true;
          });

  binaryEditor_ = new BinaryEditor(this);
  connect(binaryEditor_->history(), &QUndoStack::cleanChanged, this, [this](bool clean) {
    const auto type = SILICON::project::documentTypeForPath(session_.activeDocumentPath);
    if (!clean && type
        && SILICON::project::categoryOf(*type)
               == SILICON::project::DocumentCategory::Binary)
      binaryDocumentsDirty_ = true;
  });

  architectureWorkspace_ = new ArchitectureWorkspace(this);
  connect(architectureWorkspace_, &ArchitectureWorkspace::documentModified, this,
          [this] { codeDocumentsDirty_ = true; });

  addWidget(view_);
  addWidget(codeEditor_);
  addWidget(architectureWorkspace_);
  addWidget(binaryEditor_);
  setCurrentWidget(view_);
}

EditorWorkspace::~EditorWorkspace()
{
  // Graphical items unsubscribe from the project store during scene teardown.
  view_->setScene(nullptr);
  delete scene_;
}

CodeEditor* EditorWorkspace::activeCodeEditor() const noexcept
{
  const auto type = SILICON::project::documentTypeForPath(session_.activeDocumentPath);
  return type
                 && SILICON::project::categoryOf(*type)
                        == SILICON::project::DocumentCategory::Architecture
             ? architectureWorkspace_->editor(*type)
             : codeEditor_;
}

void EditorWorkspace::flushActiveDocument()
{
  if (session_.activeDocumentPath.empty()) {
    const auto fallback = session_.firstCircuitPath();
    if (!fallback)
      throw std::runtime_error("Project has no circuit document");
    session_.activeDocumentPath = *fallback;
  }

  const auto& store          = session_.projectContext.documents();
  const auto* activeDocument = store.find(session_.activeDocumentPath);
  if (!activeDocument)
    throw std::runtime_error("Active project document is missing");

  const auto type = activeDocument->getType();
  if (SILICON::project::categoryOf(type)
      == SILICON::project::DocumentCategory::Architecture) {
    auto* editor = architectureWorkspace_->editor(type);
    if (!editor)
      throw std::invalid_argument("Architecture component has no editor");
    session_.projectContext.upsertDocument(
        {session_.activeDocumentPath, editor->toPlainText().toStdString()});
    editor->document()->setModified(false);
    return;
  }
  switch (type) {
    case SILICON::project::DocumentType::Verilog:
      session_.projectContext.upsertDocument(
          {session_.activeDocumentPath, codeEditor_->toPlainText().toStdString()});
      codeEditor_->document()->setModified(false);
      return;

    case SILICON::project::DocumentType::RawBinary: {
      const auto& data = binaryEditor_->data();
      session_.projectContext.upsertDocument(
          {session_.activeDocumentPath,
           std::string(data.constData(), static_cast<std::size_t>(data.size()))});
      binaryEditor_->setModified(false);
      return;
    }

    case SILICON::project::DocumentType::Circuit: break;
    default: throw std::logic_error("Unsupported project document type");
  }

  auto serializedScene = scene_->serialize();

  if (type == SILICON::project::DocumentType::Circuit) {
    if (const auto* existing = store.find(session_.activeDocumentPath)) {
      try {
        auto       newJson  = nlohmann::json::parse(serializedScene);
        const auto fallback = parseGraphicalSubcircuitMetadata(existing->getContents())
                                  .value_or(GraphicalSubcircuitMetadata{});
        newJson["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
            synchronizeGraphicalSubcircuitMetadata(serializedScene, fallback));
        serializedScene = newJson.dump(2);
      } catch (const nlohmann::json::exception&) {
      }
    }
  }

  session_.projectContext.upsertDocument(SILICON::project::Document(
      session_.activeDocumentPath, std::move(serializedScene)));
}

void EditorWorkspace::loadDocument(const SILICON::project::Document& document)
{
  activateDocument(*prepareDocument(document));
}

std::shared_ptr<EditorWorkspace::PreparedDocument>
EditorWorkspace::prepareDocument(const SILICON::project::Document& document)
{
  const auto type = document.getType();

  // The document type selects the editor that owns the payload, so an unsupported type
  // is rejected before anything is replaced.
  if (SILICON::project::categoryOf(type)
      == SILICON::project::DocumentCategory::Architecture) {
    auto prepared          = std::make_shared<PreparedDocument>(type, document.getPath());
    prepared->architecture = architectureWorkspace_->prepareLoadPlan(
        document, session_.projectContext.documents());
    return prepared;
  }

  switch (type) {
    case SILICON::project::DocumentType::Verilog:
    case SILICON::project::DocumentType::RawBinary:
      return std::make_shared<PreparedDocument>(type, document.getPath());

    case SILICON::project::DocumentType::Circuit: {
      auto prepared   = std::make_shared<PreparedDocument>(type, document.getPath());
      prepared->scene = scene_->prepareDeserialize(document.getContents(),
                                                   GUIComponentFactory::instance(),
                                                   ComponentRegistry::instance());
      return prepared;
    }
    default: throw std::logic_error("Unsupported project document type");
  }
}

void EditorWorkspace::activateDocument(const PreparedDocument& prepared)
{
  const auto type = prepared.type;

  if (SILICON::project::categoryOf(type)
      == SILICON::project::DocumentCategory::Architecture) {
    if (!prepared.architecture)
      throw std::logic_error("Prepared architecture document has no load plan");
    architectureWorkspace_->applyLoadPlan(*prepared.architecture);
    setCurrentWidget(architectureWorkspace_);
    return;
  }

  switch (type) {
    case SILICON::project::DocumentType::Verilog: {
      const auto* document = session_.projectContext.documents().find(prepared.path);
      if (!document)
        throw std::runtime_error("Prepared document is no longer available");
      codeEditor_->setFileType(type);
      codeEditor_->setPlainText(QString::fromStdString(document->getContents()));
      codeEditor_->document()->setModified(false);
      this->setCurrentWidget(codeEditor_);
      codeEditor_->setFocus();
      break;
    }

    case SILICON::project::DocumentType::RawBinary: {
      const auto* document = session_.projectContext.documents().find(prepared.path);
      if (!document)
        throw std::runtime_error("Prepared document is no longer available");
      const auto& contents = document->getContents();
      binaryEditor_->setData(
          QByteArray(contents.data(), static_cast<qsizetype>(contents.size())));
      this->setCurrentWidget(binaryEditor_);
      binaryEditor_->setFocus();
      break;
    }

    case SILICON::project::DocumentType::Circuit: {
      if (!prepared.scene)
        throw std::logic_error("Prepared circuit document has no load plan");
      scene_->clear(false, false);
      scene_->setSubcircuitDocumentMode(true);
      scene_->applyDeserialize(prepared.scene);
      this->setCurrentWidget(view_);
      break;
    }
    default: throw std::logic_error("Unsupported project document type");
  }
}

bool EditorWorkspace::isVisualizerActive() const noexcept
{
  return currentWidget() == architectureWorkspace_
         && architectureWorkspace_->isVisualizerActive();
}

bool EditorWorkspace::hasUnsavedChanges() const
{
  return (undoStack_ && !undoStack_->isClean()) || codeDocumentsDirty_
         || binaryDocumentsDirty_
         || (codeEditor_ && codeEditor_->document()->isModified())
         || architectureWorkspace_->hasModifiedEditors()
         || (binaryEditor_ && binaryEditor_->isModified());
}

void EditorWorkspace::reset()
{
  architectureWorkspace_->resetLoadedArchitecture();
  resetEditorDirtyState();
  codeEditor_->clearFileType();
  binaryEditor_->setData({});
  setCurrentWidget(view_);
  scene_->clear();
  scene_->setDocumentCircuit(std::make_shared<Circuit>());
  scene_->setSubcircuitDocumentMode(false);
}

void EditorWorkspace::resetEditorDirtyState() noexcept
{
  codeDocumentsDirty_   = false;
  binaryDocumentsDirty_ = false;
}

void EditorWorkspace::undoActiveDocument()
{
  if (isVisualizerActive())
    return;
  const auto type = SILICON::project::documentTypeForPath(session_.activeDocumentPath);
  if (type
      && SILICON::project::categoryOf(*type) == SILICON::project::DocumentCategory::Binary
      && binaryEditor_->history()->canUndo())
    binaryEditor_->history()->undo();
  else if (type && SILICON::project::isCodeDocument(*type)
           && activeCodeEditor()->document()->isUndoAvailable())
    activeCodeEditor()->undo();
  else if (undoStack_)
    undoStack_->undo();
}

void EditorWorkspace::redoActiveDocument()
{
  if (isVisualizerActive())
    return;
  const auto type = SILICON::project::documentTypeForPath(session_.activeDocumentPath);
  if (type
      && SILICON::project::categoryOf(*type) == SILICON::project::DocumentCategory::Binary
      && binaryEditor_->history()->canRedo())
    binaryEditor_->history()->redo();
  else if (type && SILICON::project::isCodeDocument(*type)
           && activeCodeEditor()->document()->isRedoAvailable())
    activeCodeEditor()->redo();
  else if (undoStack_)
    undoStack_->redo();
}

}  // namespace SILICON::ui
