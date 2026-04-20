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

#include "diagramScene.hpp"

#include <memory>
#include <stdexcept>

#include <QDebug>

#include "ui/common/graphicalWire.hpp"
#include "ui/logiFlow/components/graphicalGates.hpp"
#include "ui/logiFlow/components/graphicalIO.hpp"
#include "ui/logiFlow/components/graphicalUtils.hpp"
#include "ui/serialization/gui_component_factory.hpp"

DiagramScene::DiagramScene(QObject* parent) : QGraphicsScene(parent)
{
  setInteractionMode(InteractionMode::NORMAL_MODE, true);

  csb = new ComponentSearchBox(GUIComponentFactory::instance().availableTypes());
  csb->setParent(this);
  connect(csb, &ComponentSearchBox::requestHide, this, &DiagramScene::hideCSB);
  connect(csb, &ComponentSearchBox::selectedComponent, this,
          &DiagramScene::placeComponent);

  // Bind the topology callback to support Live-Editing during Simulation Mode
  wireManager.setTopologyChangedCallback([this]() {
    if (this->getInteractionMode() == InteractionMode::SIMULATION_MODE) {
      // 1. Recalculate collisions to apply bus changes to Logic Components
      this->calculateWiresForComponents();

      // (The Circuit instance's interactive observer will automatically catch the IO
      // changes
      //  and instruct the Simulator to recompile its execution blocks.)

      // 2. Refresh graphical output states
      this->refreshGraphicalOutputs();

      // 3. Force a visual repaint so wires update their simulation colors
      this->update();
    }
  });
}

QPointF DiagramScene::snapToGrid(const QPointF point)
{
  const auto x = round(point.x() / DiagramScene::GRID_SIZE) * DiagramScene::GRID_SIZE;
  const auto y = round(point.y() / DiagramScene::GRID_SIZE) * DiagramScene::GRID_SIZE;

  return {x, y};
}

void DiagramScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  // Draw the grid to help with components alignment

  QPen pen;
  painter->setPen(pen);

  const qreal left = int(rect.left()) - (int(rect.left()) % DiagramScene::GRID_SIZE);
  const qreal top  = int(rect.top()) - (int(rect.top()) % DiagramScene::GRID_SIZE);

  QVector<QPointF> points;
  for (qreal x = left; x < rect.right(); x += DiagramScene::GRID_SIZE) {
    for (qreal y = top; y < rect.bottom(); y += DiagramScene::GRID_SIZE) {
      points.append(QPointF(x, y));
    }
  }
  painter->drawPoints(points.data(), points.size());
}

void DiagramScene::setInteractionMode(InteractionMode mode)
{
  setInteractionMode(mode, false);
}


void DiagramScene::setInteractionMode(const InteractionMode newMode, const bool force)
{
  if (!force && views().size() != 1)
    throw std::logic_error("setInteractionMode: scene must have exactly one view");

  const auto currentMode = getInteractionMode();
  if (currentMode == newMode && !force)
    return;

  // Deselect all items
  clearSelection();

  if (wireSegmentToBeDrawn && newMode != InteractionMode::WIRE_CREATION_MODE) {
    finalizeWireCreation();
  }

  if (currentMode == InteractionMode::COMPONENT_PLACING_MODE) {
    exitComponentPlacingMode();
  } else if (newMode == InteractionMode::COMPONENT_PLACING_MODE) {
    enterComponentPlacingMode();
  }

  if (newMode == InteractionMode::SIMULATION_MODE) {
    enterSimulationMode();
  }

  if (currentMode == InteractionMode::SIMULATION_MODE) {
    exitSimulationMode();
  }

  this->currentInteractionMode = newMode;
  emit DiagramScene::modeChanged(newMode);
}

void DiagramScene::finalizeWireCreation()
{
  // Remove the wireSegment if it's invisible
  if (wireSegmentToBeDrawn->empty()) {
    removeItem(wireSegmentToBeDrawn);
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  } else {
    // Always register the segment so the WireManager tracks it for junctions
    // and collision detection
    wireManager.addSegment(wireSegmentToBeDrawn);
  }
  clearWireShadow();
}

void DiagramScene::exitComponentPlacingMode()
{
  hideCSB();
  if (componentToBeDrawn) {
    // componentToBeDrawn shadow should have been cleared BEFORE switching to another
    // mode, if it's not then it means the insertion has been aborted and the component
    // should be removed

    removeItem(componentToBeDrawn);
    delete componentToBeDrawn;
    componentToBeDrawn = nullptr;
  }
}

void DiagramScene::enterComponentPlacingMode()
{
  // ALGORITHM: If the cursor is inside the view then try to place the component.
  //            If the it's outside or the component boundingRect is colliding then
  //            go to component placing mode and place it manually.
  //            When the component is placed then go to component placing mode and
  //            repeat the placing of the same component until ESC is pressed
  //            (NORMAL_MODE)

  const QPoint globalCursorPos = QCursor::pos();

  const auto view = this->views()[0];

  // The position for the CSB should be the position of the cursor (if the cursor is
  // inside the view) or the center of the view (every other case)

  // Get center pos
  const QPoint centerPos = view->viewport()->rect().center();

  // Get cursor pos within view
  const QPoint cursorPosWithinView = view->mapFromGlobal(globalCursorPos);

  const bool isCursorInsideView =
      view->viewport()->rect().contains(cursorPosWithinView);

  const QPoint posForCSB = isCursorInsideView ? cursorPosWithinView : centerPos;

  showCSB(view->mapToScene(posForCSB));
}

void DiagramScene::enterSimulationMode()
{
  calculateWiresForComponents();

  // Initialize all wires to UNKNOWN state (will be overridden by connected inputs)
  for (const auto& wire : wireManager.wires()) {
    wire->clearBusState();
  }

  // Restore inputs to neutral state & inject LOW into the logic bus
  // Do this BEFORE creating the Simulator to avoid double-evaluations
  for (auto* item : items()) {
    if (item && item->type() == SiliconTypes::SINGLE_INPUT) {
      auto* input = qgraphicsitem_cast<GraphicalInput*>(item);

      // Updates visual shape
      input->setState(State::LOW);

      // Manually push the LOW state directly to the logic bus
      auto bus = input->getComponent()->getOutputs()[0];
      bus.forceSetCurrentValue(0, input->getComponent()->weak_from_this());

      // Now connect the signal for user clicks during runtime
      connect(input, &GraphicalInput::inputToggled, this,
              &DiagramScene::handleInputToggled, Qt::UniqueConnection);
    }
  }

  // 2. Gather core components
  Component_set coreComps;
  for (auto* item : items()) {
    if (item && item->type() >= COMPONENT) {
      auto* comp = qgraphicsitem_cast<GraphicalLogicComponent*>(item);
      if (comp && comp->getComponent()) {
        coreComps.insert(comp->getComponent());
      }
    }
  }

  // 3. Initialize Circuit & Simulator frameworks
  this->circuit   = std::make_shared<Circuit>(coreComps, false);

  // The Simulator constructor automatically calls recompile() and evaluates the
  // entire circuit exactly once, using the LOW logic values we just injected!
  this->simulator = std::make_unique<Simulator>(this->circuit);

  refreshGraphicalOutputs();
  update();
}

void DiagramScene::exitSimulationMode()
{
  // Disconnect signals from inputs
  for (auto* item : items()) {
    if (item && item->type() == SiliconTypes::SINGLE_INPUT) {
      auto* input = qgraphicsitem_cast<GraphicalInput*>(item);
      disconnect(input, &GraphicalInput::inputToggled, this,
                 &DiagramScene::handleInputToggled);
    }
  }

  // Discard simulation engines
  this->simulator.reset();
  this->circuit.reset();

  for (auto* item : items()) {
    if (item && item->type() == SiliconTypes::SINGLE_OUTPUT) {
      auto* out = qgraphicsitem_cast<GraphicalOutputSingle*>(item);
      if (out)
        out->setState(State::UNKNOWN);
    }
  }

  // Clear bus states to reset visual wire colors
  for (const auto& wire : wireManager.wires()) {
    wire->clearBusState();
  }

  update();
}


void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* mouseEvent)
{
  const QPointF cursorPos = DiagramScene::snapToGrid(mouseEvent->scenePos());

  switch (currentInteractionMode) {
    case InteractionMode::COMPONENT_PLACING_MODE: {
      if (!componentToBeDrawn)
        break;

      componentToBeDrawn->setPos(cursorPos);
      break;
    }
    case InteractionMode::WIRE_CREATION_MODE: {
      // Let's wait the user to start drawing the wire
      if (!wireSegmentToBeDrawn)
        break;

      /* Calculate path to the cursor */

      const QPointF lp =
          wireSegmentToBeDrawn->mapToScene(wireSegmentToBeDrawn->lastPoint());

      const QPointF displacement    = cursorPos - lp;
      const QPointF intermediatePos = (displacement.x() >= displacement.y())
                                          ? QPointF(cursorPos.x(), lp.y())
                                          : QPointF(lp.x(), cursorPos.y());

      std::vector<QPointF> pointsToBeAdded = {intermediatePos, cursorPos};

      // Remove duplicates (if cursorPos is reachable moving only in one direction)
      pointsToBeAdded.erase(std::ranges::unique(pointsToBeAdded).begin(),
                            pointsToBeAdded.end());

      wireSegmentToBeDrawn->setShowPoints(pointsToBeAdded);
      break;
    }

    case InteractionMode::NORMAL_MODE:
    case InteractionMode::PAN_MODE:
    case InteractionMode::SIMULATION_MODE: break;
    default: throw std::logic_error("Unhandled InteractionMode in mouseMoveEvent");
  }
  QGraphicsScene::mouseMoveEvent(mouseEvent);
}


void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent)
{
  const QPointF cursorPos = DiagramScene::snapToGrid(mouseEvent->scenePos());

  switch (currentInteractionMode) {
    case InteractionMode::NORMAL_MODE: break;
    case InteractionMode::COMPONENT_PLACING_MODE: {
      if (componentToBeDrawn) {
        const auto rotation = componentToBeDrawn->rotation();

        clearComponentShadow();

        // Propose the placing of the next component
        placeComponent(lastPlacedComponentType);
        if (!componentToBeDrawn)
          throw std::logic_error(
              "componentToBeDrawn should not be null after placeComponent succeeds");
        componentToBeDrawn->setRotation(rotation);
      }
      break;
    }
    case InteractionMode::PAN_MODE: break;
    case InteractionMode::WIRE_CREATION_MODE: {
      if (!wireSegmentToBeDrawn) {
        // Let's start drawing the wire!
        wireSegmentToBeDrawn = new GraphicalWireSegment(cursorPos);
        addItem(wireSegmentToBeDrawn);
      } else {
        wireSegmentToBeDrawn->addPoints();
      }
      break;
    }
    case InteractionMode::SIMULATION_MODE: {
      auto itemsAtPos = items(cursorPos);

      for (auto item : itemsAtPos) {
        if (item && item->type() == SiliconTypes::SINGLE_INPUT) {
          auto* input = qgraphicsitem_cast<GraphicalInput*>(item);
          input->toggle();
        }
      }

      break;
    }
    default: throw std::logic_error("Unhandled InteractionMode in mousePressEvent");
  }
  QGraphicsScene::mousePressEvent(mouseEvent);
}

void DiagramScene::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
    case Qt::Key_Escape: {
      setInteractionMode(InteractionMode::NORMAL_MODE);
      break;
    }
    default: break;
  }
  QGraphicsScene::keyPressEvent(event);
}

GraphicalComponent* DiagramScene::getComponentToBeDrawn() const
{
  return componentToBeDrawn;
}

void DiagramScene::clearWireShadow()
{
  if (!wireSegmentToBeDrawn)
    return;

  wireSegmentToBeDrawn->setShowPoints({});
  wireSegmentToBeDrawn = nullptr;
}

void DiagramScene::setComponentShadow()
{
  if (!componentToBeDrawn)
    throw std::logic_error("setComponentShadow: no component to draw");
  if (views().size() != 1)
    throw std::logic_error("setComponentShadow: scene must have exactly one view");

  const auto view      = views()[0];
  const auto cursorPos = view->mapToScene(view->mapFromGlobal(QCursor::pos()));

  addComponent(componentToBeDrawn, cursorPos);
  componentToBeDrawn->setParent(this);
  componentToBeDrawn->setOpacity(0.5);
}

void DiagramScene::clearComponentShadow()
{
  if (!componentToBeDrawn)
    return;
  componentToBeDrawn->setOpacity(1.0);
  componentToBeDrawn = nullptr;
}

void DiagramScene::showCSB(const QPointF pos)
{
  csb->clear();
  csb->setPos(pos);
  addItem(csb);
  csb->showCompleter();
  csb->focus();
}

void DiagramScene::hideCSB()
{
  if (csb->scene() == this)
    removeItem(csb);
}

void DiagramScene::handleInputToggled(Bus targetBus, unsigned int value,
                                      Component_weakPtr source)
{
  if (!simulator)
    return;

  simulator->setBus(targetBus, value, source);
  simulator->run(20);

  refreshGraphicalOutputs();
  update();
}

void DiagramScene::refreshGraphicalOutputs()
{
  for (auto* item : items()) {
    if (item->type() == SiliconTypes::SINGLE_OUTPUT) {
      auto* out = qgraphicsitem_cast<GraphicalOutputSingle*>(item);

      if (!out->getComponent())
        continue;

      auto bus = out->getComponent()->getInputs()[0];

      State s = State::UNKNOWN;

      if (bus.isInErrorState())
        s = State::ERROR;
      else if (!bus.hasUnknowns())
	s = (bus.getCurrentValue() > 0) ? State::HIGH : State::LOW;

      out->setState(s);
    }
  }
}

void DiagramScene::calculateWiresForComponents() const
{
  // Set all wires to initial state
  for (const auto& wire : wireManager.wires()) {
    wire->clearBusState();
  }

  // Process all components that are >= COMPONENT type
  for (auto* item : items()) {
    if (!item || item->type() < COMPONENT)
      continue;

    auto* graphicalComponent = qgraphicsitem_cast<GraphicalLogicComponent*>(item);
    if (!graphicalComponent)
      throw std::logic_error("calculateWiresForComponents: null component encountered");

    // Disconnect the component from all wires (TODO: Make more efficient)
    graphicalComponent->getComponent()->clearWires();

    // Find wire segments colliding with the component, then deduplicate by GraphicalWire
    const auto colliding = collidingItems(graphicalComponent);

    // Deduplicate: collect the unique GraphicalWire* from the colliding segments
    std::unordered_set<GraphicalWire*> seenWires;
    std::vector<GraphicalWire*>        collidingWires;
    for (auto* el : colliding) {
      if (el->type() != SiliconTypes::WIRE_SEGMENT)
        continue;
      auto* seg  = qgraphicsitem_cast<GraphicalWireSegment*>(el);
      auto* wire = seg ? seg->getGraphicalWire() : nullptr;
      if (wire && seenWires.insert(wire).second)
        collidingWires.push_back(wire);
    }

    // For each wire that collides with the component we need to find the port the wire
    // is connected to
    for (GraphicalWire* wire : collidingWires) {
      const auto vertices = wire->getVertices();

      // Check for collision with input ports
      for (const auto& [index, p] :
           graphicalComponent->getInputPorts() | silicon::views::enumerate) {
        const auto portPositionInScene = graphicalComponent->mapToScene(p->getPosition());
        const auto findResult          = std::ranges::find(vertices, portPositionInScene);
        if (findResult != vertices.end()) {
          // If it collides with an input port then we need to set the corresponding input
          // to the wire's bus itself
          graphicalComponent->getComponent()->setInput(index, wire->getBus());
        }
      }

      for (const auto& [index, p] :
           graphicalComponent->getOutputPorts() | silicon::views::enumerate) {
        const auto portPositionInScene = graphicalComponent->mapToScene(p->getPosition());
        const auto findResult          = std::ranges::find(vertices, portPositionInScene);
        if (findResult != vertices.end()) {
          // If it collides with an output port then we need to set the wire dimension to
          // match the output dimension and set the corresponding output to the wire's bus
          // itself
          auto outputSize =
              graphicalComponent->getComponent()->getOutputs()[index].size();
          wire->setBusSize(outputSize);
          graphicalComponent->getComponent()->setOutput(index, wire->getBus());
        }
      }
    }
  }
}

void DiagramScene::addComponent(GraphicalComponent* component, QPointF pos)
{
  component->setPos(pos);

  component->modeChanged(this->getInteractionMode());

  addItem(component);
}

// TODO: Switch to auto memory management!
void DiagramScene::placeComponent(std::string typeName)
{
  if (componentToBeDrawn)
    throw std::logic_error("placeComponent: previous component not yet placed");

  componentToBeDrawn      = GUIComponentFactory::instance().create(typeName).release();
  lastPlacedComponentType = std::move(typeName);

  // TODO: IMPLEMENT COMPONENT SHADOW TO BE SHOWN WHILE DRAGGING
  setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
  setComponentShadow();
  hideCSB();
}

DiagramScene::~DiagramScene()
{
  // Clean up any remaining wire segment being drawn
  if (wireSegmentToBeDrawn) {
    removeItem(wireSegmentToBeDrawn);
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  }

  // Clean up any component being drawn
  if (componentToBeDrawn) {
    removeItem(componentToBeDrawn);
    delete componentToBeDrawn;
    componentToBeDrawn = nullptr;
  }
}
