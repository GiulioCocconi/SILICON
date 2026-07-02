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

#include <algorithm>
#include <map>
#include <stdexcept>

#include <QCursor>
#include <QMessageBox>

#include <utils/ranges_wrapper.hpp>

#include <ui/common/diagramScene/diagramSceneSerializer.hpp>
#include <ui/common/diagramScene/diagramSceneSimulationController.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/wireRouting.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/logiFlowWindow.hpp>
#include <ui/serialization/gui_component_factory.hpp>

DiagramScene::DiagramScene(QObject* parent) : QGraphicsScene(parent)
{
  simulationController = std::make_unique<DiagramSceneSimulationController>(*this);
  serializer           = std::make_unique<DiagramSceneSerializer>(*this);

  setInteractionMode(InteractionMode::NORMAL_MODE, true);

  csb = new ComponentSearchBox(GUIComponentFactory::instance().availableTypes());
  csb->setParent(this);
  connect(csb, &ComponentSearchBox::requestHide, this, &DiagramScene::hideCSB);
  connect(csb, &ComponentSearchBox::requestCancel, this,
          &DiagramScene::cancelCurrentInteraction);
  connect(csb, &ComponentSearchBox::selectedComponent, this,
          [this](std::string typeName, QPointF) { placeComponent(typeName); });

  wireManager.setTopologyChangedCallback(
      [this]() { simulationController->handleTopologyChanged(); });
}

QPointF DiagramScene::snapToGrid(const QPointF point)
{
  const auto x = round(point.x() / DiagramScene::GRID_SIZE) * DiagramScene::GRID_SIZE;
  const auto y = round(point.y() / DiagramScene::GRID_SIZE) * DiagramScene::GRID_SIZE;

  return {x, y};
}

void DiagramScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  painter->fillRect(rect, ThemeEngine::getColor("SILICON_BACKGROUND"));

  QPen pen(ThemeEngine::getColor("SILICON_GRID"));
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

bool DiagramScene::cancelCurrentInteraction()
{
  if (currentInteractionMode == InteractionMode::NORMAL_MODE)
    return false;

  setInteractionMode(InteractionMode::NORMAL_MODE);
  return true;
}

void DiagramScene::setInteractionMode(const InteractionMode newMode, const bool force)
{
  if (!force && views().size() != 1)
    throw std::logic_error("setInteractionMode: scene must have exactly one view");

  const auto currentMode = getInteractionMode();
  if (currentMode == newMode && !force)
    return;

  clearSelection();

  if (wireSegmentToBeDrawn && newMode != InteractionMode::WIRE_CREATION_MODE)
    finalizeWireCreation();

  if (currentMode == InteractionMode::COMPONENT_PLACING_MODE)
    exitComponentPlacingMode();
  else if (newMode == InteractionMode::COMPONENT_PLACING_MODE)
    enterComponentPlacingMode();

  if (newMode == InteractionMode::SIMULATION_MODE) {
    currentInteractionMode = newMode;
    emit modeChanged(newMode);

    if (!enterSimulationMode()) {
      currentInteractionMode = InteractionMode::NORMAL_MODE;
      emit modeChanged(currentInteractionMode);
    }
    return;
  }

  if (currentMode == InteractionMode::SIMULATION_MODE)
    exitSimulationMode();

  currentInteractionMode = newMode;
  emit modeChanged(newMode);
}

void DiagramScene::finalizeWireCreation()
{
  // Remove the wireSegment if it's invisible
  if (wireSegmentToBeDrawn->empty()) {
    removeItem(wireSegmentToBeDrawn);
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  } else {
    auto* finalizedSegment = wireSegmentToBeDrawn;
    // Always register the segment so the WireManager tracks it for junctions
    // and collision detection
    wireManager.addSegment(finalizedSegment);

    if (auto* undoStack = getUndoStack()) {
      undoStack->push(new SceneSelectionCommand(this, serializeItems({finalizedSegment}),
                                                SceneSelectionCommand::Operation::Add,
                                                true));
    }
  }
  clearWireShadow();
}

void DiagramScene::exitComponentPlacingMode()
{
  hideCSB();
  if (componentToBeDrawn) {
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

  if (suppressNextComponentSearch) {
    suppressNextComponentSearch = false;
    return;
  }

  const QPoint globalCursorPos = QCursor::pos();
  const auto   view            = views()[0];
  // The position for the CSB should be the position of the cursor (if the cursor is
  // inside the view) or the center of the view (every other case)
  // Get center pos
  const QPoint centerPos = view->viewport()->rect().center();
  // Get cursor pos within view
  const QPoint cursorPosWithinView = view->mapFromGlobal(globalCursorPos);
  const bool isCursorInsideView = view->viewport()->rect().contains(cursorPosWithinView);
  const QPoint posForCSB        = isCursorInsideView ? cursorPosWithinView : centerPos;

  showCSB(view->mapToScene(posForCSB));
}

bool DiagramScene::enterSimulationMode()
{
  return simulationController->enterSimulationMode();
}

void DiagramScene::exitSimulationMode()
{
  simulationController->exitSimulationMode();
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

      auto wireRoutingObstacles = [this]() {
        // HELPER: Get the obstacles for autorouting
        constexpr float     MARGIN = GRID_SIZE / 2.0;
        std::vector<QRectF> obstacles;

        for (auto* item : items()) {
          const auto* component =
              category_cast<GraphicalComponent>(item, ItemCategory::Component);
          if (!component)
            continue;

          obstacles.push_back(component->mapToScene(component->collisionRectForWires())
                                  .boundingRect()
                                  .adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN));
        }

        return obstacles;
      };

      const QPointF lastPoint =
          wireSegmentToBeDrawn->mapToScene(wireSegmentToBeDrawn->lastPoint());

      auto route =
          silicon::routeOrthogonalWire(lastPoint, cursorPos, wireRoutingObstacles());

      if (!route.empty())
        route.erase(route.begin());

      wireSegmentToBeDrawn->setShowPoints(route);
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
        auto*      placedComponent = componentToBeDrawn;
        const auto rotation        = placedComponent->rotation();

        clearComponentShadow();

        if (auto* undoStack = getUndoStack()) {
          undoStack->push(
              new SceneSelectionCommand(this, serializeItems({placedComponent}),
                                        SceneSelectionCommand::Operation::Add, true));
        }

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
      if (!ioInteractionsEnabled)
        break;

      const auto itemsAtPos = items(cursorPos);

      for (const auto item : itemsAtPos) {
        if (auto* io = category_cast<GraphicalIO>(item, ItemCategory::IO))
          io->handleSimulationClick();
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
      if (cancelCurrentInteraction()) {
        event->accept();
        return;
      }
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

  wireSegmentToBeDrawn->setInitialPosition();
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
  componentToBeDrawn->setInitialPosition();
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
  simulationController->handleInputToggled(targetBus, value, source);
}

void DiagramScene::setFstTraceFile(std::optional<std::string> fileName)
{
  simulationController->setFstTraceFile(std::move(fileName));
}

bool DiagramScene::isFstTracingEnabled() const
{
  return simulationController->isFstTracingEnabled();
}

void DiagramScene::simulateEditedWaveform(
    const qulonglong duration, std::vector<SiliconWaveformSample> inputSnapshots)
{
  simulationController->simulateEditedWaveform(duration, std::move(inputSnapshots));
}

void DiagramScene::refreshGraphicalOutputs()
{
  simulationController->refreshGraphicalOutputs();
}

bool DiagramScene::calculateWiresForComponents()
{
  for (const auto& wire : wireManager.wires()) {
    wire->clearBusState();
  }

  for (auto* item : items()) {
    if (const auto* gComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent)) {
      if (gComp->getComponent())
        gComp->getComponent()->clearWires();
    }
  }

  for (const auto& wire : wireManager.wires()) {
    for (const QPointF& vertex : wire->getVertices()) {
      const auto itemsAtVertex = items(vertex);

      for (QGraphicsItem* item : itemsAtVertex) {
        const auto* gComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        if (!gComp || !gComp->getComponent())
          continue;

        for (const auto& [index, p] :
             gComp->getOutputPorts() | silicon::views::enumerate) {
          if (gComp->mapToScene(p->getPosition()) != vertex)
            continue;

          const auto outputSize = gComp->getComponent()->getOutputs()[index].size();
          wire->setBusSize(outputSize);
          gComp->getComponent()->setOutput(index, wire->getBus());
        }

        for (const auto& [index, p] :
             gComp->getInputPorts() | silicon::views::enumerate) {
          if (gComp->mapToScene(p->getPosition()) == vertex) {
            try {
              gComp->getComponent()->setInput(index, wire->getBus(), true);
            } catch (const std::exception& e) {
              p->setInputAssignmentError(true);
              QMessageBox::critical(views().first(), "Error while assigning inputs!",
                                    QString("An input assignation for port %1 "
                                            "failed:\n %2\n")
                                        .arg(p->getName(), e.what()));
              update();
              return false;
            }
          }
        }
      }
    }
  }

  return true;
}

void DiagramScene::clearInputAssignmentErrors()
{
  for (auto* item : items()) {
    const auto* gComp =
        category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
    if (!gComp)
      continue;

    for (Port* port : gComp->getInputPorts())
      port->setInputAssignmentError(false);
  }

  update();
}

void DiagramScene::addComponent(GraphicalComponent* component, QPointF pos)
{
  component->setPos(pos);
  addItem(component);
}

void DiagramScene::placeComponent(std::string_view typeName)
{
  placeComponent(typeName, true);
}

void DiagramScene::placeComponent(std::string_view typeName, const bool showSearchBox)
{
  if (componentToBeDrawn)
    throw std::logic_error("placeComponent: previous component not yet placed");

  componentToBeDrawn      = GUIComponentFactory::instance().create(typeName).release();
  lastPlacedComponentType = typeName;
  suppressNextComponentSearch = !showSearchBox;

  // TODO: IMPLEMENT COMPONENT SHADOW TO BE SHOWN WHILE DRAGGING
  setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
  setComponentShadow();
  hideCSB();
}

void DiagramScene::registerGraphicalItem(GraphicalItem* item)
{
  if (!item)
    return;

  item->setOwningScene(this);
  itemsByUiId[item->getUiId()] = item;
}

void DiagramScene::unregisterGraphicalItem(GraphicalItem* item)
{
  if (!item)
    return;

  if (auto it = itemsByUiId.find(item->getUiId());
      it != itemsByUiId.end() && it->second == item) {
    itemsByUiId.erase(it);
  }

  item->setOwningScene(nullptr);
}

QUndoStack* DiagramScene::getUndoStack() const
{
  const auto lfw = qobject_cast<LogiFlowWindow*>(views().first()->window());
  return lfw->getUndoStack();
}

LogSideView* DiagramScene::getLogSideView() const
{
  const auto lfw = qobject_cast<LogiFlowWindow*>(views().first()->window());
  return lfw->getLogSideView();
}

std::string DiagramScene::serialize() const
{
  return serializer->serialize();
}

nlohmann::ordered_json DiagramScene::serializeSelection() const
{
  return serializer->serializeSelection();
}

nlohmann::ordered_json
DiagramScene::serializeItems(const std::vector<QGraphicsItem*>& sceneItems) const
{
  return serializer->serializeItems(sceneItems);
}

void DiagramScene::deserialize(const std::string&       jsonStr,
                               GUIComponentFactory&     guiFactory,
                               const ComponentRegistry& coreRegistry)
{
  serializer->deserialize(jsonStr, guiFactory, coreRegistry);
}

bool DiagramScene::insertSelection(const nlohmann::json&    payload,
                                   GUIComponentFactory&     guiFactory,
                                   const ComponentRegistry& coreRegistry,
                                   QPointF targetOrigin, const bool isPaste)
{
  return serializer->insertSelection(payload, guiFactory, coreRegistry, targetOrigin,
                                     isPaste);
}

GraphicalItem* DiagramScene::findGraphicalItemByUiId(const uint64_t uiId) const
{
  if (auto it = itemsByUiId.find(uiId); it != itemsByUiId.end())
    return it->second;
  return nullptr;
}

void DiagramScene::removeItems(const std::vector<QGraphicsItem*>& sceneItems)
{
  if (sceneItems.empty())
    return;

  clearSelection();

  // Remove everything from the scene before deleting objects so scene callbacks never
  // observe partially-destroyed items.
  for (auto* item : sceneItems)
    removeItem(item);

  for (auto* item : sceneItems)
    delete item;

  // Deletion can invalidate junction markers just as much as insertion can.
  wireManager.calculateJunctions();
  updateSceneAfterEdit();
}

bool DiagramScene::removeSelection(const nlohmann::json& payload)
{
  return serializer->removeSelection(payload);
}

void DiagramScene::updateSceneAfterEdit()
{
  static_cast<void>(calculateWiresForComponents());
  wireManager.notifyTopologyChanged();
  circuit.reset();
  update();
}

void DiagramScene::clear()
{
  setInteractionMode(InteractionMode::NORMAL_MODE);

  if (componentToBeDrawn) {
    delete componentToBeDrawn;
    componentToBeDrawn = nullptr;
  }

  if (wireSegmentToBeDrawn) {
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  }

  wireManager.clear();

  if (getUndoStack())
    getUndoStack()->clear();

  if (getLogSideView())
    getLogSideView()->clear();

  QGraphicsScene::clear();
  simulationController->clearWaveformTrace();
}

DiagramScene::~DiagramScene()
{
  // QGraphicsScene deletes remaining items from the base destructor. Break the back
  // references first so GraphicalItem teardown cannot touch DiagramScene state after
  // our members have already started dying.
  for (auto& [uiId, item] : itemsByUiId) {
    Q_UNUSED(uiId);
    if (item)
      item->setOwningScene(nullptr);
  }
  itemsByUiId.clear();

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
