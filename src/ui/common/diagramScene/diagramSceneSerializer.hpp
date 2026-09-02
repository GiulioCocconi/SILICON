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

#include <memory>
#include <string>
#include <vector>

#include <QGraphicsItem>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>

namespace SILICON::core {
class Circuit;
}


namespace SILICON {
namespace ui {
using namespace SILICON::core;

class DiagramScene;
class GraphicalItem;
class GUIComponentFactory;

/**
 * @class DiagramSceneSerializer
 * @brief Handles DiagramScene persistence, clipboard payloads, and reconstruction.
 *
 * This helper encapsulates the JSON formats used by the editor for full-scene saves,
 * clipboard selections, undo payloads, and scene reconstruction. It keeps the payload
 * semantics identical to the original DiagramScene implementation while moving the
 * serialization concern out of the scene coordinator.
 */
class DiagramSceneSerializer {
public:
  /**
   * @brief Constructs a serializer bound to a scene.
   * @param scene Host scene whose items and topology are serialized
   */
  explicit DiagramSceneSerializer(DiagramScene& scene);

  /**
   * @brief Serializes the full editor scene to the persisted JSON format.
   * @return JSON string representation of the scene
   */
  [[nodiscard]] std::string serialize() const;

  /**
   * @brief Serializes the current selection to the clipboard payload format.
   * @return Ordered JSON payload for clipboard and undo flows
   */
  [[nodiscard]] nlohmann::ordered_json serializeSelection() const;

  /**
   * @brief Serializes arbitrary scene items using the selection payload format.
   * @param sceneItems Items to serialize
   * @return Ordered JSON payload containing the provided items
   */
  [[nodiscard]] nlohmann::ordered_json
  serializeItems(const std::vector<QGraphicsItem*>& sceneItems) const;

  /**
   * @brief Deserializes a full scene from persisted JSON.
   * @param jsonStr JSON string to load
   * @param guiFactory Factory for graphical component construction
   * @param coreRegistry Registry for core component construction
   */
  void deserialize(const std::string& jsonStr, GUIComponentFactory& guiFactory,
                   const ComponentRegistry& coreRegistry);

  /** Builds an autoplaced graphical scene around an already-created core circuit. */
  void loadCircuit(std::shared_ptr<Circuit> circuit, GUIComponentFactory& guiFactory,
                   bool resolveSubcircuitMetadata = true);

  /**
   * @brief Inserts a serialized selection payload into the current scene.
   * @param payload Decoded clipboard or undo payload
   * @param guiFactory Factory for graphical component construction
   * @param coreRegistry Registry for core component construction
   * @param targetOrigin Scene position where the payload origin should land
   * @param isPaste True when the payload comes from paste and needs fresh runtime ids
   * @return True when at least one item was inserted
   */
  bool insertSelection(const nlohmann::json& payload, GUIComponentFactory& guiFactory,
                       const ComponentRegistry& coreRegistry, QPointF targetOrigin,
                       bool isPaste);

  /**
   * @brief Removes items matching a serialized selection payload from the scene.
   * @param payload Serialized selection payload
   * @return True when any matching item was removed
   */
  bool removeSelection(const nlohmann::json& payload);

private:
  /** @brief Host scene whose items and lookup tables back serialization operations */
  DiagramScene& scene;
};

}  // namespace ui
}  // namespace SILICON
