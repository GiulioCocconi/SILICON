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

#include <extraComponents/multiplexer.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::extra;

class GraphicalMultiplexer : public GraphicalLogicComponent {
  Q_OBJECT
private:
  void setupCallbacks();
  void updateLayout(int selectionSize, int busSize);
  int  applySelectionSize(int selectionSize);
  int  applyBusSize(int busSize);

  [[nodiscard]] Multiplexer* getComponentAsMultiplexer() const;
  [[nodiscard]] bool splitDataInputs() const;

protected:
  [[nodiscard]] bool acceptsInputPortCount(size_t portCount,
                                           const std::vector<Bus>& componentInputs) const override;

  [[nodiscard]] unsigned int inputPortSize(size_t portIndex,
                                           const std::vector<Bus>& componentInputs) const override;

public:
  explicit GraphicalMultiplexer(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::MULTIPLEXER; }

  void assignInputPortBus(unsigned int portIndex, const Bus& bus) const override;
  void setComponent(const Component_ptr& component) override;
};

class GraphicalDemultiplexer : public GraphicalLogicComponent {
  Q_OBJECT
private:
  void setupCallbacks();
  void updateLayout(int selectionSize, int busSize);
  int  applySelectionSize(int selectionSize);
  int  applyBusSize(int busSize);

  [[nodiscard]] Demultiplexer* getComponentAsDemultiplexer() const;

public:
  explicit GraphicalDemultiplexer(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::DEMULTIPLEXER; }

  void setComponent(const Component_ptr& component) override;
};

class GraphicalDecoder : public GraphicalLogicComponent {
  Q_OBJECT
private:
  void setupCallbacks();
  int  applySelectionSize(int selectionSize);

  [[nodiscard]] Decoder* getComponentAsDecoder() const;

public:
  explicit GraphicalDecoder(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::DECODER; }

  void setComponent(const Component_ptr& component) override;
};

}  // namespace ui
}  // namespace SILICON
