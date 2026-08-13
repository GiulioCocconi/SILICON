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

#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <ui/common/ogdfCircuit.hpp>
#include <ui/common/portGeometry.hpp>
#include <ui/common/wireManager.hpp>

namespace SILICON::core {
class Circuit;
}

namespace SILICON::ui {
class GraphicalLogicComponent;
class GraphicalIO;

using IoOrientationMap = std::unordered_map<GraphicalIO*, PortSide>;

/**
 * @brief Configuration for full circuit autoplacement and wire routing.
 *
 * These options combine the OGDF component-placement settings with libavoid obstacle
 * clearance used while routing wires between the newly placed components.
 */
struct CircuitAutoplacerOptions {
  /** @brief Options passed directly to GraphLayout for component placement. */
  GraphLayoutOptions graphLayout;

  /** @brief Extra scene-unit padding added around component obstacles for routing
   * clearance. */
  int obstaclePadding = 20;

  /** @brief Number of deterministic placement candidates to route and compare. */
  int candidateCount = 1;

  /** @brief Optional cooperative cancellation check, evaluated between candidates. */
  std::function<bool()> isCancelled;

  /** @brief Optional notification after each completed candidate. */
  std::function<void(int completed, int total)> progress;
};

/**
 * @brief Complete pure autoplacement result.
 *
 * The result contains component positions from OGDF and wire paths from libavoid. It is
 * intentionally detached from QGraphicsScene mutation so callers can decide how to apply
 * the changes, register undo commands, and refresh topology.
 */
struct CircuitAutoplacement {
  /** @brief Computed scene positions for participating components. */
  GraphLayout::PlacementMap components;

  /** @brief Cardinal port sides selected for boundary I/O components. */
  IoOrientationMap ioPortOrientations;

  /** @brief Routed scene-space wire paths. */
  std::vector<RoutedWire> wires;
};

/**
 * @brief Combines OGDF component layout with libavoid orthogonal wire routing.
 *
 * CircuitAutoplacer is a pure calculation helper. It ranks layered directions from the
 * connected port geometry, compares cardinal and force-directed candidates, positions
 * boundary I/O around connected logic, then routes matched logical connections between
 * their proposed port locations. Each logical net is represented by
 * terminal-to-junction branches on lanes distinct from every other net.
 */
class CircuitAutoplacer {
public:
  /**
   * @brief Determines boundary I/O sides directly from connected component ports.
   *
   * Callers may apply these orientations before layout so component bounds and port
   * geometry are identical on the first and subsequent autoplacement runs.
   */
  [[nodiscard]] static IoOrientationMap
  boundaryIoOrientations(const core::Circuit&                      circuit,
                         std::span<GraphicalLogicComponent* const> components);

  /**
   * @brief Computes component placements and routed wire paths for a circuit.
   *
   * The method ignores circuit vertices or edges whose endpoints are not represented in
   * @p components. It also skips edges when a matching output/input port cannot be found
   * for the edge bus. Returned wire points are scene coordinates after the proposed
   * component moves. Boundary I/O port sides are selected from the proposed topology,
   * components are translated to libavoid shapes, and logical edges are translated to
   * orthogonal connectors. Returned route points are snapped to the DiagramScene grid
   * and compacted to terminals, bends, and junctions.
   *
   * @param circuit Logical circuit topology to place and route.
   * @param components Graphical logic components participating in the result.
   * @param options Component layout and wire obstacle-clearance settings.
   * @return Component placement map plus routed connections.
   */
  [[nodiscard]] static CircuitAutoplacement
  compute(const core::Circuit&                      circuit,
          std::span<GraphicalLogicComponent* const> components,
          const CircuitAutoplacerOptions&           options = {});
};

}  // namespace SILICON::ui
