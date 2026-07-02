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
  refreshComponentIOListener();
}

GraphicalLogicComponent::GraphicalLogicComponent(const Component_ptr& component,
                                                 QGraphicsItem*       shape,
                                                 QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(ItemCategory::LogicComponent, component, shape, parent,
                            scanShape)
{
}

GraphicalLogicComponent::~GraphicalLogicComponent()
{
  if (associatedComponent && componentIOListenerId != 0)
    associatedComponent->removeIOListener(componentIOListenerId);
}

void GraphicalLogicComponent::refreshComponentIOListener()
{
  if (!associatedComponent)
    return;

  if (componentIOListenerId != 0)
    associatedComponent->removeIOListener(componentIOListenerId);

  componentIOListenerId =
      associatedComponent->addIOListener([this](Component*) { updatePortSizes(); });
}

void GraphicalLogicComponent::updatePortSizes()
{
  if (!associatedComponent)
    return;

  const std::vector<Bus> componentInputs  = associatedComponent->getInputs();
  const std::vector<Bus> componentOutputs = associatedComponent->getOutputs();

  for (size_t i = 0; i < inputPorts.size(); ++i)
    inputPorts[i]->setSize(inputPortSize(i, componentInputs));

  for (size_t i = 0; i < outputPorts.size() && i < componentOutputs.size(); ++i)
    outputPorts[i]->setSize(componentOutputs[i].size());

  update();
}

bool GraphicalLogicComponent::acceptsInputPortCount(
    const size_t portCount, const std::vector<Bus>& componentInputs) const
{
  return componentInputs.size() == portCount;
}

unsigned int
GraphicalLogicComponent::inputPortSize(const size_t             portIndex,
                                       const std::vector<Bus>& componentInputs) const
{
  if (portIndex >= componentInputs.size())
    return 1;

  return static_cast<unsigned int>(componentInputs[portIndex].size());
}

void GraphicalLogicComponent::applyProperty(std::string_view     key,
                                            const PropertyValue& value)
{
  if (!associatedComponent)
    return;

  prepareGeometryChange();
  associatedComponent->setProperty(key, value);
  update();
}

void GraphicalLogicComponent::setComponent(const Component_ptr& component)
{
  if (associatedComponent && componentIOListenerId != 0)
    associatedComponent->removeIOListener(componentIOListenerId);

  componentIOListenerId = 0;
  associatedComponent   = component;
  refreshComponentIOListener();
  updatePortSizes();
}

void GraphicalLogicComponent::assignInputPortBus(const unsigned int portIndex,
                                                 const Bus&         bus) const
{
  if (!associatedComponent)
    return;

  associatedComponent->setInput(portIndex, bus, true);
}

void GraphicalLogicComponent::setPorts(const std::vector<PortPair>& busToPortInputs,
                                       const std::vector<PortPair>& busToPortOutputs)
{
  if (associatedComponent) {
    const std::vector<Bus> componentInputs  = associatedComponent->getInputs();
    const std::vector<Bus> componentOutputs = associatedComponent->getOutputs();

    if (!acceptsInputPortCount(busToPortInputs.size(), componentInputs))
      throw std::logic_error("Input port count mismatch with component");
    if (componentOutputs.size() != busToPortOutputs.size())
      throw std::logic_error("Output port count mismatch with component");
  }

  GraphicalComponent::setPorts(busToPortInputs, busToPortOutputs);
  updatePortSizes();
}
