/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "circuitEditor.hpp"

#include <stdexcept>

#include <QUndoStack>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/circuit/components/subcircuit/metadata.hpp>
#include <ui/circuit/components/subcircuit/utils.hpp>
#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/scene/diagramSceneSerializer.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
namespace {
  struct PreparedCircuit final : PreparedEditorDocument {
    std::shared_ptr<SceneLoadPlan> plan;
  };
}  // namespace

CircuitEditor::CircuitEditor(ProjectSession& session, QWidget* parent)
  : session(session),
    diagramScene(new DiagramScene(parent)),
    diagramView(new DiagramView(parent))
{
  diagramScene->setDocumentStore(&session.projectContext.documents());
  diagramScene->setCircuitResolver(&session.circuitResolver);
  diagramView->setScene(diagramScene);
}

CircuitEditor::~CircuitEditor()
{
  diagramView->setScene(nullptr);
  delete diagramScene;
}

QWidget* CircuitEditor::widget() const noexcept
{
  return diagramView;
}

std::shared_ptr<PreparedEditorDocument>
CircuitEditor::prepare(const SILICON::project::Document& document)
{
  auto prepared  = std::make_shared<PreparedCircuit>();
  prepared->plan = diagramScene->prepareDeserialize(
      document.getContents(), GUIComponentFactory::instance(),
      SILICON::core::ComponentRegistry::instance());
  return prepared;
}

void CircuitEditor::apply(const PreparedEditorDocument& prepared)
{
  const auto* circuit = dynamic_cast<const PreparedCircuit*>(&prepared);
  if (!circuit || !circuit->plan)
    throw std::logic_error("Prepared circuit document has no load plan");
  diagramScene->clear(false, false);
  diagramScene->setSubcircuitDocumentMode(true);
  diagramScene->applyDeserialize(circuit->plan);
}

void CircuitEditor::flush(const std::string& path)
{
  auto serialized = diagramScene->serialize();
  if (const auto* existing = session.projectContext.documents().find(path)) {
    try {
      auto       json     = nlohmann::json::parse(serialized);
      const auto fallback = parseGraphicalSubcircuitMetadata(existing->getContents())
                                .value_or(GraphicalSubcircuitMetadata{});
      json["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
          synchronizeGraphicalSubcircuitMetadata(serialized, fallback));
      serialized = json.dump(2);
    } catch (const nlohmann::json::exception&) {
    }
  }
  session.projectContext.upsertDocument({path, std::move(serialized)});
}

bool CircuitEditor::isDirty() const
{
  return undoStack && !undoStack->isClean();
}
void CircuitEditor::resetDirtyState() noexcept {}
void CircuitEditor::reset()
{
  diagramScene->clear();
  diagramScene->setDocumentCircuit(std::make_shared<SILICON::core::Circuit>());
  diagramScene->setSubcircuitDocumentMode(false);
}
void CircuitEditor::undo()
{
  if (undoStack)
    undoStack->undo();
}
void CircuitEditor::redo()
{
  if (undoStack)
    undoStack->redo();
}
bool CircuitEditor::canUndo() const
{
  return undoStack && undoStack->canUndo();
}
bool CircuitEditor::canRedo() const
{
  return undoStack && undoStack->canRedo();
}

}  // namespace SILICON::ui
