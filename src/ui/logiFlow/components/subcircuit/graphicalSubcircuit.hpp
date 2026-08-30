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

#include <string>

#include <QGraphicsItem>

#include <core/subcircuit.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

/**
 * @brief Graphical component wrapper for a registered subcircuit.
 *
 * The component listens for subcircuit registry changes and redraws its
 * rectangle and ports from the selected subcircuit's graphical metadata.
 */
class GraphicalSubcircuitComponent : public GraphicalLogicComponent {
  Q_OBJECT
public:
  /** @brief Creates a graphical subcircuit with an empty slug. */
  explicit GraphicalSubcircuitComponent(QGraphicsItem* parent = nullptr);

  /**
   * @brief Creates a graphical subcircuit and immediately selects a slug.
   * @param slug Subcircuit registry slug.
   * @param parent Optional parent graphics item.
   */
  explicit GraphicalSubcircuitComponent(std::string slug, QGraphicsItem* parent = nullptr);

  /** @brief Unregisters the subcircuit registry listener. */
  ~GraphicalSubcircuitComponent() override;

  /** @brief Replaces the associated core subcircuit component. */
  void setComponent(const Component_ptr& component) override;

  /** @brief Applies a component property and refreshes metadata when the slug changes. */
  void applyProperty(std::string_view key, const PropertyValue& value) override;

  /** @brief Reloads shape and ports from the active registry document. */
  void refreshFromMetadata();

  /** @brief Uses default metadata derived only from the attached imported buses. */
  void useAttachedInterfaceMetadata();

  int type() const override { return SiliconTypes::SUBCIRCUIT; }

private:
  std::uint64_t registryListenerId = 0;

  /** @brief Returns the slug stored by the associated core component. */
  [[nodiscard]] std::string currentSlug() const;

  /** @brief Applies parsed metadata to the rendered shape and ports. */
  void                      applyMetadata(const GraphicalSubcircuitMetadata& metadata);

  /** @brief Rebuilds visible ports when subcircuit I/O shape changes. */
  void                      updatePortSizes() override;
};

}  // namespace ui
}  // namespace SILICON
