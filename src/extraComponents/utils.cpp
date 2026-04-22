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

#include "utils.hpp"

#include <core/simulator.hpp>

WireSplitter::WireSplitter(Bus input, const std::vector<Bus>& outputs)
  : Component({std::move(input)}, outputs)
{
  defineProperty("size", 2);
  defineProperty("delay", 0);

  setPropertyCallback("size", [this](const PropertyValue& value) {
    int newSize = std::get<int>(value);
    this->setSize(newSize);
    return value;
  });
}

int WireSplitter::setSize(int newSize)
{
  if (newSize <= 1) {
    return static_cast<int>(this->outputs.size());
  }

  if (this->inputs[0].size() == static_cast<unsigned int>(newSize)
      && this->outputs.size() == static_cast<unsigned int>(newSize)) {
    return newSize;
  }

  setInput(0, Bus(newSize));
  std::vector<Bus> outs(newSize, Bus(1));
  setOutputs(outs);

  return newSize;
}

void WireSplitter::simulate(Simulator& sim)
{
  const unsigned int N     = this->outputs.size();
  const int          delay = getPropertyValue<int>("delay").value();

  for (unsigned int i = 0; i < N; i++) {
    State s;
    if (this->inputs[0].size() != N) {
      s = State::ERROR;
    } else {
      s = Wire::safeGetCurrentState(this->inputs[0][i]);
    }

    if (this->outputs[i].size() != 0) {
      sim.updateWire(this->outputs[i][0], s, delay, weak_from_this());
    }
  }
}

WireMerger::WireMerger(const std::vector<Bus>& inputs, Bus output)
  : Component(inputs, {std::move(output)})
{
  defineProperty("size", 2);
  defineProperty("delay", 0);

  setPropertyCallback("size", [this](const PropertyValue& value) {
    int newSize = std::get<int>(value);
    this->setSize(newSize);
    return value;
  });
}

int WireMerger::setSize(int newSize)
{
  if (newSize <= 1)
    return static_cast<int>(this->inputs.size());

  if (this->inputs.size() == static_cast<unsigned int>(newSize)
      && this->outputs[0].size() == static_cast<unsigned int>(newSize)) {
    return newSize;
  }

  std::vector<Bus> inputs(newSize, Bus());
  setInputs(inputs);
  setOutput(0, Bus(newSize));
  return newSize;
}

void WireMerger::simulate(Simulator& sim)
{
  const unsigned int N     = this->inputs.size();
  const int          delay = getPropertyValue<int>("delay").value();

  for (unsigned int i = 0; i < N; i++) {
    State s;
    if (this->inputs[i].size() == 0) {
      s = State::ERROR;
    } else {
      s = Wire::safeGetCurrentState(this->inputs[i][0]);
    }

    if (this->outputs[0].size() > i) {
      sim.updateWire(this->outputs[0][i], s, delay, weak_from_this());
    }
  }
}
