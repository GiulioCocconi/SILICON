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

#include "metadata.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/circuit/diagram/enums.hpp>
#include <ui/serialization/gui_component_factory.hpp>
#include <utils/transparent_string.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

constexpr ItemCategory INPUT_CATEGORY  = ItemCategory::IO | ItemCategory::Input;
constexpr ItemCategory OUTPUT_CATEGORY = ItemCategory::IO | ItemCategory::Output;

[[nodiscard]] int gridToPixels(const int value)
{
  return value * DiagramScene::GRID_SIZE;
}

[[nodiscard]] int pixelsToGrid(const int value)
{
  return value / DiagramScene::GRID_SIZE;
}

[[nodiscard]] QPoint gridToPixels(const QPoint& point)
{
  return {gridToPixels(point.x()), gridToPixels(point.y())};
}

[[nodiscard]] QPoint pixelsToGrid(const QPoint& point)
{
  return {pixelsToGrid(point.x()), pixelsToGrid(point.y())};
}

[[nodiscard]] GraphicalSubcircuitPortMetadata parsePort(const nlohmann::json& json)
{
  // Port coordinates in JSON are relative to the rectangle top-left, in grid units.
  return {.name     = json.value("name", std::string()),
          .position = gridToPixels(QPoint(json.value("x", 0), json.value("y", 0))),
          .busSize  = static_cast<unsigned int>(std::max(1, json.value("busSize", 1)))};
}

[[nodiscard]] nlohmann::json portToJson(const GraphicalSubcircuitPortMetadata& port)
{
  // Keep project files grid-based even though QGraphicsItems consume pixels.
  const auto gridPosition = pixelsToGrid(port.position);
  return {{"name", port.name},
          {"x", gridPosition.x()},
          {"y", gridPosition.y()},
          {"busSize", port.busSize}};
}

using WireId     = std::optional<std::uint64_t>;
using BusWireIds = std::vector<WireId>;

struct PortDefinition {
  std::string  name;
  unsigned int busSize = 1;
  QPoint       visualPosition;
  BusWireIds   wireIds;
};

enum class BoundaryRole { None, Input, Output };

[[nodiscard]] BoundaryRole boundaryRoleForType(const std::string_view type)
{
  static std::unordered_map<std::string, BoundaryRole, TransparentStringHash,
                            TransparentStringEqual>
      boundaryRoleByType;

  if (const auto it = boundaryRoleByType.find(type); it != boundaryRoleByType.end())
    return it->second;

  auto role = BoundaryRole::None;
  if (GUIComponentFactory::instance().hasType(type)) {
    auto component = GUIComponentFactory::instance().create(type);
    if (hasCategory(component.get(), INPUT_CATEGORY))
      role = BoundaryRole::Input;
    else if (hasCategory(component.get(), OUTPUT_CATEGORY))
      role = BoundaryRole::Output;
  }

  return boundaryRoleByType.emplace(type, role).first->second;
}

[[nodiscard]] bool isBoundaryType(const std::string_view type, const bool inputPort)
{
  const auto role = boundaryRoleForType(type);
  return inputPort ? role == BoundaryRole::Input : role == BoundaryRole::Output;
}

[[nodiscard]] nlohmann::json metadataToJson(const GraphicalSubcircuitMetadata& metadata)
{
  nlohmann::json inputs = nlohmann::json::array();
  for (const auto& port : metadata.inputs)
    inputs.push_back(portToJson(port));

  nlohmann::json outputs = nlohmann::json::array();
  for (const auto& port : metadata.outputs)
    outputs.push_back(portToJson(port));

  return {{"shape",
           {{"type", "rectangle"},
            {"autoSize", metadata.autoSize},
            {"width", pixelsToGrid(metadata.widthHeight.width())},
            {"height", pixelsToGrid(metadata.widthHeight.height())}}},
          {"inputs", std::move(inputs)},
          {"outputs", std::move(outputs)}};
}

[[nodiscard]] BusWireIds busWireIdsFromJson(const nlohmann::json& bus)
{
  BusWireIds wireIds;
  if (!bus.is_array())
    return wireIds;

  wireIds.reserve(bus.size());
  for (const auto& wire : bus) {
    if (wire.is_null())
      wireIds.push_back(std::nullopt);
    else if (wire.is_number_integer())
      wireIds.push_back(wire.get<std::uint64_t>());
  }

  return wireIds;
}

[[nodiscard]] BusWireIds exposedBoundaryBusWireIds(const nlohmann::json& coreComponent,
                                                   const bool            inputPort)
{
  const auto buses = coreComponent.find(inputPort ? "outputs" : "inputs");
  if (buses == coreComponent.end() || !buses->is_array() || buses->empty()
      || !buses->front().is_array()) {
    return {};
  }
  return busWireIdsFromJson(buses->front());
}

[[nodiscard]] BusWireIds busWireIds(const Bus& bus)
{
  BusWireIds wireIds;
  wireIds.reserve(bus.size());
  for (const auto& wire : bus) {
    if (wire)
      wireIds.push_back(wire->getId());
    else
      wireIds.push_back(std::nullopt);
  }
  return wireIds;
}

[[nodiscard]] std::vector<BusWireIds>
interfaceBusWireIdsFromDocument(const nlohmann::json& document, const bool inputPort)
{
  try {
    const auto definition =
        parseCircuitDocument(document.dump(), ComponentRegistry::instance());
    const auto& selectedPorts = inputPort ? definition.inputs : definition.outputs;

    std::vector<BusWireIds> ids;
    ids.reserve(selectedPorts.size());
    for (const auto& port : selectedPorts)
      ids.push_back(busWireIds(port.bus));
    return ids;
  } catch (const std::exception&) {
    return {};
  }
}

[[nodiscard]] std::vector<PortDefinition>
orderDefinitionsByInterfaceIds(const nlohmann::json&       document,
                               std::vector<PortDefinition> definitions,
                               const bool                  inputPort)
{
  const auto interfaceBusIds = interfaceBusWireIdsFromDocument(document, inputPort);
  if (interfaceBusIds.size() != definitions.size())
    return definitions;

  if (std::ranges::any_of(interfaceBusIds,
                          [](const BusWireIds& busIds) { return busIds.empty(); })
      || std::ranges::any_of(definitions, [](const PortDefinition& definition) {
           return definition.wireIds.empty();
         })) {
    return definitions;
  }

  std::vector<std::size_t> orderedIndexes;
  orderedIndexes.reserve(definitions.size());
  std::vector<bool> used(definitions.size(), false);

  for (const auto& busIds : interfaceBusIds) {
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
      if (!used[index] && definitions[index].wireIds == busIds) {
        match = index;
        break;
      }
    }

    if (!match)
      return definitions;

    used[*match] = true;
    orderedIndexes.push_back(*match);
  }

  std::vector<PortDefinition> ordered;
  ordered.reserve(definitions.size());
  for (const auto index : orderedIndexes)
    ordered.push_back(std::move(definitions[index]));

  return ordered;
}

[[nodiscard]] std::vector<PortDefinition>
portDefinitionsFromDocument(const nlohmann::json& document, bool inputPort)
{
  std::vector<PortDefinition> definitions;

  const auto circuitIt = document.find("circuit");
  if (circuitIt == document.end() || !circuitIt->is_object())
    return definitions;

  const auto coreComponentsIt = circuitIt->find("components");
  if (coreComponentsIt == circuitIt->end() || !coreComponentsIt->is_array())
    return definitions;

  std::unordered_map<int, const nlohmann::json*> coreComponentsById;
  for (const auto& component : *coreComponentsIt) {
    if (!component.contains("id") || !component["id"].is_number_integer())
      continue;
    coreComponentsById.emplace(component["id"].get<int>(), &component);
  }

  const auto visualIt = document.find("visual");
  if (visualIt != document.end() && visualIt->is_object()) {
    const auto componentsIt = visualIt->find("components");
    if (componentsIt != visualIt->end() && componentsIt->is_array()) {
      for (const auto& component : *componentsIt) {
        const auto typeIt = component.find("type");
        const std::string_view type =
            typeIt == component.end() ? std::string_view{}
                                      : typeIt->get_ref<const std::string&>();
        if (!isBoundaryType(type, inputPort))
          continue;
        if (!component.contains("vertexId")
            || !component["vertexId"].is_number_integer()) {
          continue;
        }

        const auto coreIt = coreComponentsById.find(component["vertexId"].get<int>());
        if (coreIt == coreComponentsById.end())
          continue;

        const auto& coreComponent = *coreIt->second;
        QPoint      position;
        if (const auto positionIt = component.find("position");
            positionIt != component.end() && positionIt->is_object()) {
          position = QPoint(static_cast<int>(positionIt->value("x", 0.0)),
                            static_cast<int>(positionIt->value("y", 0.0)));
        }

        std::string name = inputPort ? "input" : "output";
        if (const auto propertiesIt = coreComponent.find("properties");
            propertiesIt != coreComponent.end() && propertiesIt->is_object()) {
          name = propertiesIt->value("name", name);
        }

        auto wireIds = exposedBoundaryBusWireIds(coreComponent, inputPort);
        definitions.push_back({.name    = std::move(name),
                               .busSize = static_cast<unsigned int>(
                                   std::max<std::size_t>(1, wireIds.size())),
                               .visualPosition = position,
                               .wireIds        = std::move(wireIds)});
      }
    }
  }

  if (definitions.empty()) {
    try {
      const auto circuit =
          Circuit::deserialize(circuitIt->dump(), ComponentRegistry::instance());
      const auto& ports = inputPort ? circuit.getInputPorts() : circuit.getOutputPorts();
      for (const auto& port : ports) {
        definitions.push_back({.name    = port.name,
                               .busSize = static_cast<unsigned int>(
                                   std::max<std::size_t>(1, port.bus.size())),
                               .visualPosition = {},
                               .wireIds        = busWireIds(port.bus)});
      }
    } catch (const std::exception&) {
      return {};
    }
  }

  // Use visual order as the stable identity order for duplicate names and as fallback.
  std::ranges::sort(definitions,
                    [](const PortDefinition& lhs, const PortDefinition& rhs) {
                      if (lhs.visualPosition.y() != rhs.visualPosition.y())
                        return lhs.visualPosition.y() < rhs.visualPosition.y();
                      return lhs.visualPosition.x() < rhs.visualPosition.x();
                    });

  // Boundary component names may collide; visible subcircuit ports still need unique
  // names.
  std::unordered_map<std::string, int> seenNames;
  for (auto& definition : definitions) {
    if (definition.name.empty())
      definition.name = inputPort ? "input" : "output";
    const int seen = seenNames[definition.name]++;
    if (seen != 0)
      definition.name = std::format("{}{}", definition.name, seen + 1);
  }

  return orderDefinitionsByInterfaceIds(document, std::move(definitions), inputPort);
}

[[nodiscard]] int distributedPortY(int height, std::size_t index, std::size_t count)
{
  // Recognize positions generated by the previous divide-and-round layout.
  return gridToPixels(static_cast<int>(std::lround(
      (index + 1) * (pixelsToGrid(height) / static_cast<double>(count + 1)))));
}

[[nodiscard]] QPoint
defaultExternalPortPosition(const GraphicalSubcircuitMetadata& metadata, bool inputPort,
                            std::size_t index, std::size_t count)
{
  const int width  = metadata.widthHeight.width();
  const int height = metadata.widthHeight.height();
  const int x      = inputPort ? gridToPixels(-2) : width + gridToPixels(2);
  // Center the group with one empty grid row between neighboring ports.
  const int y = height / 2
                + gridToPixels(2 * static_cast<int>(index) - static_cast<int>(count) + 1);
  return {x, y};
}

[[nodiscard]] bool isDefaultPortPosition(const GraphicalSubcircuitMetadata& metadata,
                                         bool inputPort, std::size_t index)
{
  const auto& ports = inputPort ? metadata.inputs : metadata.outputs;
  const auto  expected =
      defaultExternalPortPosition(metadata, inputPort, index, ports.size());
  const auto position = ports[index].position;
  // Recognize defaults saved before fixed spacing was introduced as well.
  return position == expected
         || (position.x() == expected.x()
             && position.y()
                    == distributedPortY(metadata.widthHeight.height(), index,
                                        ports.size()));
}

[[nodiscard]] std::vector<GraphicalSubcircuitPortMetadata>
synchronizePorts(const std::vector<PortDefinition>& definitions,
                 const GraphicalSubcircuitMetadata& fallbackMetadata,
                 const GraphicalSubcircuitMetadata& metadata, bool inputPort)
{
  const auto& fallbackPorts =
      inputPort ? fallbackMetadata.inputs : fallbackMetadata.outputs;
  std::vector<GraphicalSubcircuitPortMetadata> synchronized;
  synchronized.reserve(definitions.size());
  std::vector<bool> usedFallback(fallbackPorts.size(), false);

  for (std::size_t i = 0; i < definitions.size(); ++i) {
    const auto&                definition = definitions[i];
    std::optional<std::size_t> match;

    for (std::size_t fallbackIndex = 0; fallbackIndex < fallbackPorts.size();
         ++fallbackIndex) {
      if (!usedFallback[fallbackIndex]
          && fallbackPorts[fallbackIndex].name == definition.name) {
        match = fallbackIndex;
        break;
      }
    }

    if (!match && i < fallbackPorts.size() && !usedFallback[i])
      match = i;

    // Reflow generated ports while preserving explicitly moved ports.
    const bool useDefaultPosition =
        !match || isDefaultPortPosition(fallbackMetadata, inputPort, *match);
    const QPoint position =
        useDefaultPosition
            ? defaultExternalPortPosition(metadata, inputPort, i, definitions.size())
            : fallbackPorts[*match].position;
    if (match)
      usedFallback[*match] = true;

    synchronized.push_back(
        {.name = definition.name, .position = position, .busSize = definition.busSize});
  }

  return synchronized;
}

[[nodiscard]] std::string fallbackPortName(const bool inputPort, const std::size_t index)
{
  const std::string base = inputPort ? "input" : "output";
  return index == 0 ? base : std::format("{}{}", base, index + 1);
}

}  // namespace

QSize defaultGraphicalSubcircuitSize(std::size_t inputCount, std::size_t outputCount)
{
  const int size = gridToPixels(
      2 * static_cast<int>(std::max({std::size_t{1}, inputCount, outputCount})));
  return {size, size};
}

std::optional<GraphicalSubcircuitMetadata>
parseGraphicalSubcircuitMetadata(std::string_view sceneJson)
{
  try {
    const auto document = nlohmann::json::parse(sceneJson);
    if (!document.contains("graphicalComponent")
        || !document["graphicalComponent"].is_object()) {
      return std::nullopt;
    }

    const auto& graphical = document["graphicalComponent"];
    const auto& shape     = graphical["shape"];
    if (!shape.is_object() || shape.value("type", std::string()) != "rectangle")
      return std::nullopt;

    GraphicalSubcircuitMetadata metadata;
    // Existing documents with explicit dimensions retain their saved shape.
    metadata.autoSize =
        shape.value("autoSize", !shape.contains("width") && !shape.contains("height"));
    metadata.widthHeight = QSize(
        gridToPixels(std::max(1, shape.value("width", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE))),
        gridToPixels(std::max(1, shape.value("height", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE))));

    if (const auto inputs = graphical.find("inputs");
        inputs != graphical.end() && inputs->is_array()) {
      for (const auto& input : *inputs)
        metadata.inputs.push_back(parsePort(input));
    }
    if (const auto outputs = graphical.find("outputs");
        outputs != graphical.end() && outputs->is_array()) {
      for (const auto& output : *outputs)
        metadata.outputs.push_back(parsePort(output));
    }

    if (metadata.autoSize)
      metadata.widthHeight =
          defaultGraphicalSubcircuitSize(metadata.inputs.size(), metadata.outputs.size());

    return metadata;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

bool subcircuitHasGraphicalMetadata(std::string_view sceneJson)
{
  return parseGraphicalSubcircuitMetadata(sceneJson).has_value();
}

GraphicalSubcircuitMetadata synchronizeGraphicalSubcircuitMetadata(
    std::string_view sceneJson, const GraphicalSubcircuitMetadata& fallbackMetadata)
{
  try {
    const auto document = nlohmann::json::parse(sceneJson);
    auto       metadata = fallbackMetadata;
    const auto inputs   = portDefinitionsFromDocument(document, true);
    const auto outputs  = portDefinitionsFromDocument(document, false);
    if (metadata.autoSize) {
      metadata.widthHeight =
          defaultGraphicalSubcircuitSize(inputs.size(), outputs.size());
    } else {
      // Older default rectangles have explicit dimensions. Give generated groups
      // enough height for fixed spacing, without resizing fully custom groups.
      auto requiredHeight = [&](bool inputPort, std::size_t count) {
        const auto& ports =
            inputPort ? fallbackMetadata.inputs : fallbackMetadata.outputs;
        bool generated = count > ports.size();
        for (std::size_t index = 0; index < ports.size() && !generated; ++index)
          generated = isDefaultPortPosition(fallbackMetadata, inputPort, index);
        return generated ? gridToPixels(2 * static_cast<int>(count)) : 0;
      };
      metadata.widthHeight.setHeight(
          std::max({metadata.widthHeight.height(), requiredHeight(true, inputs.size()),
                    requiredHeight(false, outputs.size())}));
    }
    metadata.inputs  = synchronizePorts(inputs, fallbackMetadata, metadata, true);
    metadata.outputs = synchronizePorts(outputs, fallbackMetadata, metadata, false);
    return metadata;
  } catch (const nlohmann::json::exception&) {
    return fallbackMetadata;
  }
}

std::vector<GraphicalSubcircuitPortMetadata> synchronizePortsWithBuses(
    const std::vector<GraphicalSubcircuitPortMetadata>& fallbackPorts,
    const std::vector<Bus>& buses, const GraphicalSubcircuitMetadata& metadata,
    const bool inputPort)
{
  std::vector<GraphicalSubcircuitPortMetadata> synchronized;
  synchronized.reserve(buses.size());

  for (std::size_t i = 0; i < buses.size(); ++i) {
    const bool hasFallback = i < fallbackPorts.size();
    synchronized.push_back(
        {.name     = hasFallback && !fallbackPorts[i].name.empty()
                         ? fallbackPorts[i].name
                         : fallbackPortName(inputPort, i),
         .position = hasFallback ? fallbackPorts[i].position
                                 : defaultExternalPortPosition(metadata, inputPort, i,
                                                               buses.size()),
         .busSize =
             static_cast<unsigned int>(std::max<std::size_t>(1, buses[i].size()))});
  }

  return synchronized;
}

nlohmann::ordered_json
graphicalSubcircuitMetadataToJson(const GraphicalSubcircuitMetadata& metadata)
{
  return metadataToJson(metadata);
}

}  // namespace SILICON::ui
