/*
  Copyright (C) 2025 Giulio Cocconi

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


namespace SILICON {
namespace ui {
using namespace SILICON::core;

GraphicalGate::GraphicalGate(const std::shared_ptr<Gate> gate, QGraphicsItem* shape,
                             QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(gate, shape, parent, scanShape)
{
  // TODO: ADD SUPPORT FOR OVER 2 INPUTS GATES
  if (associatedComponent->getInputs().size() != 2)
    throw std::logic_error("GraphicalGate: expected exactly 2 inputs");

  isEditable = false;

  std::vector<PortPair> inputVec;
  inputVec.reserve(2);

  inputVec.emplace_back("a", QPoint(-20, 10));

  inputVec.emplace_back("b", QPoint(-20, 30));

  constexpr auto outputPoint = QPoint(80, 20);

  setPorts(inputVec, {PortPair{"o", outputPoint}});
}

GraphicalNot::GraphicalNot(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<NotGate>(nullptr, nullptr),
                            new QGraphicsSvgItem(":/gates/NOT_ANSI.svg"), parent)
{
  isEditable = false;

  setPorts({PortPair{"i", QPoint(-20, 20)}}, {PortPair{"o", QPoint(80, 20)}});
}

}  // namespace ui
}  // namespace SILICON
