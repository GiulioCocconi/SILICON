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

#include <QGraphicsItem>

#include <core/serialization/component_registry.hpp>

#include <memory>
#include <ui/common/graphicalComponent.hpp>

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>


namespace SILICON {
namespace ui {
class GraphicalComponent;

/**
 * @class GUIComponentFactory
 * @brief Factory for creating graphical circuit components.
 *
 * The GUIComponentFactory implements a registry pattern for creating
 * GraphicalComponent instances by type name. It supports registration
 * of custom component types and provides a singleton instance
 * for global access.
 *
 * Components are registered via registerType() and created via create().
 * This allows for extensible component libraries without modifying
 * the core factory code.
 *
 * @see GraphicalComponent
 * @see GraphicalLogicComponent
 */
class GUIComponentFactory {
public:
  /**
   * @brief Factory function type for creating components.
   *
   * @param parent Optional parent graphics item
   * @return Unique pointer to created component
   */
  using Factory = std::function<std::unique_ptr<GraphicalComponent>(QGraphicsItem*)>;

  struct EntryMetadata {
    std::string coreType;
  };

  GUIComponentFactory();

  /**
   * @brief Creates an empty factory without registered types.
   * @return New empty factory instance
   */
  static GUIComponentFactory empty();

  /**
   * @brief Gets the global singleton factory instance.
   * @return Reference to the singleton
   */
  static GUIComponentFactory& instance();

  /**
   * @brief Registers a component type.
   *
   * @param type The type identifier string
   * @param factory The factory function
   * @throws std::logic_error if type is already registered
   */
  void registerType(std::string type, Factory factory);
  void registerType(std::string type, Factory factory, EntryMetadata metadata);

  /**
   * @brief Creates a component by type name.
   *
   * @param type The type identifier
   * @param parent Optional parent graphics item
   * @return Unique pointer to created component
   * @throws std::runtime_error if type is unknown
   */
  std::unique_ptr<GraphicalComponent> create(std::string_view type,
                                             QGraphicsItem*   parent = nullptr) const;

  /**
   * @brief Gets all available component types.
   * @return Vector of type name strings
   */
  std::vector<std::string> availableTypes() const;

  const EntryMetadata& metadata(std::string_view type) const;

  /**
   * @brief Checks if a type is registered.
   * @param type The type to check
   * @return True if the type exists
   */
  bool hasType(std::string_view type) const;

private:
  /** @brief Map of type names to factory functions */
  struct Entry {
    Factory       factory;
    EntryMetadata metadata;
  };

  std::map<std::string, Entry, std::less<>> factories_;
};

}  // namespace ui
}  // namespace SILICON
