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

#include <core/memory.hpp>
#include <cstdint>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <vector>

namespace SILICON::project {
class DocumentStore;
}

namespace SILICON::ui {

class GraphicalMemory : public GraphicalLogicComponent {
  Q_OBJECT
public:
  ~GraphicalMemory() override;

  void setComponent(const SILICON::core::Component_ptr& component) override;
  void applyProperty(std::string_view                    key,
                     const SILICON::core::PropertyValue& value) override;
  void setDocumentStore(const SILICON::project::DocumentStore* documents);

protected:
  GraphicalMemory(const std::shared_ptr<SILICON::core::Memory>& memory,
                  QGraphicsItem* shape, QGraphicsItem* parent);
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
  const SILICON::project::DocumentStore* documents          = nullptr;
  std::uint64_t                          documentListenerId = 0;

  void subscribeToDocuments();
  void unsubscribeFromDocuments();
  void refreshBinaryContents();
  [[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>>
                            resolveBinaryContents(std::string_view slug) const;
  [[nodiscard]] std::string currentSlug() const;
};

class GraphicalROM final : public GraphicalMemory {
  Q_OBJECT
public:
  explicit GraphicalROM(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::ROM_COMPONENT; }
};

class GraphicalRAM final : public GraphicalMemory {
  Q_OBJECT
public:
  explicit GraphicalRAM(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::RAM_COMPONENT; }
  void setComponent(const SILICON::core::Component_ptr& component) override;
  void applyProperty(std::string_view key,
                     const SILICON::core::PropertyValue& value) override;

private:
  void rebuildPorts();
};

}  // namespace SILICON::ui
