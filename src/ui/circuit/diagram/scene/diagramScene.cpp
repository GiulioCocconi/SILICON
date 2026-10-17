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
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <QCursor>
#include <QLineF>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScopedValueRollback>
#include <QTimer>

#include <logging/logger.hpp>
#include <utils/ranges_wrapper.hpp>

#include <ui/circuit/components/graphicalIO.hpp>
#include <ui/circuit/components/graphicalMemory.hpp>
#include <ui/circuit/components/subcircuit/graphicalSubcircuit.hpp>
#include <ui/circuit/diagram/circuitAutoplacer.hpp>
#include <ui/circuit/diagram/enums.hpp>
#include <ui/circuit/diagram/graphicalComponent.hpp>
#include <ui/circuit/diagram/graphicalWire.hpp>
#include <ui/circuit/diagram/interactiveWireRouter.hpp>
#include <ui/circuit/diagram/scene/diagramSceneSerializer.hpp>
#include <ui/circuit/diagram/scene/diagramSceneSimulationController.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/circuit/diagram/wireRouting.hpp>
#include <ui/serialization/gui_component_factory.hpp>
#include <ui/shell/theme.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  template <typename Visitor>
  void forEachPortAnchor(const GraphicalComponent& component, Visitor&& visit)
  {
    auto collect = [&](const std::vector<Port*>& ports, bool output) {
      for (uint32_t index = 0; index < ports.size(); ++index) {
        const QPointF position = component.mapToScene(ports[index]->getPosition());
        visit(WireManager::PortAnchor{component.getUiId(), index, output, position});
      }
    };
    collect(component.getInputPorts(), false);
    collect(component.getOutputPorts(), true);
  }

  const GraphicalComponent* componentForItem(const QGraphicsItem* item)
  {
    for (; item; item = item->parentItem()) {
      if (const auto* component =
              category_cast<GraphicalComponent>(item, ItemCategory::Component))
        return component;
    }
    return nullptr;
  }

  qreal gridCoordinate(const qreal coordinate)
  {
    return std::round(coordinate / DiagramScene::GRID_SIZE) * DiagramScene::GRID_SIZE;
  }

  QPointF snappedEdgePoint(const QPointF point, const QPointF a, const QPointF b)
  {
    if (a.x() == b.x()) {
      return {a.x(), std::clamp(gridCoordinate(point.y()),
                               std::min(a.y(), b.y()), std::max(a.y(), b.y()))};
    }
    return {std::clamp(gridCoordinate(point.x()),
                       std::min(a.x(), b.x()), std::max(a.x(), b.x())), a.y()};
  }

}  // namespace

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

  wireManager.setChangedCallback([this](const WireManager::Change change) {
    const auto flags = static_cast<uint8_t>(change);
    if (!(flags & (static_cast<uint8_t>(WireManager::Change::Connectivity)
                   | static_cast<uint8_t>(WireManager::Change::Electrical)))) {
      update();
      return;
    }
    // Outside simulation, any user wire edit invalidates the cached logical circuit.
    // Autoplacement restores a fresh authoritative circuit after replacing all routes.
    if (getInteractionMode() != InteractionMode::SIMULATION_MODE)
      circuit.reset();
    requestConnectionsRefresh();
  });
  wireManager.setPortAnchors([this] {
    std::vector<WireManager::PortAnchor> anchors;
    for (auto* item : items()) {
      auto* component = category_cast<GraphicalComponent>(item, ItemCategory::Component);
      if (!component)
        continue;
      forEachPortAnchor(*component,
                        [&](const auto& anchor) { anchors.push_back(anchor); });
    }
    return anchors;
  });
}

void DiagramScene::setDocumentStore(
    const SILICON::project::DocumentStore* projectDocuments) noexcept
{
  documents = projectDocuments;
}

const SILICON::project::DocumentStore* DiagramScene::documentStore() const noexcept
{
  return documents;
}

void DiagramScene::setCircuitResolver(const CircuitResolver* projectResolver) noexcept
{
  resolver = projectResolver;
}

const CircuitResolver* DiagramScene::circuitResolver() const noexcept
{
  return resolver;
}

namespace {

  std::vector<std::string> componentTypesForScene(const DiagramScene& scene)
  {
    Q_UNUSED(scene);
    return GUIComponentFactory::instance().availableTypes();
  }

  std::vector<GraphicalLogicComponent*>
  logicComponentsInScene(const QGraphicsScene& scene)
  {
    std::vector<GraphicalLogicComponent*> components;

    for (auto* item : scene.items()) {
      auto* component =
          category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
      if (component && component->getComponent())
        components.push_back(component);
    }

    return components;
  }

  Component_set coreComponentsFor(std::span<GraphicalLogicComponent* const> components)
  {
    Component_set coreComponents;
    for (const auto* component : components) {
      if (component && component->getComponent())
        coreComponents.insert(component->getComponent());
    }
    return coreComponents;
  }

  void applyAutoplacement(DiagramScene& scene, const Circuit& activeCircuit,
                          std::span<GraphicalLogicComponent* const> components,
                          const CircuitAutoplacerOptions&           options = {})
  {
    const auto  result = CircuitAutoplacer::compute(activeCircuit, components, options);
    const auto& componentMap = result.components;
    const auto& wires        = result.wires;

    if (componentMap.empty())
      return;

    const bool collisionChecksEnabled = scene.itemCollisionChecksEnabled();
    scene.setItemCollisionChecksEnabled(false);
    for (const auto& [io, side] : result.ioPortOrientations)
      io->setPortOrientation(side);
    for (const auto& [component, position] : componentMap) {
      component->setPos(position);
      component->setInitialPosition();
    }
    scene.setItemCollisionChecksEnabled(collisionChecksEnabled);

    scene.getWireManager().replaceSegments(scene, wires);

    // The routed wires were produced from activeCircuit and already carry its buses.
    // Recalculating component buses from geometry here is both redundant and unsafe:
    // coincident route portions near a port can make a later graphical wire overwrite
    // that port's original assignment. Keep the logical topology authoritative and only
    // refresh the cached circuit after applying the new geometry. DiagramScene applies
    // the document name when accepting the replacement cache.
    auto refreshedCircuit =
        std::make_shared<Circuit>(coreComponentsFor(components), false);
    scene.setCircuit(std::move(refreshedCircuit));
    scene.update();
  }

}  // namespace

int DiagramScene::snapToGrid(const qreal value)
{
  return static_cast<int>(std::round(value / GRID_SIZE)) * GRID_SIZE;
}

QPoint DiagramScene::snapToGrid(const QPointF point)
{
  return {snapToGrid(point.x()), snapToGrid(point.y())};
}

QPointF DiagramScene::resolveWireTarget(const QPointF scenePoint) const
{
  QPointF      bestPoint{gridCoordinate(scenePoint.x()), gridCoordinate(scenePoint.y())};
  qreal        bestDistance = GRID_SIZE / 2.0;
  const QRectF query(scenePoint - QPointF(bestDistance, bestDistance),
                     QSizeF(2 * bestDistance, 2 * bestDistance));
  // Query bounding rectangles so port children and their lines participate even
  // when the anchors lie outside the component's collision or painted shape.
  const auto nearby = items(query, Qt::IntersectsItemBoundingRect);

  const auto& registeredSegments = wireManager.getSegments();
  const GraphicalWireSegment* bestSegment = nullptr;

  auto updateBest = [&](const QPointF candidate,
                        const GraphicalWireSegment* segment = nullptr) {
    const qreal distance = QLineF(scenePoint, candidate).length();
    if (distance > bestDistance)
      return;
    if (distance == bestDistance) {
      // Ports win ties. Between wires, preserve registration order.
      if (!segment || !bestSegment || segment == bestSegment)
        return;
      const auto first = std::ranges::find_if(registeredSegments, [&](const auto* wire) {
        return wire == segment || wire == bestSegment;
      });
      if (first == registeredSegments.end() || *first != segment)
        return;
    }
    bestDistance = distance;
    bestPoint    = candidate;
    bestSegment  = segment;
  };

  // A nearby port or line can belong to a component outside the query rectangle.
  // Visit each owner once, in scene stacking order, before considering wires.
  std::vector<const GraphicalComponent*> visitedComponents;
  for (const auto* item : nearby) {
    const auto* component = componentForItem(item);
    if (!component || std::ranges::contains(visitedComponents, component))
      continue;
    visitedComponents.push_back(component);
    forEachPortAnchor(*component,
                      [&](const auto& anchor) { updateBest(anchor.position); });
  }

  for (const auto* item : nearby) {
    const auto* segment =
        category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment);
    if (!segment || !segment->getGraphicalWire()
        || segment->getGraphicalWire()->getManager() != &wireManager)
      continue;
    // Topology normally identifies registered segments. During deferred bulk
    // registration it may be absent, so fall back to the authoritative list.
    if (!wireManager.topologyFor(segment->getUiId()).firstVertex
        && !std::ranges::contains(registeredSegments, segment))
      continue;
    const auto points = segment->getScenePoints();
    for (const auto& [a, b] : points | SILICON::views::adjacent<2>)
      updateBest(snappedEdgePoint(scenePoint, a, b), segment);
  }
  return bestPoint;
}

std::unique_ptr<InteractiveWireRouter> DiagramScene::makeInteractiveRouter() const
{
  std::vector<RoutingComponent> components;
  for (const auto* item : items()) {
    const auto* component =
        category_cast<GraphicalComponent>(item, ItemCategory::Component);
    if (!component)
      continue;

    RoutingComponent snapshot{
        .id     = component->getUiId(),
        .bounds = component->mapToScene(component->collisionRectForWires())
                      .boundingRect()
                      .normalized(),
        .ports = {}};
    snapshot.ports.reserve(component->getInputPorts().size()
                           + component->getOutputPorts().size());
    forEachPortAnchor(*component, [&](const auto& anchor) {
      snapshot.ports.push_back(anchor.position);
    });
    components.push_back(std::move(snapshot));
  }

  std::vector<RoutingSegment> segments;
  segments.reserve(wireManager.getSegments().size());
  for (const auto* segment : wireManager.getSegments()) {
    if (!segment || segment->empty() || !segment->getGraphicalWire())
      continue;

    auto        points   = segment->getScenePoints();
    const auto& topology = wireManager.topologyFor(segment->getUiId());
    std::vector<std::pair<uint64_t, QPointF>> vertices;
    vertices.reserve(2 + topology.junctions.size());
    vertices.emplace_back(topology.firstVertex, points.front());
    vertices.emplace_back(topology.lastVertex, points.back());
    vertices.append_range(topology.junctions);
    segments.push_back({segment->getUiId(), segment->getGraphicalWire()->getRouteId(),
                        std::move(points), std::move(vertices)});
  }
  return std::make_unique<InteractiveWireRouter>(std::move(components),
                                                 std::move(segments), GRID_SIZE);
}

void DiagramScene::applyRoutingPreview(const std::vector<RoutedSegment>& routes,
                                       RouteEditState& state)
{
  for (const auto& route : routes) {
    auto* segment = category_cast<GraphicalWireSegment>(
        findGraphicalItemByUiId(route.id), ItemCategory::WireSegment);
    if (!segment)
      continue;
    std::vector<QPointF> local;
    local.reserve(route.points.size());
    for (const QPointF point : route.points)
      local.push_back(segment->mapFromScene(point));
    if (local != segment->getPoints()) {
      state.remember(wireManager, segment);
      segment->setPoints(std::move(local));
    }
  }
}

void DiagramScene::RouteEditState::remember(WireManager& manager,
                                            GraphicalWireSegment* segment)
{
  this->manager = &manager;
  const auto id = segment->getUiId();
  if (std::ranges::find_if(originals, [id](const auto& entry) {
        return entry.first == id;
      }) != originals.end())
    return;
  edits.push_back(std::make_unique<WireManager::Edit>(manager, segment));
  originals.emplace_back(id, segment->getPoints());
}

void DiagramScene::RouteEditState::captureContacts(const DiagramScene& scene)
{
  contacts.clear();
  topologyBefore.clear();
  netBefore.clear();
  const auto segments = scene.getWireManager().getSegments();
  for (const auto* segment : segments) {
    topologyBefore.emplace(segment->getUiId(),
                           scene.getWireManager().topologyFor(segment->getUiId()));

    if (const auto* wire = segment->getGraphicalWire())
      netBefore.emplace(segment->getUiId(),
                        WireEndpointEdit::NetState{wire->getRouteId(), wire->getBus()});

    const auto points = segment->getScenePoints();

    if (points.size() < 2)
      continue;
    for (const bool first : {true, false}) {
      const QPointF endpoint = first ? points.front() : points.back();
      const auto ownTopology = scene.getWireManager().topologyFor(segment->getUiId());
      const uint64_t vertex = first ? ownTopology.firstVertex : ownTopology.lastVertex;
      for (const auto* other : segments) {
        if (other == segment)
          continue;
        const auto otherTopology = scene.getWireManager().topologyFor(other->getUiId());
        if (vertex && (otherTopology.firstVertex == vertex
                       || otherTopology.lastVertex == vertex
                       || std::ranges::any_of(otherTopology.junctions,
                           [vertex](const auto& junction) { return junction.first == vertex; })))
          contacts.push_back({segment->getUiId(), first, other->getUiId()});
      }

      for (auto* item : scene.items()) {
        auto* component = category_cast<GraphicalComponent>(item, ItemCategory::Component);
        if (!component)
          continue;
        for (Port* port : component->getPorts())
          if (component->mapToScene(port->getPosition()) == endpoint)
            contacts.push_back({segment->getUiId(), first, 0, component, port});
      }
    }
  }
}

void DiagramScene::RouteEditState::cancel()
{
  if (manager) {
    for (const auto& [id, points] : originals)
      for (auto* segment : manager->getSegments())
        if (segment->getUiId() == id) {
          segment->setPoints(points);
          break;
        }
  }

  for (auto& edit : edits)
    edit->cancel();

  edits.clear();
  originals.clear();
  topologyBefore.clear();
  netBefore.clear();
  manager = nullptr;
}

void DiagramScene::RouteEditState::commit(WireManager& manager)
{
  // All contacts are recalculated from the committed routes.
  WireManager::Batch batch(manager);
  const auto segments = manager.getSegments();
  for (const auto& id : originals | std::views::keys) {
    const auto found = std::ranges::find(segments, id, &GraphicalWireSegment::getUiId);
    if (found != segments.end()
        && ((*found)->getPoints().size() < 2
            || !isOrthogonalRoute((*found)->getPoints()))) {
      cancel();
      throw std::invalid_argument("A committed wire route must be orthogonal");
    }
  }
  for (const auto& edit : edits)
    edit->commit();
  batch.commit();
}

std::vector<WireRouteChange> DiagramScene::RouteEditState::changes(const DiagramScene& scene) const
{
  std::vector<WireRouteChange> changes;
  for (const auto& [id, before] : originals) {
    const auto* segment = category_cast<GraphicalWireSegment>(
        scene.findGraphicalItemByUiId(id), ItemCategory::WireSegment);
    if (segment && segment->getPoints() != before)
      changes.push_back({id, before, segment->getPoints()});
  }
  return changes;
}

void DiagramScene::beginComponentDrag()
{
  cancelComponentDrag();
  for (auto* item : selectedItems()) {
    if (auto* component = category_cast<GraphicalComponent>(item, ItemCategory::Component)) {
      componentDrag.initialPositions.emplace_back(component, component->pos());
      componentDrag.validPositions.emplace_back(component, component->pos());
    }
  }

  if (componentDrag.initialPositions.empty())
    return;

  const auto segments = wireManager.getSegments();

  for (const auto& component : componentDrag.initialPositions | std::views::keys) {
    std::vector<QPointF> ports;
    for (const auto* port : component->getPorts())
      ports.push_back(component->mapToScene(port->getPosition()));
    auto& attached = componentDrag.attachedWires[component];

    for (const auto* segment : segments) {
      const auto points = segment->getScenePoints();
      if (points.size() < 2 || !segment->getGraphicalWire())
        continue;
      if (std::ranges::find(ports, points.front()) != ports.end()
          || std::ranges::find(ports, points.back()) != ports.end())
        attached.insert(segment->getGraphicalWire());
    }
  }

  componentDrag.router = makeInteractiveRouter();
}

bool DiagramScene::isDraggingComponent(const GraphicalComponent* component) const
{
  return (componentDrag.router && componentDrag.attachedWires.contains(component))
         || mixedDragAttachedWires.contains(component);
}

bool DiagramScene::isWireAttachedToDraggedComponent(
    const GraphicalComponent* component, const GraphicalWire* wire) const
{
  const auto found = componentDrag.attachedWires.find(component);

  if (found != componentDrag.attachedWires.end() && found->second.contains(wire))
    return true;

  const auto mixed = mixedDragAttachedWires.find(component);
  return mixed != mixedDragAttachedWires.end() && mixed->second.contains(wire);
}

void DiagramScene::updateComponentDrag()
{
  if (!componentDrag.router)
    return;
  if (std::ranges::all_of(componentDrag.validPositions, [](const auto& entry) {
        return entry.first->pos() == entry.second;
      }))
    return;
  std::vector<ComponentRouteMove> moves;
  for (const auto& [component, initial] : componentDrag.initialPositions) {
    if (component->pos() != initial)
      moves.push_back({component->getUiId(),
                       component->mapToScene(component->collisionRectForWires())
                           .boundingRect().normalized()});
  }
  const auto routes = componentDrag.router->routeComponents(moves);
  if (!routes) {
    const bool collisionChecks = itemCollisionChecksEnabled();
    setItemCollisionChecksEnabled(false);
    for (const auto& [component, position] : componentDrag.validPositions)
      component->setPos(position);
    setItemCollisionChecksEnabled(collisionChecks);
    return;
  }
  applyRoutingPreview(*routes, componentDrag.routes);
  for (auto& [component, position] : componentDrag.validPositions)
    position = component->pos();
}

std::vector<WireRouteChange> DiagramScene::finishComponentDrag()
{
  if (!componentDrag.router)
    return {};
  auto changes = componentDrag.routes.changes(*this);
  try {
    componentDrag.routes.commit(wireManager);
  } catch (const std::invalid_argument&) {
    cancelComponentDrag();
    return {};
  }
  componentDrag = {};
  wireManager.refreshPortAnchors();
  requestConnectionsRefresh();
  update();
  return changes;
}

void DiagramScene::cancelComponentDrag()
{
  if (!componentDrag.router)
    return;
  const bool collisionChecks = itemCollisionChecksEnabled();
  setItemCollisionChecksEnabled(false);
  for (const auto& [component, position] : componentDrag.initialPositions) {
    component->setPos(position);
    component->setInitialPosition();
  }
  setItemCollisionChecksEnabled(collisionChecks);
  componentDrag.routes.cancel();
  componentDrag = {};
}

bool DiagramScene::hasWireGroupSelection() const
{
  bool hasComponent = false;
  size_t wireCount = 0;
  for (auto* item : selectedItems()) {
    hasComponent |= category_cast<GraphicalComponent>(item, ItemCategory::Component) != nullptr;
    wireCount += category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment) != nullptr;
  }
  return wireCount > 0 && (hasComponent || wireCount > 1);
}

void DiagramScene::beginMixedDrag()
{
  cancelMixedDrag();
  for (auto* item : selectedItems())
    if (auto* graphical = dynamic_cast<GraphicalItem*>(item))
      mixedDrag.push_back({graphical, graphical->pos(), graphical->pos()});
  const auto segments = wireManager.getSegments();
  for (const auto& entry : mixedDrag) {
    auto* component = category_cast<GraphicalComponent>(entry.item,
                                                        ItemCategory::Component);
    if (!component)
      continue;
    std::vector<QPointF> ports;
    for (const auto* port : component->getPorts())
      ports.push_back(component->mapToScene(port->getPosition()));
    auto& attached = mixedDragAttachedWires[component];
    for (const auto* segment : segments) {
      const auto points = segment->getScenePoints();
      if (points.size() < 2 || !segment->getGraphicalWire())
        continue;
      if (std::ranges::find(ports, points.front()) != ports.end()
          || std::ranges::find(ports, points.back()) != ports.end())
        attached.insert(segment->getGraphicalWire());
    }
  }
  for (const auto* segment : wireManager.getSegments()) {
    const auto id = segment->getUiId();
    mixedDragTopologyBefore.emplace(id, wireManager.topologyFor(id));
    if (const auto* wire = segment->getGraphicalWire())
      mixedDragNetBefore.emplace(id, WireEndpointEdit::NetState{
                                         wire->getRouteId(), wire->getBus()});
  }
}

void DiagramScene::updateMixedDrag()
{
  if (mixedDrag.empty())
    return;
  const QPointF delta = mixedDrag.front().item->pos()
                        - mixedDrag.front().validPosition;
  const bool movedTogether = std::ranges::all_of(mixedDrag, [delta](const auto& entry) {
    return entry.item->pos() - entry.validPosition == delta;
  });
  if (!movedTogether) {
    const bool collisionChecks = itemCollisionChecksEnabled();
    setItemCollisionChecksEnabled(false);
    for (const auto& entry : mixedDrag)
      entry.item->setPos(entry.validPosition);
    setItemCollisionChecksEnabled(collisionChecks);
    return;
  }
  for (auto& entry : mixedDrag)
    entry.validPosition = entry.item->pos();
}

WireEndpointEdit DiagramScene::finishMixedDrag()
{
  if (mixedDrag.empty())
    return {};

  updateMixedDrag();
  WireEndpointEdit edit;
  if (!mixedDrag.empty()) {
    std::vector<std::pair<uint64_t, QPointF>> translations;
    for (const auto& entry : mixedDrag)
      if (category_cast<GraphicalWireSegment>(entry.item, ItemCategory::WireSegment)) {
        translations.emplace_back(entry.item->getUiId(),
                                  entry.validPosition - entry.originalPosition);
        edit.segmentId = entry.item->getUiId();
      }
    if (!wireManager.commitSegmentTranslations(translations)) {
      const bool collisionChecks = itemCollisionChecksEnabled();
      setItemCollisionChecksEnabled(false);
      for (const auto& entry : mixedDrag)
        entry.item->setPos(entry.originalPosition);
      setItemCollisionChecksEnabled(collisionChecks);
    } else {
      for (const auto& [id, before] : mixedDragTopologyBefore) {
        const auto after = wireManager.topologyFor(id);
        if (before.firstVertex != after.firstVertex
            || before.lastVertex != after.lastVertex
            || before.junctions != after.junctions
            || before.edgeIds != after.edgeIds)
          edit.topologyChanges.push_back({id, before, after});
      }
      for (const auto& [id, before] : mixedDragNetBefore) {
        auto* segment = category_cast<GraphicalWireSegment>(
            findGraphicalItemByUiId(id), ItemCategory::WireSegment);
        if (!segment || !segment->getGraphicalWire())
          continue;
        const auto* wire = segment->getGraphicalWire();
        WireEndpointEdit::NetState after{wire->getRouteId(), wire->getBus()};
        if (before.routeId != after.routeId || !(before.bus == after.bus))
          edit.netChanges.push_back({id, before, after});
      }
    }
  }
  mixedDrag.clear();
  mixedDragAttachedWires.clear();
  mixedDragTopologyBefore.clear();
  mixedDragNetBefore.clear();
  wireManager.refreshPortAnchors();
  requestConnectionsRefresh();
  return edit;
}

void DiagramScene::cancelMixedDrag()
{
  if (mixedDrag.empty())
    return;
  const bool collisionChecks = itemCollisionChecksEnabled();
  setItemCollisionChecksEnabled(false);
  for (const auto& entry : mixedDrag)
    entry.item->setPos(entry.originalPosition);
  setItemCollisionChecksEnabled(collisionChecks);
  mixedDrag.clear();
  mixedDragAttachedWires.clear();
  mixedDragTopologyBefore.clear();
  mixedDragNetBefore.clear();
}

bool DiagramScene::beginWireDrag(GraphicalWireSegment* segment,
                                 const QPointF scenePos, const QPointF localPos)
{
  if (!segment || currentInteractionMode != InteractionMode::NORMAL_MODE ||
      !segment->getGraphicalWire() || !segment->getGraphicalWire()->getManager())
    return false;
  cancelWireDrag();
  const auto& points = segment->getPoints();
  const auto bend = segment->pointIndexAt(localPos);
  const bool dragEndpoint = bend && (*bend == 0 || *bend + 1 == points.size());
  const bool dragBend     = bend && *bend > 0 && *bend + 1 < points.size();
  const auto edge = bend ? std::optional<size_t>{} : segment->edgeIndexAt(localPos);
  if (!dragEndpoint && !dragBend && !edge)
    return false;

  wireDrag.segment = segment;
  wireDrag.type = dragEndpoint ? WireDragKind::Endpoint
                            : dragBend ? WireDragKind::Bend : WireDragKind::Edge;
  wireDrag.index = bend ? *bend : *edge;
  wireDrag.start = snapToGrid(scenePos);
  wireDrag.originalPoints = points;
  wireDrag.router = makeInteractiveRouter();
  wireDrag.routes.captureContacts(*this);
  return true;
}

bool DiagramScene::isDraggingWire(const GraphicalWireSegment* segment) const
{
  return wireDrag.segment == segment && wireDrag.router != nullptr;
}

void DiagramScene::updateWireDrag(GraphicalWireSegment* segment, const QPointF scenePos)
{
  if (!isDraggingWire(segment))
    return;
  QPointF delta = snapToGrid(scenePos) - wireDrag.start;
  const auto& original = wireDrag.originalPoints;
  if (wireDrag.type == WireDragKind::Edge) {
    if (original[wireDrag.index].y() == original[wireDrag.index + 1].y())
      delta.setX(0);
    else
      delta.setY(0);
  }
  if (delta.isNull()) {
    wireDrag.routes.cancel();
    return;
  }
  RoutePreview routes;
  if (wireDrag.type == WireDragKind::Bend)
    routes = wireDrag.router->routeWireBend(segment->getUiId(), wireDrag.index,
                                            segment->mapToScene(original[wireDrag.index]) + delta);
  else if (wireDrag.type == WireDragKind::Endpoint)
    routes = wireDrag.router->routeWireEndpoint(segment->getUiId(), wireDrag.index,
                                                segment->mapToScene(original[wireDrag.index])
                                                    + delta);
  else
    routes = wireDrag.router->routeWireEdge(segment->getUiId(), wireDrag.index, delta);
  if (routes)
    applyRoutingPreview(*routes, wireDrag.routes);
}

void DiagramScene::finishWireDrag(GraphicalWireSegment* segment)
{
  if (!isDraggingWire(segment))
    return;
  auto changes = wireDrag.routes.changes(*this);
  const bool movedEndpoint = wireDrag.type == WireDragKind::Endpoint;
  const bool movableFirst  = wireDrag.index == 0;
  WireEndpointEdit endpointEdit;
  if (movedEndpoint) {
    endpointEdit.segmentId = segment->getUiId();
    for (const auto& contact : wireDrag.routes.contacts)
      if (contact.segmentId == segment->getUiId() && contact.first == movableFirst
          && contact.neighborId)
        endpointEdit.beforeContacts.push_back({contact.neighborId, contact.segmentId,
                                               contact.first});

    const auto points = segment->getScenePoints();
    const QPointF endpoint = movableFirst ? points.front() : points.back();
    std::vector<GraphicalWire*> joiningWires{segment->getGraphicalWire()};
    for (auto* other : wireManager.getSegments()) {
      if (other == segment || other->empty()
          || !pointOnOrthogonalRoute(endpoint, other->getScenePoints()))
        continue;
      endpointEdit.afterContacts.push_back({other->getUiId(), segment->getUiId(),
                                            movableFirst});
      if (std::ranges::find(joiningWires, other->getGraphicalWire()) == joiningWires.end())
        joiningWires.push_back(other->getGraphicalWire());
    }
    if (!inspectConnection(joiningWires).isValid()) {
      cancelWireDrag();
      return;
    }
  }
  try {
    WireManager::Batch batch(wireManager);
    wireDrag.routes.commit(wireManager);
    if (!changes.empty())
      wireManager.rebuildFromGeometry();
    batch.commit();
  } catch (const std::invalid_argument&) {
    cancelWireDrag();
    return;
  }
  if (movedEndpoint) {
    for (const auto& [id, before] : wireDrag.routes.topologyBefore) {
      const auto after = wireManager.topologyFor(id);
      if (before.firstVertex != after.firstVertex
          || before.lastVertex != after.lastVertex
          || before.junctions != after.junctions
          || before.edgeIds != after.edgeIds)
        endpointEdit.topologyChanges.push_back({id, before, after});
    }
    for (const auto& [id, before] : wireDrag.routes.netBefore) {
      auto* member = category_cast<GraphicalWireSegment>(
          findGraphicalItemByUiId(id), ItemCategory::WireSegment);
      if (!member || !member->getGraphicalWire())
        continue;
      const auto* wire = member->getGraphicalWire();
      WireEndpointEdit::NetState after{wire->getRouteId(), wire->getBus()};
      if (before.routeId != after.routeId || !(before.bus == after.bus))
        endpointEdit.netChanges.push_back({id, before, after});
    }
  }
  const WireDragKind kind = wireDrag.type;
  wireDrag = {};
  if (!changes.empty() && undoStack)
    undoStack->push(new EditWireRouteCommand(segment, std::move(changes), kind,
                                             std::move(endpointEdit)));
}

void DiagramScene::cancelWireDrag()
{
  wireDrag.routes.cancel();
  wireDrag = {};
}

void DiagramScene::applyWireRouteChanges(std::span<const WireRouteChange> changes,
                                         const bool useAfter,
                                         const WireEndpointEdit& endpointEdit)
{
  WireManager::Batch batch(wireManager);
  for (const auto& change : changes) {
    auto* segment = category_cast<GraphicalWireSegment>(
        findGraphicalItemByUiId(change.uiId), ItemCategory::WireSegment);
    if (!segment)
      continue;
    WireManager::Edit edit(wireManager, segment);
    segment->setPoints(useAfter ? change.after : change.before);
    edit.commit();
  }
  wireManager.rebuildFromGeometry();
  batch.commit();
}

void DiagramScene::applyItemMoves(std::span<const ItemPositionChange> moves,
                                  std::span<const WireRouteChange> wires,
                                  const bool useAfter,
                                  const WireEndpointEdit& topology)
{
  const bool collisionChecks = itemCollisionChecksEnabled();
  setItemCollisionChecksEnabled(false);
  std::vector<std::pair<GraphicalItem*, QPointF>> previousPositions;
  std::vector<std::pair<uint64_t, QPointF>> wireTranslations;
  for (const auto& move : moves) {
    if (auto* item = findGraphicalItemByUiId(move.uiId)) {
      const QPointF target = useAfter ? move.after : move.before;
      previousPositions.emplace_back(item, item->pos());
      if (category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment))
        wireTranslations.emplace_back(move.uiId, target - item->pos());
      item->setPos(target);
      item->setInitialPosition();
    }
  }
  if (!wireManager.commitSegmentTranslations(wireTranslations)) {
    for (const auto& [item, position] : previousPositions)
      item->setPos(position);
    setItemCollisionChecksEnabled(collisionChecks);
    throw std::invalid_argument("Cannot restore moved wire topology");
  }
  applyWireRouteChanges(wires, useAfter);
  wireManager.rebuildFromGeometry();
  wireManager.refreshPortAnchors();
  requestConnectionsRefresh();
  setItemCollisionChecksEnabled(collisionChecks);
  update();
}

void DiagramScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  painter->fillRect(rect, ThemeEngine::getColor("SILICON_BACKGROUND"));

  QPen pen(ThemeEngine::getColor("SILICON_GRID"));
  painter->setPen(pen);

  const qreal left = int(rect.left()) - (int(rect.left()) % GRID_SIZE);
  const qreal top  = int(rect.top()) - (int(rect.top()) % GRID_SIZE);

  QVector<QPointF> points;
  for (qreal x = left; x < rect.right(); x += GRID_SIZE) {
    for (qreal y = top; y < rect.bottom(); y += GRID_SIZE) {
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
  if (!mixedDrag.empty()) {
    cancelMixedDrag();
    return true;
  }
  if (wireDrag.router) {
    cancelWireDrag();
    return true;
  }
  if (componentDrag.router) {
    cancelComponentDrag();
    return true;
  }
  if (currentInteractionMode == InteractionMode::NORMAL_MODE)
    return false;

  // Escape completes a drawn route even when one or both tips are free. A
  // one-point draft has no geometry and can simply be discarded.
  if (wireSegmentToBeDrawn)
    finalizeWireCreation();
  if (wireSegmentToBeDrawn)
    return true;  // A rejected join remains available for correction.

  setInteractionMode(InteractionMode::NORMAL_MODE);
  return true;
}

void DiagramScene::setInteractionMode(const InteractionMode newMode, const bool force)
{
  cancelMixedDrag();
  cancelWireDrag();
  cancelComponentDrag();
  const auto currentMode = getInteractionMode();
  if (currentMode == newMode && !force)
    return;

  if (!force && views().size() != 1)
    throw std::logic_error("setInteractionMode: scene must have exactly one view");

  clearSelection();

  if (wireSegmentToBeDrawn && newMode != InteractionMode::WIRE_CREATION_MODE)
    finalizeWireCreation();
  if (wireSegmentToBeDrawn && newMode != InteractionMode::WIRE_CREATION_MODE)
    return;

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
  // Only clicked points belong to the completed route; discard the red preview.
  wireSegmentToBeDrawn->setShowPoints({});
  wirePreviewRouteValid = true;

  // Remove the wireSegment if it's invisible
  if (wireSegmentToBeDrawn->empty()) {
    removeItem(wireSegmentToBeDrawn);
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  } else {
    auto* finalizedSegment = wireSegmentToBeDrawn;
    // Always register the segment so the WireManager tracks it for junctions
    // and collision detection
    try {
      wireManager.addSegment(finalizedSegment);
    } catch (const std::invalid_argument& error) {
      QMessageBox::warning(views().isEmpty() ? nullptr : views().first(),
                           "Cannot connect wire", error.what());
      return;
    }

    if (auto* undoStack = getUndoStack()) {
      undoStack->push(new WireTopologyCommand(this, {finalizedSegment},
                                              WireTopologyCommand::Operation::Add));
    }
    wireSegmentToBeDrawn = nullptr;
  }
  clearWireShadow();
}

bool DiagramScene::isWireCompletionPoint(const QPointF scenePoint) const
{
  // Ending on an existing segment creates (or extends) a wire junction.
  if (wireManager.segmentAtPoint(scenePoint, wireSegmentToBeDrawn))
    return true;

  // Use the same exact port positions as logical wire assignment does.
  for (const QGraphicsItem* item : items(scenePoint)) {
    const auto* component =
        category_cast<GraphicalComponent>(item, ItemCategory::Component);
    if (!component)
      continue;

    const auto portIsAtPoint = [component, scenePoint](const Port* port) {
      return component->mapToScene(port->getPosition()) == scenePoint;
    };

    if (std::ranges::any_of(component->getPorts(), portIsAtPoint))
      return true;
  }

  return false;
}

void DiagramScene::updateWirePreview(const QPointF scenePoint)
{
  if (!wireSegmentToBeDrawn)
    return;

  constexpr qreal MARGIN = GRID_SIZE / 2.0;
  std::vector<QRectF> obstacles;
  std::vector<QRectF> tipKeepouts;
  for (auto* item : items()) {
    const auto* component =
        category_cast<GraphicalComponent>(item, ItemCategory::Component);
    if (!component)
      continue;
    obstacles.push_back(component->mapToScene(component->collisionRectForWires())
                            .boundingRect().adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN));
  }

  const QPointF lastPoint =
      wireSegmentToBeDrawn->mapToScene(wireSegmentToBeDrawn->lastPoint());
  for (const auto* segment : wireManager.getSegments()) {
    const auto points = segment->getScenePoints();
    if (points.size() < 2)
      continue;
    for (const QPointF tip : {points.front(), points.back()}) {
      if (tip == lastPoint || tip == scenePoint)
        continue;
      const QRectF keepout(tip - QPointF(MARGIN, MARGIN),
                           QSizeF(2 * MARGIN, 2 * MARGIN));
      obstacles.push_back(keepout);
      tipKeepouts.push_back(keepout);
    }
  }
  auto route = core::routeOrthogonalWire(lastPoint, scenePoint, obstacles, GRID_SIZE);
  wirePreviewRouteValid = !route.empty()
      && !orthogonalRouteCrossesObstacleInterior(route, tipKeepouts);
  if (!wirePreviewRouteValid)
    route.clear();
  if (!route.empty())
    route.erase(route.begin());
  for (QPointF& point : route)
    point = QPointF(snapToGrid(point.x()), snapToGrid(point.y()));
  wireSegmentToBeDrawn->setShowPoints(route);
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
  const QPointF cursorPos = currentInteractionMode == InteractionMode::WIRE_CREATION_MODE
      ? resolveWireTarget(mouseEvent->scenePos())
      : QPointF(DiagramScene::snapToGrid(mouseEvent->scenePos()));

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
      updateWirePreview(cursorPos);
      break;
    }
    case InteractionMode::NORMAL_MODE:
    case InteractionMode::PAN_MODE:
    case InteractionMode::SIMULATION_MODE: break;
    default: throw std::logic_error("Unhandled InteractionMode in mouseMoveEvent");
  }
  QGraphicsScene::mouseMoveEvent(mouseEvent);
  if (currentInteractionMode == InteractionMode::NORMAL_MODE) {
    if (!mixedDrag.empty())
      updateMixedDrag();
    else
      updateComponentDrag();
  }
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent)
{
  const QPointF cursorPos = currentInteractionMode == InteractionMode::WIRE_CREATION_MODE
      ? resolveWireTarget(mouseEvent->scenePos())
      : QPointF(DiagramScene::snapToGrid(mouseEvent->scenePos()));

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

        placeComponent(lastPlacedComponentType, true, lastPlacedComponentProperties);
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
        updateWirePreview(cursorPos);
        if (!wirePreviewRouteValid) {
          QMessageBox::warning(views().isEmpty() ? nullptr : views().first(),
                               "Cannot connect wire",
                               "No valid orthogonal route avoids other wire endpoints");
          break;
        }
        wireSegmentToBeDrawn->addPoints();

        const QPointF endpoint =
            wireSegmentToBeDrawn->mapToScene(wireSegmentToBeDrawn->lastPoint());

        // Intermediate clicks remain available for routing bends. A port or existing
        // wire completes the route while wire creation mode stays ready for another.
        if (endpoint == cursorPos && isWireCompletionPoint(endpoint))
          finalizeWireCreation();
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
  if (currentInteractionMode == InteractionMode::NORMAL_MODE &&
      mouseEvent->button() == Qt::LeftButton) {
    if (hasWireGroupSelection() && !wireDrag.router) {
      beginMixedDrag();
    } else {
      for (auto* item = mouseGrabberItem(); item; item = item->parentItem()) {
        if (category_cast<GraphicalComponent>(item, ItemCategory::Component)) {
          beginComponentDrag();
          break;
        }
      }
    }
  }
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* mouseEvent)
{
  uint64_t clickedId = 0;
  if (currentInteractionMode == InteractionMode::NORMAL_MODE
      && mouseEvent->button() == Qt::LeftButton
      && !(mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
      && mouseEvent->screenPos() == mouseEvent->buttonDownScreenPos(Qt::LeftButton)) {
    for (auto* item = mouseGrabberItem(); item; item = item->parentItem()) {
      if (auto* component = category_cast<GraphicalComponent>(item,
                                                               ItemCategory::Component)) {
        clickedId = component->getUiId();
        break;
      }
      if (auto* wire = category_cast<GraphicalWireSegment>(item,
                                                           ItemCategory::WireSegment)) {
        clickedId = wire->getUiId();
        break;
      }
    }
  }

  QGraphicsScene::mouseReleaseEvent(mouseEvent);
  if (clickedId) {
    if (auto* clicked = findGraphicalItemByUiId(clickedId)) {
      clearSelection();
      clicked->setSelected(true);
    }
  }
}

void DiagramScene::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
    case Qt::Key_Escape: {
      // Escape commits a drawn wire with free endpoints; Shift+Escape explicitly
      // discards its draft instead.
      if (event->modifiers() & Qt::ShiftModifier) {
        if (wireSegmentToBeDrawn) {
          removeItem(wireSegmentToBeDrawn);
          delete wireSegmentToBeDrawn;
          wireSegmentToBeDrawn = nullptr;
          wirePreviewRouteValid = true;
          setInteractionMode(InteractionMode::NORMAL_MODE);
          event->accept();
          return;
        }
      }
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
  updateSceneAfterEdit();
}

void DiagramScene::showCSB(const QPointF pos)
{
  csb->setCompletionList(componentTypesForScene(*this));
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

void DiagramScene::handleInputToggled(Bus targetBus, BusValue value,
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

void DiagramScene::simulateEditedWaveform(const qulonglong    duration,
                                          std::vector<Sample> inputSnapshots)
{
  simulationController->simulateEditedWaveform(duration, std::move(inputSnapshots));
}

NetConnection DiagramScene::inspectConnection(const std::vector<GraphicalWire*>& wires) const
{
  NetConnection result;
  std::vector<QPointF> endpoints;
  for (const auto* wire : wires)
    for (const auto* segment : wire->getSegments()) {
      const auto points = segment->getScenePoints();
      if (points.size() >= 2) {
        endpoints.push_back(points.front());
        endpoints.push_back(points.back());
      }
    }
  std::optional<size_t> width;
  auto collect = [&](const GraphicalLogicComponent* component,
                     const std::vector<Port*>& ports, const std::vector<Bus>& buses,
                     const bool isOutput) {
    for (size_t index = 0; index < std::min(buses.size(), ports.size()); ++index) {
      Port* port = ports[index];
      const QPointF position = component->mapToScene(port->getPosition());

      const bool connected = std::ranges::contains(endpoints, position);
      const bool portAlreadyAdded = std::ranges::contains(result.affectedPorts, port);
      if (!connected || portAlreadyAdded)
        continue;

      result.affectedPorts.push_back(port);
      // Input buses retain the width of their previous binding. Validate them
      // through assignInputPortBus() when binding the output-sized net.
      if (!isOutput)
        continue;
      const auto portWidth = buses[index].size();
      if (!width)
        width = portWidth;
      else if (*width != portWidth && result.isValid()) {
        // Width mismatch
        result.error = QString("Port %1 requires %2 bits; this net requires %3 bits")
                            .arg(port->getName()).arg(portWidth).arg(*width);
      }
    }
  };

  for (auto* item : items()) {
    const auto* component = category_cast<GraphicalLogicComponent>(
        item, ItemCategory::LogicComponent);
    if (!component || !component->getComponent())
      continue;
    collect(component, component->getOutputPorts(),
            component->getComponent()->getOutputs(), true);
    collect(component, component->getInputPorts(),
            component->getComponent()->getInputs(), false);
  }

  result.width = width.value_or(1U);

  return result;
}

const ConnectionReport& DiagramScene::currentConnectionReport()
{
  if (connectionReportDirty)
    refreshConnections();
  return connectionReport;
}

void DiagramScene::requestConnectionsRefresh()
{
  if (refreshingConnections)
    return;
  connectionReportDirty = true;
  if (connectionRefreshScheduled)
    return;
  connectionRefreshScheduled = true;
  QTimer::singleShot(0, this, [this] {
    connectionRefreshScheduled = false;
    if (connectionReportDirty)
      refreshConnections();
  });
}

void DiagramScene::refreshConnections()
{
  // Prevent recursive calls
  if (refreshingConnections)
    return;
  QScopedValueRollback guard(refreshingConnections, true);

  connectionReportDirty = false;
  connectionReport.nets.clear();

  // clang-format off
  connectionReport.nets = wireManager.wires()
    | std::views::transform([this](const std::shared_ptr<GraphicalWire>& w) {
      return inspectConnection({w.get()});
    })
    | std::ranges::to<std::vector>();
  //clang-format on

  auto clearBindings = [this] {
    for (auto* item : items()) {
      auto* component = category_cast<GraphicalLogicComponent>(
          item, ItemCategory::LogicComponent);
      if (component && component->getComponent())
        component->getComponent()->clearWires();
    }
  };

  // Iterates over valid nets and associates their buses with connected ports
  auto bind = [this] {
    for (auto [index, wire] : wireManager.wires() | SILICON::views::enumerate) {
      auto& net = connectionReport.nets[index];

      if (!net.isValid())
        continue;

      if (wire->getBusSize() != net.width)
        wire->setBusSize(net.width);

      for (auto* item : items()) {
        auto* component = category_cast<GraphicalLogicComponent>(
            item, ItemCategory::LogicComponent);
        if (!component || !component->getComponent())
          continue;

        for (const auto& [portIndex, port] :
             component->getOutputPorts() | SILICON::views::enumerate)
          if (std::ranges::find(net.affectedPorts, port) != net.affectedPorts.end())
            component->getComponent()->setOutput(portIndex, wire->getBus());
        for (const auto& [portIndex, port] :
             component->getInputPorts() | SILICON::views::enumerate)
          if (std::ranges::find(net.affectedPorts, port) != net.affectedPorts.end()) {
            try {
              component->assignInputPortBus(portIndex, wire->getBus());
            } catch (const std::exception& error) {
              net.error = QString::fromUtf8(error.what());
            }
          }
      }
    }
  };

  clearBindings();
  bind();

  if (!connectionReport.valid()) {
    // A component's assignment rule can reject a width that passed the common
    // width check. Rebind only the remaining valid nets after clearing partial work.
    clearBindings();
    bind();
  }
  for (const auto [index, wire] : wireManager.wires() | SILICON::views::enumerate) {
    const auto& net = connectionReport.nets[index];
    wire->setConnectionError(net.error);
  }
  for (auto* item : items()) {
    auto* component = category_cast<GraphicalLogicComponent>(item,
                                                              ItemCategory::LogicComponent);
    if (!component)
      continue;
    for (Port* port : component->getPorts()) {
      port->setInputAssignmentError(false);
      port->setToolTip(port->getName());
    }
  }
  for (const auto& net : connectionReport.nets)
    if (!net.isValid())
      for (Port* port : net.affectedPorts) {
        port->setInputAssignmentError(true);
        port->setToolTip(net.error.value());
      }
  update();
}

bool DiagramScene::calculateWiresForComponents()
{
  return currentConnectionReport().valid();
}

void DiagramScene::addComponent(GraphicalComponent* component, QPointF pos)
{
  component->setPos(pos);
  addItem(component);
}

void DiagramScene::autoPlaceCircuit()
{
  setInteractionMode(InteractionMode::NORMAL_MODE);

  auto components = logicComponentsInScene(*this);
  if (components.empty())
    return;

  // A deserialized circuit is the authoritative topology. Reconstructing it from the
  // current drawing before autoplacement can import accidental visual overlaps as real
  // connections. For an edited scene the cache is invalidated by updateSceneAfterEdit,
  // so only then derive the circuit from graphical endpoints.
  std::shared_ptr<Circuit> authoritativeCircuit = getCircuit();
  if (!authoritativeCircuit
      || boost::num_vertices(authoritativeCircuit->getGraph()) == 0) {
    if (!calculateWiresForComponents())
      return;
    authoritativeCircuit =
        std::make_shared<Circuit>(coreComponentsFor(components), false);
  }
  const Circuit& activeCircuit = *authoritativeCircuit;

  CircuitAutoplacerOptions options;
  auto*           parent = views().isEmpty() ? nullptr : views().first()->window();
  QProgressDialog progress(tr("Finding a clean circuit layout..."), tr("Use best so far"),
                           0, options.candidateCount, parent);
  progress.setWindowTitle(tr("Auto place"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(300);
  progress.setValue(0);
  QApplication::processEvents();

  options.isCancelled = [&progress]() {
    QApplication::processEvents();
    return progress.wasCanceled();
  };
  options.progress = [&progress](const int completed, const int) {
    progress.setValue(completed);
    QApplication::processEvents();
  };

  applyAutoplacement(*this, activeCircuit, components, options);
}

void DiagramScene::placeComponent(std::string_view typeName)
{
  placeComponent(typeName, true);
}

void DiagramScene::placeComponent(std::string_view typeName, const bool showSearchBox)
{
  placeComponent(typeName, showSearchBox, {});
}

void DiagramScene::placeComponent(std::string_view typeName, const bool showSearchBox,
                                  const PropertyMap& initialProperties)
{
  if (componentToBeDrawn)
    throw std::logic_error("placeComponent: previous component not yet placed");

  componentToBeDrawn = GUIComponentFactory::instance().create(typeName).release();
  if (auto* subcircuit =
          dynamic_cast<GraphicalSubcircuitComponent*>(componentToBeDrawn)) {
    subcircuit->setDocumentStore(documents);
    subcircuit->setCircuitResolver(resolver);
  }
  if (auto* rom = dynamic_cast<GraphicalROM*>(componentToBeDrawn))
    rom->setDocumentStore(documents);
  lastPlacedComponentType       = typeName;
  lastPlacedComponentProperties = initialProperties;
  suppressNextComponentSearch   = !showSearchBox;

  if (!initialProperties.empty()) {
    if (auto* logicComponent = category_cast<GraphicalLogicComponent>(
            componentToBeDrawn, ItemCategory::LogicComponent)) {
      for (const auto& [key, value] : initialProperties)
        logicComponent->applyProperty(key, value);
    }
  }

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

std::shared_ptr<SceneLoadPlan>
DiagramScene::prepareDeserialize(const std::string&       jsonStr,
                                 GUIComponentFactory&     guiFactory,
                                 const ComponentRegistry& coreRegistry)
{
  return serializer->prepareDeserialize(jsonStr, guiFactory, coreRegistry);
}

void DiagramScene::applyDeserialize(std::shared_ptr<SceneLoadPlan> plan)
{
  serializer->applyDeserialize(std::move(plan));
}

void DiagramScene::loadCircuit(std::shared_ptr<Circuit> circuit,
                               GUIComponentFactory&     guiFactory,
                               const bool               resolveSubcircuitMetadata)
{
  serializer->loadCircuit(std::move(circuit), guiFactory, resolveSubcircuitMetadata);
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

  cancelMixedDrag();
  cancelWireDrag();
  cancelComponentDrag();
  WireManager::Batch edit(wireManager);
  const bool removedComponents = std::ranges::any_of(sceneItems, [](const auto* item) {
    return category_cast<GraphicalComponent>(item, ItemCategory::Component) != nullptr;
  });
  clearSelection();

  // Remove everything from the scene before deleting objects so scene callbacks never
  // observe partially-destroyed items.
  for (auto* item : sceneItems)
    removeItem(item);

  for (auto* item : sceneItems)
    delete item;

  if (removedComponents)
    wireManager.refreshPortAnchors();
  updateSceneAfterEdit();
  edit.commit();
}

bool DiagramScene::removeSelection(const nlohmann::json& payload)
{
  return serializer->removeSelection(payload);
}

void DiagramScene::updateSceneAfterEdit()
{
  wireManager.notifyElectricalChanged();
  update();
}

void DiagramScene::clear(const bool clearUndoStack, const bool clearLogs)
{
  cancelMixedDrag();
  cancelWireDrag();
  cancelComponentDrag();
  if (wireSegmentToBeDrawn) {
    removeItem(wireSegmentToBeDrawn);
    delete wireSegmentToBeDrawn;
    wireSegmentToBeDrawn = nullptr;
  }
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

  if (clearUndoStack && getUndoStack())
    getUndoStack()->clear();

  if (clearLogs)
    emit logsClearRequested();

  QGraphicsScene::clear();
  simulationController->clearWaveformTrace();
  connectionReport.nets.clear();
  connectionReportDirty = true;
}

DiagramScene::~DiagramScene()
{
  cancelMixedDrag();
  cancelWireDrag();
  cancelComponentDrag();
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

}  // namespace SILICON::ui
