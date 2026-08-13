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

#include <span>
#include <unordered_map>

#include <QPointF>

#include <ui/common/diagramScene/diagramScene.hpp>

namespace SILICON::core {
class Circuit;
}

namespace SILICON::ui {
class GraphicalLogicComponent;

enum class GraphLayoutAlgorithm {
  Layered,
  ForceDirected,
};

/** @brief Signal-flow direction used for a layered layout. */
enum class GraphLayoutDirection {
  TopToBottom,
  LeftToRight,
};

/**
 * @brief Configuration for OGDF-based component placement.
 *
 * The layout adapter uses these values while converting the logical circuit graph into
 * an OGDF graph and while mapping OGDF center coordinates back into Qt scene positions.
 */
struct GraphLayoutOptions {
  /** @brief Placement family used to generate the candidate. */
  GraphLayoutAlgorithm algorithm = GraphLayoutAlgorithm::Layered;
  /** @brief Scene direction assigned to source-to-target ranks. */
  GraphLayoutDirection direction = GraphLayoutDirection::TopToBottom;
  /** @brief Deterministic seed used by force-directed candidates. */
  int randomSeed = 0;
  /** @brief Minimum horizontal scene distance between nodes on the same OGDF layer. */
  int nodeDistance = 6 * DiagramScene::GRID_SIZE;
  /** @brief Minimum vertical scene distance between neighboring OGDF layers. */
  int layerDistance = 10 * DiagramScene::GRID_SIZE;
  /** @brief Minimum distance between disconnected graph components. */
  int connectedComponentDistance = 12 * DiagramScene::GRID_SIZE;
  /** @brief Width used when a graphical item has no valid scene bounds. */
  int fallbackNodeWidth = 8 * DiagramScene::GRID_SIZE;
  /** @brief Height used when a graphical item has no valid scene bounds. */
  int fallbackNodeHeight = 5 * DiagramScene::GRID_SIZE;
  /** @brief Top-left scene position of the normalized arranged layout. */
  QPointF origin = QPointF(0.0, 0.0);
  /** @brief True to use a single deterministic Sugiyama crossing-minimization run. */
  bool deterministic = true;
};

/**
 * @brief Pure OGDF adapter for calculating circuit component positions.
 *
 * GraphLayout does not mutate the scene or any graphical item. It only matches
 * graphical components to vertices in a Circuit, computes a Sugiyama layered layout,
 * and returns the scene positions that callers may apply through their own command or
 * undo flow.
 */
class GraphLayout {
public:
  /**
   * @brief Map from graphical component to the computed scene position for that item.
   */
  using PlacementMap = std::unordered_map<GraphicalLogicComponent*, QPointF>;

  /**
   * @brief Computes component placements for the subset represented by @p components.
   *
   * Components whose associated core component is not present in @p circuit are ignored.
   * Circuit vertices without a matching graphical component are also ignored. The
   * returned points are suitable for QGraphicsItem::setPos().
   *
   * @param circuit Logical circuit topology used as the layout graph.
   * @param components Graphical logic components that may participate in placement.
   * @param options Layout spacing, fallback sizing, origin, and determinism settings.
   * @return Computed scene positions indexed by graphical component pointer.
   */
  [[nodiscard]] static PlacementMap
  compute(const core::Circuit&                      circuit,
          std::span<GraphicalLogicComponent* const> components,
          const GraphLayoutOptions&                 options = {});
};

}  // namespace SILICON::ui
