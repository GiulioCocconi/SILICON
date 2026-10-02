/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "diagramInteractionController.hpp"
#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/project/projectSession.hpp>

#include <vector>

#include <QAction>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QWidget>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/circuit/components/subcircuit/componentShapeEditor.hpp>
#include <ui/circuit/components/subcircuit/utils.hpp>
#include <ui/circuit/diagram/diagramView.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/circuit/editor/componentCatalogOverlay.hpp>
#include <ui/serialization/gui_component_factory.hpp>
#include <ui/shell/inputDialogUtils.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

DiagramInteractionController::DiagramInteractionController(
    ProjectSession& session, CircuitEditor& circuit, ComponentCatalogOverlay& catalog,
    QUndoStack& history, QWidget& window)
  : QObject(&window),
    session(session),
    circuit(circuit),
    catalog(catalog),
    undoStack(history),
    window(window)
{
}

void DiagramInteractionController::bindActions(const CircuitActions& actions)
{
  connect(actions.rotate, &QAction::triggered, this,
          &DiagramInteractionController::rotate);
  connect(actions.autoPlace, &QAction::triggered, this,
          &DiagramInteractionController::autoPlace);
  connect(actions.setNormalMode, &QAction::triggered, this,
          &DiagramInteractionController::setNormalMode);
  connect(actions.setPanMode, &QAction::triggered, this,
          &DiagramInteractionController::setPanMode);
  connect(actions.setWireCreationMode, &QAction::triggered, this,
          &DiagramInteractionController::setWireCreationMode);
  connect(actions.setSimulationMode, &QAction::triggered, this,
          &DiagramInteractionController::setSimulationMode);
  connect(actions.setComponentPlacingMode, &QAction::triggered, this,
          &DiagramInteractionController::setComponentPlacingMode);
  connect(actions.cancelInteraction, &QAction::triggered, this,
          &DiagramInteractionController::cancelCurrentInteraction);
  connect(actions.openComponentCatalog, &QAction::triggered, this,
          &DiagramInteractionController::showComponentCatalog);
  connect(actions.editSubcircuitShape, &QAction::triggered, this,
          &DiagramInteractionController::editActiveSubcircuitShape);
}

void DiagramInteractionController::rotate()
{
  std::vector<GraphicalComponent*> selectedComponents;
  for (auto* item : circuit.scene()->selectedItems()) {
    if (auto* component =
            category_cast<GraphicalComponent>(item, ItemCategory::Component)) {
      selectedComponents.push_back(component);
    }
  }

  switch (circuit.scene()->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: {
      if (selectedComponents.size() != 1)
        return;

      auto* component   = selectedComponents.front();
      auto  oldRotation = component->rotation();
      component->rotate();
      auto newRotation = component->rotation();
      component->setInitialRotation();
      auto rotateCmd = new RotateItemCommand(component, oldRotation, newRotation);
      undoStack.push(rotateCmd);
      break;
    }
    case InteractionMode::COMPONENT_PLACING_MODE: {
      circuit.scene()->getComponentToBeDrawn()->rotate();
      break;
    }

    default: return;
  }
}

void DiagramInteractionController::autoPlace()
{
  circuit.scene()->autoPlaceCircuit();
}

void DiagramInteractionController::editActiveSubcircuitShape()
{
  if (SILICON::project::documentTypeForPath(session.activeDocumentPath)
      != SILICON::project::DocumentType::Circuit)
    return;

  const auto slug = SILICON::project::documentSlugForPath(session.activeDocumentPath);
  if (!slug)
    return;

  try {
    circuit.flush(session.activeDocumentPath);
    editGraphicalSubcircuitShape(*slug, session.projectContext, &undoStack, &window);
  } catch (const std::exception& e) {
    inputDialog::warning(
        &window, window.tr("Edit shape"),
        window.tr("Failed to save the active circuit before editing its shape:\n%1")
            .arg(e.what()));
  }
}

void DiagramInteractionController::setNormalMode()
{
  circuit.scene()->setInteractionMode(InteractionMode::NORMAL_MODE);
}

void DiagramInteractionController::setPanMode()
{
  circuit.scene()->setInteractionMode(InteractionMode::PAN_MODE);
}

void DiagramInteractionController::setWireCreationMode()
{
  circuit.scene()->setInteractionMode(InteractionMode::WIRE_CREATION_MODE);
}

void DiagramInteractionController::setSimulationMode()
{
  const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (!type
      || SILICON::project::categoryOf(*type)
             != SILICON::project::DocumentCategory::Diagram)
    return;

  circuit.scene()->setInteractionMode(InteractionMode::SIMULATION_MODE);
}

void DiagramInteractionController::setComponentPlacingMode()
{
  circuit.scene()->setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
}

void DiagramInteractionController::showComponentCatalog()
{
  catalog.setGeometry(circuit.view()->viewport()->rect());
  catalog.open();
}

void DiagramInteractionController::cancelCurrentInteraction()
{
  circuit.scene()->cancelCurrentInteraction();
}

}  // namespace SILICON::ui
