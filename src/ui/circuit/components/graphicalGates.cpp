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


namespace SILICON::ui {
using namespace SILICON::core;

GraphicalGate::GraphicalGate(const std::shared_ptr<Gate> gate, QGraphicsItem* shape,
                             QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(gate, shape, parent, scanShape)
{
  isEditable = false;
  updatePortSizes();
}

void GraphicalGate::updatePortSizes()
{
  if (!associatedComponent)
    return;

  const auto count = associatedComponent->inputBuses().size();
  if (inputPorts.size() != count || outputPorts.empty()) {
    std::vector<PortPair> inputs;
    inputs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const int y = 20 + (2 * static_cast<int>(i) - static_cast<int>(count) + 1) * 10;
      const QString name = count == 1 ? QStringLiteral("i")
                           : count == 2 ? (i == 0 ? QStringLiteral("a")
                                                  : QStringLiteral("b"))
                                        : QString::number(i);
      inputs.emplace_back(name, QPoint(-20, y));
    }
    GraphicalComponent::setPorts(inputs, {PortPair{"o", QPoint(80, 20)}});
  }
  GraphicalLogicComponent::updatePortSizes();
}

GraphicalNot::GraphicalNot(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<NotGate>(nullptr, nullptr),
                            new ThemedSvgItem(":/gates/NOT_ANSI.svg"), parent)
{
  isEditable = false;

  setPorts({PortPair{"i", QPoint(-20, 20)}}, {PortPair{"o", QPoint(80, 20)}});
}

}  // namespace SILICON::ui
