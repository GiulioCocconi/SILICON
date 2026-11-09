/*
  Copyright (c) 2025. Giulio Cocconi

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

#include "graphicalGates.hpp"

#include <stdexcept>


namespace SILICON::ui {
using namespace SILICON::core;

GraphicalGate::GraphicalGate(const std::shared_ptr<Gate> gate, QGraphicsItem* shape,
                             QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(gate, shape, parent, scanShape)
{
  isEditable = false;
  updateGatePorts();
}

void GraphicalGate::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  updateGatePorts();
}

void GraphicalGate::updateGatePorts()
{
  const auto count = associatedComponent->inputBuses().size();
  if (count == 0 || associatedComponent->outputBuses().size() != 1)
    throw std::logic_error("GraphicalGate requires inputs and one output");

  // Preserve the familiar two-input symbol, stretching its body vertically
  // for imported gates with additional inputs. Ports retain 20-pixel spacing.
  prepareGeometryChange();
  const int height = 20 * static_cast<int>(count);
  getItemShape()->setTransform(QTransform::fromScale(1, height / 40.0));

  std::vector<PortPair> inputVec;
  inputVec.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto name =
        count == 2 ? (i == 0 ? QString("a") : QString("b")) : QString("i%1").arg(i);
    inputVec.emplace_back(name, QPoint(-20, 10 + 20 * static_cast<int>(i)));
  }
  setPorts(inputVec, {PortPair{"o", QPoint(80, height / 2)}});
}

GraphicalNot::GraphicalNot(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<NotGate>(nullptr, nullptr),
                            new ThemedSvgItem(":/gates/NOT_ANSI.svg"), parent)
{
  isEditable = false;

  setPorts({PortPair{"i", QPoint(-20, 20)}}, {PortPair{"o", QPoint(80, 20)}});
}

}  // namespace SILICON::ui
