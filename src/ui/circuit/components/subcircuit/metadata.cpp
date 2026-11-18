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
#include <format>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/circuit/components/subcircuit/geometry.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  /** Read the shape-level mode, preserving coordinates saved before modes existed. */
  PortPositioning readLayoutPositioning(const nlohmann::json& graphical)
  {
    const auto& shape = graphical.at("shape");
    if (const auto mode = shape.find("positioning"); mode != shape.end()) {
      if (*mode == "automatic")
        return PortPositioning::Automatic;
      if (*mode == "manual")
        return PortPositioning::Manual;
      throw std::invalid_argument("Invalid port positioning mode");
    }
    // Old graphical documents always persisted coordinates, even for defaults.
    // Preserve them on migration; new documents explicitly choose a mode.
    return PortPositioning::Manual;
  }

[[nodiscard]] GraphicalSubcircuitPortMetadata parsePort(const nlohmann::json& json)
{
  // Port coordinates in JSON are relative to the rectangle top-left, in grid units.
  return {.name     = json.value("name", std::string()),
          .position = gridToPixels(QPoint(json.value("x", 0), json.value("y", 0))),
          .busSize  = static_cast<unsigned int>(std::max(1, json.value("busSize", 1))),
          .id       = json.contains("id") ? deserializePortId(json.at("id")) : 0};
}

[[nodiscard]] nlohmann::json portToJson(const GraphicalSubcircuitPortMetadata& port)
{
  // Keep project files grid-based even though QGraphicsItems consume pixels.
  const auto gridPosition = pixelsToGrid(port.position);
  nlohmann::json result       = {{"name", port.name},
                                 {"x", gridPosition.x()},
                                 {"y", gridPosition.y()},
                                 {"busSize", port.busSize}};
  if (port.id != 0)
    result["id"] = port.id;
  return result;
}

struct PortDefinition {
  std::string  name;
  unsigned int busSize;
  uint64_t     id;
};

// Visual positions affect display-name suffixes only. Semantic order and widths
// always come from the core interface, including ports that share the same wires.
[[nodiscard]] std::vector<PortDefinition>
portDefinitions(const std::vector<CircuitPort>& ports, const nlohmann::json& document,
                bool inputPort)
{
  std::unordered_map<VertexDescriptor, QPoint> positions;
  if (const auto visual = document.find("visual");
      visual != document.end() && visual->is_object()) {
    if (const auto components = visual->find("components");
        components != visual->end() && components->is_array()) {
      for (const auto& component : *components) {
        const auto vertex   = component.find("vertexId");
        const auto position = component.find("position");
        if (vertex != component.end() && vertex->is_number_integer()
            && position != component.end() && position->is_object()) {
          positions.emplace(vertex->get<VertexDescriptor>(),
                            QPoint(static_cast<int>(position->value("x", 0.0)),
                                   static_cast<int>(position->value("y", 0.0))));
        }
      }
    }
  }

  std::vector<std::size_t> visualOrder(ports.size());
  std::iota(visualOrder.begin(), visualOrder.end(), 0);
  auto position = [&](std::size_t index) {
    const auto vertex = ports[index].boundaryVertex;
    const auto it     = vertex ? positions.find(*vertex) : positions.end();
    return it == positions.end() ? QPoint{} : it->second;
  };
  std::ranges::stable_sort(visualOrder, [&](std::size_t lhs, std::size_t rhs) {
    const auto a = position(lhs);
    const auto b = position(rhs);
    return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x();
  });

  std::vector<PortDefinition>          definitions(ports.size());
  std::unordered_map<std::string, int> seenNames;
  for (const auto index : visualOrder) {
    const auto& port = ports[index];
    auto        name =
        port.name.empty() ? std::string(inputPort ? "input" : "output") : port.name;
    const int seen = seenNames[name]++;
    if (seen != 0)
      name = std::format("{}{}", name, seen + 1);
    definitions[index] = {
        .name    = std::move(name),
        .busSize = static_cast<unsigned int>(std::max<std::size_t>(1, port.bus.size())),
        .id      = port.id};
  }
  return definitions;
}

[[nodiscard]] std::optional<std::size_t>
findMatchingPort(const PortDefinition& definition, std::size_t index,
                 const std::vector<GraphicalSubcircuitPortMetadata>& fallbackPorts,
                 const std::vector<bool>&                            usedFallback)
{
  const auto candidates = std::views::iota(std::size_t{0}, fallbackPorts.size());
  auto       find       = [&](const auto& predicate) -> std::optional<std::size_t> {
    const auto match = std::ranges::find_if(candidates, [&](std::size_t candidate) {
      return !usedFallback[candidate] && predicate(fallbackPorts[candidate]);
    });
    if (match == candidates.end())
      return std::nullopt;
    return *match;
  };

  if (definition.id != 0) {
    if (const auto match =
            find([&](const auto& port) { return port.id == definition.id; }))
      return match;
  }

  if (const auto match = find(
          [&](const auto& port) { return port.id == 0 && port.name == definition.name; }))
    return match;

  if (index < fallbackPorts.size() && !usedFallback[index]
      && fallbackPorts[index].id == 0)
    return index;
  return std::nullopt;
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
  const bool        layoutChanged = metadata.widthHeight != fallbackMetadata.widthHeight
                             || definitions.size() != fallbackPorts.size();
  for (std::size_t i = 0; i < definitions.size(); ++i) {
    const auto& definition = definitions[i];
    const auto  match      = findMatchingPort(definition, i, fallbackPorts, usedFallback);

    // A reorder alone keeps each identity's pin location. Automatic positions
    // reflow when the shape or port count changes; manual positions never reflow.
    const bool placeAutomatically =
        !match || (metadata.positioning == PortPositioning::Automatic && layoutChanged);
    const QPoint position = placeAutomatically
                                ? automaticPortPosition(metadata.widthHeight, inputPort,
                                                        i, definitions.size())
                                : fallbackPorts[*match].position;
    if (match)
      usedFallback[*match] = true;

    synchronized.push_back({.name     = definition.name,
                            .position = position,
                            .busSize  = definition.busSize,
                            .id       = definition.id});
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
  // Leave half a port spacing above and below the taller column of ports.
  const auto rows = std::max({std::size_t{1}, inputCount, outputCount});
  const int  size = gridToPixels(EXTERNAL_PORT_SPACING_GRID * static_cast<int>(rows));
  return {size, size};
}

std::optional<GraphicalSubcircuitMetadata>
parseGraphicalSubcircuitMetadata(const nlohmann::json& document)
{
  try {
    if (!document.contains("graphicalComponent")
        || !document["graphicalComponent"].is_object()) {
      return std::nullopt;
    }

    const auto& graphical = document["graphicalComponent"];
    const auto& shape     = graphical.at("shape");
    if (!shape.is_object() || shape.value("type", std::string()) != "rectangle")
      return std::nullopt;

    GraphicalSubcircuitMetadata metadata;
    metadata.positioning = readLayoutPositioning(graphical);
    // Existing documents with explicit dimensions retain their saved shape.
    metadata.autoSize =
        shape.value("autoSize", !shape.contains("width") && !shape.contains("height"));
    metadata.widthHeight =
        QSize(gridToPixels(
                  std::max(1, shape.value("width", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE))),
              gridToPixels(
                  std::max(1, shape.value("height", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE))));

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
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool subcircuitHasGraphicalMetadata(std::string_view sceneJson)
{
  return parseGraphicalSubcircuitMetadata(
             nlohmann::json::parse(sceneJson, nullptr, false))
      .has_value();
}

GraphicalSubcircuitMetadata synchronizeGraphicalSubcircuitMetadata(
    const nlohmann::json& document, const GraphicalSubcircuitMetadata& fallbackMetadata,
    const CircuitResolver* resolver)
{
  try {
    auto       metadata = fallbackMetadata;
    const auto circuit  = deserializeCircuitDocument(
        document.dump(), ComponentRegistry::instance(), resolver);
    const auto inputs  = portDefinitions(circuit.getInputPorts(), document, true);
    const auto outputs = portDefinitions(circuit.getOutputPorts(), document, false);
    if (metadata.autoSize) {
      metadata.widthHeight =
          defaultGraphicalSubcircuitSize(inputs.size(), outputs.size());
    }
    metadata.inputs  = synchronizePorts(inputs, fallbackMetadata, metadata, true);
    metadata.outputs = synchronizePorts(outputs, fallbackMetadata, metadata, false);
    return metadata;
  } catch (const std::exception&) {
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
         .position = hasFallback && metadata.positioning == PortPositioning::Manual
                         ? fallbackPorts[i].position
                         : automaticPortPosition(metadata.widthHeight, inputPort, i,
                                                 buses.size()),
         .busSize  = static_cast<unsigned int>(std::max<std::size_t>(1, buses[i].size())),
         .id       = hasFallback ? fallbackPorts[i].id : 0});
  }

  return synchronized;
}

nlohmann::ordered_json
graphicalSubcircuitMetadataToJson(const GraphicalSubcircuitMetadata& metadata)
{
  nlohmann::json inputs = nlohmann::json::array();
  for (const auto& port : metadata.inputs)
    inputs.push_back(portToJson(port));

  nlohmann::json outputs = nlohmann::json::array();
  for (const auto& port : metadata.outputs)
    outputs.push_back(portToJson(port));

  return nlohmann::json{
      {"shape",
       {{"type", "rectangle"},
        {"autoSize", metadata.autoSize},
        {"positioning",
         metadata.positioning == PortPositioning::Automatic ? "automatic" : "manual"},
        {"width", pixelsToGrid(metadata.widthHeight.width())},
        {"height", pixelsToGrid(metadata.widthHeight.height())}}},
      {"inputs", std::move(inputs)},
      {"outputs", std::move(outputs)}};
}

}  // namespace SILICON::ui
