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

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <QPoint>
#include <QSize>

#include <core/wire.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

/** @brief Default subcircuit rectangle size in persisted grid units. */
inline constexpr int GraphicalSubcircuitDefaultSize = 8;

/**
 * @brief Visual metadata for one subcircuit boundary port.
 *
 * Positions are runtime scene pixels relative to the top-left of the subcircuit
 * rectangle. The JSON representation stores the same values in grid units.
 */
struct GraphicalSubcircuitPortMetadata {
  /** @brief Port label and core boundary name. */
  std::string  name;
  /** @brief Port connection point in pixels, relative to the rectangle top-left. */
  QPoint       position;
  /** @brief Number of wires represented by the port. */
  unsigned int busSize = 1;
};

/**
 * @brief Visual metadata used to render a subcircuit as a reusable component.
 *
 * The in-memory representation uses pixels because it is consumed directly by
 * QGraphicsItem geometry. Serialization converts width, height, and port
 * positions to grid units.
 */
struct GraphicalSubcircuitMetadata {
  /** @brief Rectangle dimensions in pixels. */
  QSize widthHeight{GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE,
                    GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE};
  /** @brief Input boundary ports in display order. */
  std::vector<GraphicalSubcircuitPortMetadata> inputs;
  /** @brief Output boundary ports in display order. */
  std::vector<GraphicalSubcircuitPortMetadata> outputs;
};

/**
 * @brief Parses graphical subcircuit metadata from a serialized scene document.
 * @param sceneJson Serialized subcircuit scene JSON.
 * @return Parsed metadata, or std::nullopt when metadata is missing or invalid.
 */
[[nodiscard]] std::optional<GraphicalSubcircuitMetadata>
parseGraphicalSubcircuitMetadata(std::string_view sceneJson);

/**
 * @brief Checks whether a scene document contains valid graphical metadata.
 * @param sceneJson Serialized subcircuit scene JSON.
 * @return True when parseGraphicalSubcircuitMetadata() can parse the document.
 */
[[nodiscard]] bool subcircuitHasGraphicalMetadata(std::string_view sceneJson);

/**
 * @brief Builds the core circuit JSON for a graphical subcircuit document.
 *
 * Graphical boundary I/O components are editor-only; this strips their associated
 * core vertices by using the visual component categories and vertex IDs.
 */
[[nodiscard]] std::optional<std::string>
graphicalSubcircuitCoreCircuitJson(std::string_view sceneJson);

/**
 * @brief Reconciles saved graphical metadata with the current subcircuit document.
 *
 * Boundary ports are re-derived from the circuit contents while matching existing
 * positions by name or index when possible.
 *
 * @param sceneJson Serialized subcircuit scene JSON.
 * @param fallbackMetadata Existing metadata to preserve user-edited positions.
 * @return Metadata whose ports match the current boundary components.
 */
[[nodiscard]] GraphicalSubcircuitMetadata synchronizeGraphicalSubcircuitMetadata(
    std::string_view sceneJson, const GraphicalSubcircuitMetadata& fallbackMetadata);

/**
 * @brief Reconciles metadata when a loaded subcircuit component has a different bus
 * count than its saved graphical metadata.
 */
[[nodiscard]] std::vector<GraphicalSubcircuitPortMetadata> synchronizePortsWithBuses(
    const std::vector<GraphicalSubcircuitPortMetadata>& fallbackPorts,
    const std::vector<Bus>& buses, const GraphicalSubcircuitMetadata& metadata,
    bool inputPort);

/**
 * @brief Serializes graphical metadata to the JSON shape/port object.
 * @param metadata Runtime pixel metadata.
 * @return JSON metadata with dimensions and port positions in grid units.
 */
[[nodiscard]] nlohmann::ordered_json
graphicalSubcircuitMetadataToJson(const GraphicalSubcircuitMetadata& metadata);

}  // namespace ui
}  // namespace SILICON
