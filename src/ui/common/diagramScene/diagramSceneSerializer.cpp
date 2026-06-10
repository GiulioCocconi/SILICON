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

#include "diagramSceneSerializer.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QPointer>

#include <logging/logger.hpp>

#include <core/circuit.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalUtils.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace {

const Logger uiLogger("ui scene");

struct PendingWireSegment {
  uint64_t             wireId;
  uint64_t             uiId;
  std::vector<QPointF> points;
};

ComponentRegistry registryWithIoComponents(const ComponentRegistry& coreRegistry)
{
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
  if (!mergedRegistry.hasType("DummyBusInputComponent")) {
    mergedRegistry.registerType("DummyBusInputComponent", [] {
      return std::make_shared<DummyBusInputComponent>(Bus(8), "bus_in");
    });
  }
  if (!mergedRegistry.hasType("DummyBusOutputComponent")) {
    mergedRegistry.registerType("DummyBusOutputComponent", [] {
      return std::make_shared<DummyBusOutputComponent>(Bus(8), "bus_out");
    });
  }

  return mergedRegistry;
}

std::shared_ptr<Circuit> deserializeCircuitPayload(const nlohmann::json&    payload,
                                                   const ComponentRegistry& coreRegistry)
{
  if (!payload.contains("circuit"))
    return nullptr;

  ComponentRegistry mergedRegistry = registryWithIoComponents(coreRegistry);
  return std::make_shared<Circuit>(
      Circuit::deserialize(payload["circuit"].dump(), mergedRegistry));
}

void attachCoreComponent(GraphicalComponent* component, const nlohmann::json& compJson,
                         const std::shared_ptr<Circuit>& coreCircuit)
{
  if (!component || !coreCircuit)
    return;

  auto* logicComp =
      category_cast<GraphicalLogicComponent>(component, ItemCategory::LogicComponent);
  if (!logicComp || !compJson.contains("vertexId"))
    return;

  const int vertexId = compJson["vertexId"].get<int>();
  auto      coreComp = coreCircuit->getComponentByVertexId(vertexId);
  if (!coreComp)
    return;

  logicComp->setComponent(coreComp);

  auto* io = category_cast<GraphicalIO>(logicComp, ItemCategory::IO);

  // Output backends remain UI-independent; their graphical state is refreshed by the
  // simulation controller on the GUI thread after each worker job.
  if (auto* gIn = dynamic_cast<GraphicalInput*>(io)) {
    QPointer<GraphicalInput> safeGIn(gIn);
    coreComp->setPropertyCallback("name", [safeGIn](const PropertyValue& value) {
      if (!safeGIn)
        return value;
      safeGIn->triggerGeometryChange();
      return value;
    });
  }
}

nlohmann::json componentJsonWithOffset(const nlohmann::json& compJson,
                                       const QPointF&        offset)
{
  auto result = compJson;
  if (result.contains("position")) {
    result["position"]["x"] = result["position"].value("x", 0.0) + offset.x();
    result["position"]["y"] = result["position"].value("y", 0.0) + offset.y();
  }
  return result;
}

QPointF payloadOrigin(const nlohmann::json& payload)
{
  if (!payload.contains("origin") || !payload["origin"].is_object())
    return {};

  return {payload["origin"].value("x", 0.0), payload["origin"].value("y", 0.0)};
}

uint64_t requiredUiId(const nlohmann::json& itemJson)
{
  if (!itemJson.contains("uiId"))
    throw std::runtime_error("Selection payload item missing uiId");

  return itemJson["uiId"].get<uint64_t>();
}

void remapPayloadUiIds(nlohmann::json& payload)
{
  if (!payload.contains("visual") || !payload["visual"].is_object())
    return;

  std::unordered_map<uint64_t, uint64_t> uiIdRemap;
  std::unordered_map<uint64_t, uint64_t> wireIdRemap;

  // Clipboard paste must get fresh runtime IDs while preserving references shared by
  // multiple serialized items within the same payload.
  auto remapItemIds = [&uiIdRemap](nlohmann::json& itemsJson) {
    if (!itemsJson.is_array())
      return;

    for (auto& itemJson : itemsJson) {
      if (!itemJson.contains("uiId"))
        continue;

      const auto oldUiId = itemJson["uiId"].get<uint64_t>();
      auto&      newUiId = uiIdRemap[oldUiId];

      if (newUiId == 0)
        newUiId = GraphicalItem::generateUiId();

      itemJson["uiId"] = newUiId;
    }
  };

  remapItemIds(payload["visual"]["components"]);
  remapItemIds(payload["visual"]["wires"]);

  auto& wiresJson = payload["visual"]["wires"];
  if (!wiresJson.is_array())
    return;

  for (auto& wireJson : wiresJson) {
    if (!wireJson.contains("wireId"))
      continue;

    const auto oldWireId = wireJson["wireId"].get<uint64_t>();
    auto&      newWireId = wireIdRemap[oldWireId];

    if (newWireId == 0)
      newWireId = GraphicalItem::generateUiId();

    // Distinct pasted wire groups must not reuse the source bus identity, otherwise
    // separate copies can be re-merged electrically by later topology reconstruction.
    wireJson["wireId"] = newWireId;
  }
}

std::vector<std::unique_ptr<GraphicalComponent>>
deserializeVisualComponents(const nlohmann::json& visual, GUIComponentFactory& guiFactory,
                            const std::shared_ptr<Circuit>& coreCircuit,
                            const QPointF&                  offset)
{
  // Builds graphical components without adding them to the scene, so callers can decide
  // whether insertion should select the new items.
  std::vector<std::unique_ptr<GraphicalComponent>> components;

  const auto visualComponents = visual.find("components");
  if (visualComponents == visual.end() || !visualComponents->is_array())
    return components;

  components.reserve(visualComponents->size());
  for (const auto& sourceCompJson : *visualComponents) {
    auto compJson  = componentJsonWithOffset(sourceCompJson, offset);
    auto component = GraphicalComponent::deserialize(compJson, guiFactory);
    if (!component)
      continue;

    attachCoreComponent(component.get(), compJson, coreCircuit);
    components.push_back(std::move(component));
  }

  return components;
}

std::vector<PendingWireSegment> deserializeVisualWires(const nlohmann::json& visual,
                                                       const QPointF&        offset,
                                                       const bool requireWireId)
{
  // Parses visual wire geometry while preserving shared wire IDs within the payload.
  std::vector<PendingWireSegment> wires;

  const auto visualWires = visual.find("wires");
  if (visualWires == visual.end() || !visualWires->is_array())
    return wires;

  wires.reserve(visualWires->size());
  uint64_t fallbackWireId = 1;

  for (const auto& wireJson : *visualWires) {
    if (!wireJson.contains("wireId") && requireWireId)
      throw std::runtime_error("Wire segment missing wireId");

    if (!wireJson.contains("points") || !wireJson["points"].is_array())
      continue;

    std::vector<QPointF> segmentPoints;
    segmentPoints.reserve(wireJson["points"].size());

    for (const auto& pointJson : wireJson["points"]) {
      segmentPoints.emplace_back(pointJson.value("x", 0.0) + offset.x(),
                                 pointJson.value("y", 0.0) + offset.y());
    }

    if (segmentPoints.size() < 2)
      continue;

    const uint64_t wireId = wireJson.contains("wireId")
                                ? wireJson["wireId"].get<uint64_t>()
                                : fallbackWireId++;
    wires.push_back({wireId, wireJson.value("uiId", GraphicalItem::generateUiId()),
                     std::move(segmentPoints)});
  }

  return wires;
}

void addVisualComponents(QGraphicsScene&                                  scene,
                         std::vector<std::unique_ptr<GraphicalComponent>> components,
                         const bool                                       selectInserted)
{
  // Transfers ownership of graphical components to the target scene.
  for (auto& component : components) {
    auto* item = component.release();
    scene.addItem(item);
    item->setSelected(selectInserted);
  }
}

void addVisualWires(QGraphicsScene& scene, WireManager& wireManager,
                    std::vector<PendingWireSegment> wires, const bool selectInserted)
{
  // Recreates wire objects lazily so segments with the same serialized ID share a bus.
  std::map<uint64_t, std::shared_ptr<GraphicalWire>> wireIdToWire;

  for (auto& pending : wires) {
    auto* segment = new GraphicalWireSegment(pending.points.front());
    segment->setUiId(pending.uiId);
    segment->setPoints(std::move(pending.points));

    // Rebuild each serialized wire group exactly once so all of its segments share the
    // same GraphicalWire and therefore the same logical bus.
    auto& wire = wireIdToWire[pending.wireId];
    if (!wire)
      wire = wireManager.createWire(1);

    scene.addItem(segment);
    segment->setGraphicalWire(wire.get());
    wireManager.addSegment(segment);
    segment->setSelected(selectInserted);
  }
}

}  // namespace

DiagramSceneSerializer::DiagramSceneSerializer(DiagramScene& scene) : scene(scene) {}

std::string DiagramSceneSerializer::serialize() const
{
  uiLogger.info("Serializing the scene...");
  nlohmann::ordered_json   j;
  std::shared_ptr<Circuit> activeCircuit = scene.getCircuit();

  if (!activeCircuit) {
    scene.calculateWiresForComponents();
    Component_set coreComps;
    for (auto* item : scene.items()) {
      auto* comp =
          category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
      if (comp && comp->getComponent())
        coreComps.insert(comp->getComponent());
    }
    activeCircuit = std::make_shared<Circuit>(coreComps, false);
  }

  j["circuit"] = nlohmann::json::parse(activeCircuit->serialize());

  nlohmann::ordered_json visualComponents = nlohmann::ordered_json::array();
  const auto&            compToVertexMap  = activeCircuit->getComponentToVertex();

  for (auto* item : scene.items()) {
    if (auto* comp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent)) {
      auto compJson = comp->serialize();
      compJson.erase("uiId");
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

  auto wiresJson = scene.getWireManager().getSegments()
                   | std::views::transform([](const auto& seg) {
                       auto wireJson = seg->serialize();
                       wireJson.erase("uiId");
                       return wireJson;
                     })
                   | std::ranges::to<std::vector>();

  j["visual"]["wires"] = wiresJson;
  return j.dump(2);
}

nlohmann::ordered_json DiagramSceneSerializer::serializeSelection() const
{
  return serializeItems(scene.selectedItems() | std::ranges::to<std::vector>());
}

nlohmann::ordered_json DiagramSceneSerializer::serializeItems(
    const std::vector<QGraphicsItem*>& sceneItems) const
{
  nlohmann::ordered_json payload;
  payload["format"]  = "silicon.logiflow.selection";
  payload["version"] = 1;

  nlohmann::ordered_json visualComponents = nlohmann::ordered_json::array();
  nlohmann::ordered_json visualWires      = nlohmann::ordered_json::array();

  Component_set selectedCoreComponents;
  for (auto* item : sceneItems) {
    if (const auto* comp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent)) {
      if (comp->getComponent())
        selectedCoreComponents.insert(comp->getComponent());
    }
  }

  std::shared_ptr<Circuit> selectedCircuit;
  if (!selectedCoreComponents.empty()) {
    selectedCircuit    = std::make_shared<Circuit>(selectedCoreComponents, false);
    payload["circuit"] = nlohmann::json::parse(selectedCircuit->serialize());
  }

  bool  hasOrigin = false;
  qreal minX      = std::numeric_limits<qreal>::max();
  qreal minY      = std::numeric_limits<qreal>::max();

  auto includePoint = [&](const QPointF& point) {
    hasOrigin = true;
    minX      = std::min(minX, point.x());
    minY      = std::min(minY, point.y());
  };

  for (auto* item : sceneItems) {
    auto* comp = category_cast<GraphicalComponent>(item, ItemCategory::Component);
    if (!comp)
      continue;

    auto compJson    = comp->serialize();
    compJson["type"] = comp->getTypeName();

    if (auto* logicComp =
            category_cast<GraphicalLogicComponent>(comp, ItemCategory::LogicComponent)) {
      if (selectedCircuit && logicComp->getComponent()) {
        const auto& compToVertexMap = selectedCircuit->getComponentToVertex();
        if (auto it = compToVertexMap.find(logicComp->getComponent().get());
            it != compToVertexMap.end()) {
          compJson["vertexId"] = static_cast<int>(it->second);
        }
      }
    }

    if (compJson.contains("position")) {
      includePoint(QPointF(compJson["position"].value("x", comp->pos().x()),
                           compJson["position"].value("y", comp->pos().y())));
    }
    visualComponents.push_back(std::move(compJson));
  }

  for (auto* item : sceneItems) {
    const auto* segment =
        category_cast<GraphicalWireSegment>(item, ItemCategory::WireSegment);
    if (!segment)
      continue;

    auto wireJson = segment->serialize();
    if (wireJson.contains("points") && wireJson["points"].is_array()) {
      for (const auto& pointJson : wireJson["points"]) {
        includePoint(QPointF(pointJson.value("x", 0.0), pointJson.value("y", 0.0)));
      }
    }
    visualWires.push_back(std::move(wireJson));
  }

  payload["origin"] = hasOrigin ? nlohmann::ordered_json{{"x", minX}, {"y", minY}}
                                : nlohmann::ordered_json{{"x", 0.0}, {"y", 0.0}};
  payload["visual"]["components"] = std::move(visualComponents);
  payload["visual"]["wires"]      = std::move(visualWires);

  return payload;
}

void DiagramSceneSerializer::deserialize(const std::string&       jsonStr,
                                         GUIComponentFactory&     guiFactory,
                                         const ComponentRegistry& coreRegistry)
{
  auto j = nlohmann::json::parse(jsonStr);

  if (j.contains("circuit"))
    scene.setCircuit(deserializeCircuitPayload(j, coreRegistry));

  if (!j.contains("visual"))
    throw std::runtime_error("Opened file doesn't have the visual part!");

  addVisualWires(scene, scene.getWireManager(),
                 deserializeVisualWires(j["visual"], QPointF(), true), false);
  addVisualComponents(
      scene,
      deserializeVisualComponents(j["visual"], guiFactory, scene.getCircuit(), QPointF()),
      false);

  scene.setInteractionMode(InteractionMode::NORMAL_MODE);
}

bool DiagramSceneSerializer::insertSelection(const nlohmann::json&    payload,
                                             GUIComponentFactory&     guiFactory,
                                             const ComponentRegistry& coreRegistry,
                                             QPointF targetOrigin, const bool isPaste)
{
  if (!payload.is_object() || !payload.contains("visual")
      || !payload["visual"].is_object()) {
    return false;
  }

  auto remappedPayload = payload;
  if (isPaste)
    remapPayloadUiIds(remappedPayload);

  const QPointF pasteOffset =
      DiagramScene::snapToGrid(targetOrigin - payloadOrigin(remappedPayload));
  auto pastedCircuit     = deserializeCircuitPayload(remappedPayload, coreRegistry);
  auto pendingComponents = deserializeVisualComponents(
      remappedPayload["visual"], guiFactory, pastedCircuit, pasteOffset);
  auto pendingWires =
      deserializeVisualWires(remappedPayload["visual"], pasteOffset, true);

  if (pendingComponents.empty() && pendingWires.empty())
    return false;

  scene.clearSelection();

  addVisualComponents(scene, std::move(pendingComponents), true);
  addVisualWires(scene, scene.getWireManager(), std::move(pendingWires), true);

  scene.getWireManager().calculateJunctions();
  scene.updateSceneAfterEdit();
  return true;
}

bool DiagramSceneSerializer::removeSelection(const nlohmann::json& payload)
{
  if (!payload.is_object() || !payload.contains("visual")
      || !payload["visual"].is_object()) {
    return false;
  }

  std::vector<QGraphicsItem*>        itemsToDelete;
  std::unordered_set<QGraphicsItem*> queuedItems;

  auto queueItem = [&](const nlohmann::json& itemJson,
                       const ItemCategory    expectedCategory) {
    auto* item = scene.findGraphicalItemByUiId(requiredUiId(itemJson));
    if (!item || !category_cast<GraphicalItem>(item, expectedCategory))
      return false;

    if (queuedItems.insert(item).second)
      itemsToDelete.push_back(item);
    return true;
  };

  if (const auto visualComponents = payload["visual"].find("components");
      visualComponents != payload["visual"].end() && visualComponents->is_array()) {
    for (const auto& compJson : *visualComponents) {
      if (!queueItem(compJson, ItemCategory::Component))
        return false;
    }
  }

  if (const auto visualWires = payload["visual"].find("wires");
      visualWires != payload["visual"].end() && visualWires->is_array()) {
    for (const auto& wireJson : *visualWires) {
      if (!queueItem(wireJson, ItemCategory::WireSegment))
        return false;
    }
  }

  if (itemsToDelete.empty())
    return false;

  scene.removeItems(itemsToDelete);
  return true;
}
