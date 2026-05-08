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
#include <stdexcept>
#include <variant>

#include <QPointer>

#include <nlohmann/json.hpp>

#include <utils/ranges_wrapper.hpp>

#include <ui/common/enums.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalUtils.hpp>
#include <ui/logiFlow/logiFlowWindow.hpp>
#include <ui/serialization/gui_component_factory.hpp>

// ADDED: Missing include for the ComponentRegistry so it is fully defined
#include <core/serialization/component_registry.hpp>

namespace {

std::string componentNameOr(const Component_ptr& component, const std::string& fallback)
{
  if (!component)
    return fallback;

  const auto property = component->getProperty("name");
  if (!property)
    return fallback;

  if (const auto* name = std::get_if<std::string>(&*property); name && !name->empty())
    return *name;

  return fallback;
}

}  // namespace

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
      this->resetWaveformTrace();
      this->configureSimulatorTrace();
      this->applyFstTraceWriter();

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

  const bool isCursorInsideView = view->viewport()->rect().contains(cursorPosWithinView);

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
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
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
    const auto* comp =
        category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
    if (comp && comp->getComponent()) {
      coreComps.insert(comp->getComponent());
    }
  }

  // 3. Initialize Circuit & Simulator frameworks
  this->circuit = std::make_shared<Circuit>(coreComps, false);

  // The simulator constructor evaluates the circuit once at time zero. Attach tracing
  // before advancing time so waveform exports include the initial 0ns snapshot.
  this->simulator = std::make_unique<Simulator>(this->circuit, 0, true);
  resetWaveformTrace();
  configureSimulatorTrace();
  applyFstTraceWriter();
  simulator->run(1);

  refreshGraphicalOutputs();
  update();
}

void DiagramScene::exitSimulationMode()
{
  // Disconnect signals from inputs
  for (auto* item : items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
      disconnect(input, &GraphicalInput::inputToggled, this,
                 &DiagramScene::handleInputToggled);
    }
  }

  // Discard simulation engines
  this->simulator.reset();
  this->circuit.reset();

  for (auto* item : items()) {
    if (auto* out = category_cast<GraphicalOutputSingle>(item, ItemCategory::Output)) {
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
        if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
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
  if (!simulator)
    return;

  simulator->setBus(targetBus, value, source);
  simulator->run(20);

  refreshGraphicalOutputs();
  update();
}

void DiagramScene::setFstTraceFile(std::optional<std::string> fileName)
{
  fstTraceFile = std::move(fileName);

  if (!simulator)
    return;

  resetWaveformTrace();
  configureSimulatorTrace();
  applyFstTraceWriter();
}

void DiagramScene::applyFstTraceWriter()
{
  if (!simulator)
    return;

  if (!fstTraceFile) {
    simulator->setFstWriter(nullptr);
    return;
  }

  simulator->setFstWriter(std::make_unique<SiliconFstWriter>(
      *fstTraceFile, collectTraceBuses(), SiliconFstWriter::Options{}));
}

void DiagramScene::configureSimulatorTrace()
{
  if (!simulator)
    return;

  simulator->setTraceBuses(collectTraceBuses());
  simulator->setTraceSink([this](uint64_t time, const std::vector<std::string>& values) {
    QStringList qtValues;
    qtValues.reserve(values.size());
    for (const auto& value : values)
      qtValues.push_back(QString::fromStdString(value));

    emit waveformTraceSnapshot(time, qtValues);
  });
}

void DiagramScene::resetWaveformTrace()
{
  QStringList names;
  for (const auto& [name, bus] : collectTraceBuses())
    names.push_back(QString::fromStdString(name));

  emit waveformTraceReset(names, collectTraceInputCount());
}

int DiagramScene::collectTraceInputCount() const
{
  int count = 0;
  for (auto* item : items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
      const auto component = input->getComponent();
      if (component && !component->getOutputs().empty())
        ++count;
    }
  }

  return count;
}

std::vector<SiliconFstWriter::NamedBus> DiagramScene::collectTraceBuses() const
{
  std::vector<GraphicalInput*> inputs;
  std::vector<GraphicalOutputSingle*> outputs;

  for (auto* item : items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input))
      inputs.push_back(input);
    else if (auto* output = category_cast<GraphicalOutputSingle>(item,
                                                                 ItemCategory::Output))
      outputs.push_back(output);
  }

  const auto byPosition = [](const auto* a, const auto* b) {
    if (a->scenePos().y() != b->scenePos().y())
      return a->scenePos().y() < b->scenePos().y();
    return a->scenePos().x() < b->scenePos().x();
  };

  std::ranges::sort(inputs, byPosition);
  std::ranges::sort(outputs, byPosition);

  std::vector<SiliconFstWriter::NamedBus> buses;
  buses.reserve(inputs.size() + outputs.size());

  for (const auto& [index, input] : inputs | silicon::views::enumerate) {
    const auto component = input->getComponent();
    if (component && !component->getOutputs().empty())
      buses.emplace_back(componentNameOr(component, QString("input_%1").arg(index).toStdString()),
                         component->getOutputs()[0]);
  }

  for (const auto& [index, output] : outputs | silicon::views::enumerate) {
    const auto component = output->getComponent();
    if (component && !component->getInputs().empty())
      buses.emplace_back(componentNameOr(component, QString("output_%1").arg(index).toStdString()),
                         component->getInputs()[0]);
  }

  return buses;
}

void DiagramScene::refreshGraphicalOutputs()
{
  for (auto* item : items()) {
    if (auto* out = category_cast<GraphicalOutputSingle>(item, ItemCategory::Output)) {
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
  // 1. Reset all wires to their initial state
  for (const auto& wire : wireManager.wires()) {
    wire->clearBusState();
  }

  // 2. Disconnect all logic components so we can rebuild connections cleanly
  for (auto* item : items()) {
    if (const auto* gComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent)) {
      if (gComp->getComponent()) {
        gComp->getComponent()->clearWires();
      }
    }
  }

  // 3. Drive connections natively from the Wire's endpoints (vertices)
  for (const auto& wire : wireManager.wires()) {
    for (const QPointF& vertex : wire->getVertices()) {
      // Get the graphical items located exactly at the wire's endpoint
      const auto itemsAtVertex = items(vertex);

      for (QGraphicsItem* item : itemsAtVertex) {
        const auto* gComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        if (!gComp || !gComp->getComponent())
          continue;
        // Check if the vertex aligns with any Input port
        for (const auto& [index, p] :
             gComp->getInputPorts() | silicon::views::enumerate) {
          if (gComp->mapToScene(p->getPosition()) == vertex) {
            gComp->getComponent()->setInput(index, wire->getBus());
          }
        }

        // Check if the vertex aligns with any Output port
        for (const auto& [index, p] :
             gComp->getOutputPorts() | silicon::views::enumerate) {
          if (gComp->mapToScene(p->getPosition()) != vertex)
            continue;

          // Adjust the bus dimension to match the output size
          const auto outputSize = gComp->getComponent()->getOutputs()[index].size();
          wire->setBusSize(outputSize);

          // Connect it to the core component
          gComp->getComponent()->setOutput(index, wire->getBus());
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

QUndoStack* DiagramScene::getUndoStack() const
{
  const auto lfw = qobject_cast<LogiFlowWindow*>(views().first()->window());
  return lfw->getUndoStack();
}

// --- Serialization ---------------------------------------------------------------------

std::string DiagramScene::serialize() const
{
  nlohmann::ordered_json   j;
  std::shared_ptr<Circuit> activeCircuit = circuit;

  // Generate temporary circuit if none exists
  if (!activeCircuit) {
    calculateWiresForComponents();
    Component_set coreComps;
    for (auto* item : items()) {
      auto* comp =
          category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
      if (comp && comp->getComponent()) {
        coreComps.insert(comp->getComponent());
      }
    }
    activeCircuit = std::make_shared<Circuit>(coreComps, false);
  }

  j["circuit"] = nlohmann::json::parse(activeCircuit->serialize());

  // Visual Components
  nlohmann::ordered_json visualComponents = nlohmann::ordered_json::array();
  const auto&            compToVertexMap  = activeCircuit->getComponentToVertex();

  for (auto* item : items()) {
    if (auto* comp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent)) {
      auto compJson    = comp->serialize();
      compJson["type"] = comp->getTypeName();

      if (auto component = comp->getComponent()) {
        if (auto it = compToVertexMap.find(component.get());
            it != compToVertexMap.end()) {
          compJson["vertexId"] = static_cast<int>(it->second);
        }
      }
      visualComponents.push_back(std::move(compJson));
    }
  }
  j["visual"]["components"] = std::move(visualComponents);

  // Visual Wires
  auto wiresJson =
      wireManager.getSegments()
      | std::views::transform([](const auto& seg) { return seg->serialize(); })
      | std::ranges::to<std::vector>();

  j["visual"]["wires"] = wiresJson;

  return j.dump(2);
}

void DiagramScene::deserialize(const std::string&       jsonStr,
                               GUIComponentFactory&     guiFactory,
                               const ComponentRegistry& coreRegistry)
{
  auto j = nlohmann::json::parse(jsonStr);

  if (j.contains("circuit")) {
    ComponentRegistry mergedRegistry = coreRegistry;

    if (!mergedRegistry.hasType("DummyInputComponent")) {
      mergedRegistry.registerType("DummyInputComponent", [] {
        return std::make_shared<DummyInputComponent>(Bus(1), "in");
      });
    }
    if (!mergedRegistry.hasType("DummyOutputComponent")) {
      mergedRegistry.registerType("DummyOutputComponent", [] {
        return std::make_shared<DummyOutputComponent>(Bus(1), "out");
      });
    }

    circuit = std::make_shared<Circuit>(
        Circuit::deserialize(j["circuit"].dump(), mergedRegistry));
  }

  if (!j.contains("visual"))
    throw std::runtime_error("Opened file doesn't have the visual part!");

  // Deserializing Wires
  if (j["visual"].contains("wires")) {
    std::map<uint64_t, std::shared_ptr<GraphicalWire>> wireIdToWire;
    for (const auto& wireJson : j["visual"]["wires"]) {
      uint64_t wireId = wireJson.value("wireId", 0ULL);

      if (!wireJson.contains("wireId")) {
        throw std::runtime_error("Wire segment missing wireId");
      }

      std::shared_ptr<GraphicalWire> wire;
      if (wireIdToWire.contains(wireId)) {
        wire = wireIdToWire[wireId];
      } else {
        wire                 = wireManager.createWire(1);
        wireIdToWire[wireId] = wire;
      }

      if (!wireJson.contains("points") || !wireJson["points"].is_array())
        continue;

      std::vector<QPointF> segmentPoints;
      segmentPoints.reserve(wireJson["points"].size());

      for (const auto& pointJson : wireJson["points"]) {
        segmentPoints.emplace_back(pointJson.value("x", 0.0), pointJson.value("y", 0.0));
      }

      if (segmentPoints.size() < 2)
        continue;
      auto* segment = new GraphicalWireSegment(segmentPoints.front());
      segment->setPoints(std::move(segmentPoints));
      addItem(segment);

      // Set logic counterpart
      segment->setGraphicalWire(wire.get());
      wireManager.addSegment(segment);
    }
  }

  // Deserializing Components
  if (j["visual"].contains("components")) {
    for (const auto& compJson : j["visual"]["components"]) {
      auto component = GraphicalComponent::deserialize(compJson, guiFactory);
      if (!component)
        continue;

      if (auto* logicComp = category_cast<GraphicalLogicComponent>(
              component.get(), ItemCategory::LogicComponent)) {
        if (compJson.contains("vertexId") && circuit) {
          const int vertexId = compJson["vertexId"].get<int>();
          if (auto coreComp = circuit->getComponentByVertexId(vertexId)) {
            logicComp->setComponent(coreComp);

            if (auto* gOut = category_cast<GraphicalOutputSingle>(logicComp,
                                                                  ItemCategory::Output)) {
              if (auto dOut = std::dynamic_pointer_cast<DummyOutputComponent>(coreComp)) {
                dOut->setSkin(gOut);
              }
            } else if (auto* gIn = category_cast<GraphicalInput>(logicComp,
                                                                 ItemCategory::Input)) {
              QPointer<GraphicalInput> safeGIn(gIn);
              coreComp->setPropertyCallback("name",
                                            [safeGIn](const PropertyValue& value) {
                                              if (!safeGIn)
                                                return value;
                                              safeGIn->triggerGeometryChange();
                                              return value;
                                            });
            }
          }
        }
      }

      addItem(component.release());
    }
  }

  setInteractionMode(InteractionMode::NORMAL_MODE, true);
}

// --- Clean up --------------------------------------------------------------------------

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

  QGraphicsScene::clear();
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
