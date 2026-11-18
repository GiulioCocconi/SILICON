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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <QPoint>
#include <QSize>

#include <core/wire.hpp>
#include <ui/circuit/diagram/grid.hpp>

namespace SILICON::core {
class CircuitResolver;
}

namespace SILICON::ui {
using namespace SILICON::core;

enum class PortPositioning { Automatic, Manual };

/** @brief Default subcircuit rectangle size in persisted grid units. */
inline constexpr int GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE = 2;

/** @brief Default square dimensions for the boundary port counts, in pixels. */
[[nodiscard]] QSize defaultGraphicalSubcircuitSize(std::size_t inputCount,
                                                   std::size_t outputCount);

/**
 * @brief Visual metadata for one subcircuit boundary port.
 *
 * Positions are runtime scene pixels relative to the top-left of the subcircuit
 * rectangle. The JSON representation stores the same values in grid units.
 */
struct GraphicalSubcircuitPortMetadata {
  /** @brief Display label derived from the core boundary name. */
  std::string  name;
  /** @brief Port connection point in pixels, relative to the rectangle top-left. */
  QPoint       position;
  /** @brief Number of wires represented by the port. */
  unsigned int busSize = 1;
  /** @brief Stable core boundary identity, independent of the display label. */
  uint64_t id = 0;
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
  QSize widthHeight{GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE * GRID_SIZE,
                    GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE* GRID_SIZE};
  /** @brief Derive dimensions from port counts until the user edits the size. */
  bool autoSize = true;
  /** @brief One positioning mode for all input and output ports of the shape. */
  PortPositioning positioning = PortPositioning::Automatic;
  /** @brief Input boundary ports in core interface order. */
  std::vector<GraphicalSubcircuitPortMetadata> inputs;
  /** @brief Output boundary ports in core interface order. */
  std::vector<GraphicalSubcircuitPortMetadata> outputs;
};

/**
 * @brief Parses graphical subcircuit metadata from a scene document.
 * @param document Parsed subcircuit scene JSON.
 * @return Parsed metadata, or std::nullopt when metadata is missing or invalid.
 */
[[nodiscard]] std::optional<GraphicalSubcircuitMetadata>
parseGraphicalSubcircuitMetadata(const nlohmann::json& document);

/**
 * @brief Checks whether a scene document contains valid graphical metadata.
 * @param sceneJson Serialized subcircuit scene JSON.
 * @return True when parseGraphicalSubcircuitMetadata() can parse the document.
 */
[[nodiscard]] bool subcircuitHasGraphicalMetadata(std::string_view sceneJson);

/**
 * @brief Reconciles saved graphical metadata with the current circuit document.
 *
 * Boundary ports are re-derived from the circuit contents while matching existing
 * positions by identity, with name/index compatibility for old metadata.
 * Automatic dimensions use twice the
 * larger boundary port count in grid units, with a minimum of two grid units.
 * Generated port groups are centered on each side with two grid units between centers.
 * Automatic layouts follow size/count changes; manual layouts retain saved positions.
 *
 * @param document Parsed subcircuit scene JSON.
 * @param fallbackMetadata Existing metadata to preserve user-edited positions.
 * @param resolver Optional resolver for dependent subcircuits.
 * @return Metadata whose ports match the current boundary components.
 */
[[nodiscard]] GraphicalSubcircuitMetadata synchronizeGraphicalSubcircuitMetadata(
    const nlohmann::json& document, const GraphicalSubcircuitMetadata& fallbackMetadata,
    const CircuitResolver* resolver = nullptr);

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

}  // namespace SILICON::ui
