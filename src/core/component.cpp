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

#include "component.hpp"
#include <ranges>
#include <stdexcept>

Component::Component(std::vector<Bus> inputs, std::vector<Bus> outputs, std::string name)
{
  this->inputs  = std::move(inputs);
  this->outputs = std::move(outputs);
  this->name    = std::move(name);
}

void Component::setInput(const unsigned int index, const Bus& bus)
{
  // If the action is already defined then we should remove it from the inputs:
  if (this->act)
    for (const auto& w : this->inputs[index])
      if (w)
        w->deleteUpdateAction(this->act);

  // Then we set the new inputs and add the update action to them:
  this->inputs[index] = bus;

  if (this->act)
    for (const auto& w : this->inputs[index])
      if (w)
        w->addUpdateAction(this->act);
}

void Component::setInputs(const std::vector<Bus>& newInputs)
{
  // If the former inputs are the same as the new inputs then do nothing:
  if (this->inputs == newInputs)
    return;

  if (this->inputs.empty()) {
    this->inputs = newInputs;
  }

  for (const auto& [index, bus] : newInputs | silicon::views::enumerate) {
    setInput(index, bus);
  }
}

void Component::setOutput(const unsigned int index, const Bus& bus)
{
  if (this->outputs[index] == bus)
    return;

  this->outputs[index] = bus;
}

void Component::setOutputs(const std::vector<Bus>& newOutputs)
{
  // If the former outputs are the same as the new outputs then do nothing:
  if (this->outputs == newOutputs)
    return;

  // We set the new outputs
  this->outputs = newOutputs;
}
void Component::clearWires()
{
  for (const auto [index, bus] : this->outputs | silicon::views::enumerate) {
    const auto                  busSize = bus.size();
    const std::vector<Wire_ptr> wires(busSize);

    const auto newBus = Bus(wires);

    setOutput(index, newBus);
  }

  for (const auto [index, bus] : this->inputs | silicon::views::enumerate) {
    const auto                  busSize = bus.size();
    const std::vector<Wire_ptr> wires(busSize);

    const auto newBus = Bus(wires);

    setInput(index, newBus);
  }
}

void Component::setAction(const action& a)
{
  this->act = std::make_shared<action>(a);
  if (!this->act)
    throw std::logic_error("Failed to create action");

  // The action is to be set for all the inputs of the component connected to wires:
  for (auto bus : this->inputs)
    for (const auto& w : bus)
      if (w)
        w->addUpdateAction(this->act);
}

Component::~Component()
{
  // Remove the associated update action from all the inputs:
  for (auto bus : this->inputs)
    for (const auto& w : bus)
      if (w)
        w->deleteUpdateAction(this->act);
}
