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

#include "graphicalLogicComponent.hpp"

#include <stdexcept>

GraphicalLogicComponent::GraphicalLogicComponent(ItemCategory         category,
                                                 const Component_ptr& component,
                                                 QGraphicsItem*       shape,
                                                 QGraphicsItem* parent, bool scanShape)
  : GraphicalComponent(category | ItemCategory::LogicComponent, shape, parent, scanShape)
{
  this->associatedComponent = component;
}

GraphicalLogicComponent::GraphicalLogicComponent(const Component_ptr& component,
                                                 QGraphicsItem*       shape,
                                                 QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(ItemCategory::LogicComponent, component, shape, parent,
                            scanShape)
{
}
void GraphicalLogicComponent::setPorts(
    const std::vector<std::pair<std::string, QPoint>>& busToPortInputs,
    const std::vector<std::pair<std::string, QPoint>>& busToPortOutputs)
{
  if (associatedComponent) {
    const std::vector<Bus> componentInputs  = associatedComponent->getInputs();
    const std::vector<Bus> componentOutputs = associatedComponent->getOutputs();

    if (componentInputs.size() != busToPortInputs.size())
      throw std::logic_error("Input port count mismatch with component");
    if (componentOutputs.size() != busToPortOutputs.size())
      throw std::logic_error("Output port count mismatch with component");
  }

  GraphicalComponent::setPorts(busToPortInputs, busToPortOutputs);
}
