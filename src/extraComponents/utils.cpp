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

#include <algorithm>

#include <core/simulator.hpp>

namespace SILICON::extra {
using namespace SILICON::core;

WireSplitter::WireSplitter()
{
  initializeProperties();
}

WireSplitter::WireSplitter(Bus input, const std::vector<Bus>& outputs)
  : Component({std::move(input)}, outputs)
{
  initializeProperties();
}

void WireSplitter::initializeProperties()
{
  // Default-constructed components are used by the registry and must keep empty
  // buses. The size callback only reshapes I/O after real buses have been attached.
  defineProperty("size", 2);
  defineProperty("delay", 0);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    return requireNonNegative("WireSplitter delay", value);
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int newSize = std::get<int>(requireValidSize("WireSplitter size", value));
    if (!this->inputs.empty())
      this->setSize(newSize);
    return value;
  });
}

int WireSplitter::setSize(int newSize)
{
  if (newSize <= 1) {
    return static_cast<int>(this->outputs.size());
  }

  if (this->inputs.size() == 1
      && this->inputs[0].size() == static_cast<unsigned int>(newSize)
      && this->outputs.size() == static_cast<unsigned int>(newSize)
      && std::ranges::all_of(this->outputs,
                             [](const Bus& bus) { return bus.size() == 1; })) {
    return newSize;
  }

  setInput(0, Bus(newSize));
  std::vector<Bus> outs;
  outs.reserve(newSize);
  for (int i = 0; i < newSize; ++i)
    outs.emplace_back(1);
  setOutputs(outs);

  return newSize;
}

void WireSplitter::simulate(SILICON::simulation::Simulator& sim)
{
  const unsigned int size  = getPropertyValue<int>("size").value();
  const int          delay = getPropertyValue<int>("delay").value();

  for (unsigned int i = 0; i < size; i++) {
    const State s = Wire::safeGetCurrentState(this->inputs[0][i]);

    if (this->outputs.size() > i && this->outputs[i].size() != 0) {
      sim.updateWire(this->outputs[i][0], s, delay, weak_from_this());
    }
  }
}

WireMerger::WireMerger()
{
  initializeProperties();
}

WireMerger::WireMerger(const std::vector<Bus>& inputs, Bus output)
  : Component(inputs, {std::move(output)})
{
  initializeProperties();
}

void WireMerger::initializeProperties()
{
  // Default-constructed components are used by the registry and must keep empty
  // buses. The size callback only reshapes I/O after real buses have been attached.
  defineProperty("size", 2);
  defineProperty("delay", 0);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    return requireNonNegative("WireMerger delay", value);
  });

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int newSize = std::get<int>(requireValidSize("WireMerger size", value));
    if (!this->outputs.empty())
      this->setSize(newSize);
    return value;
  });
}

int WireMerger::setSize(int newSize)
{
  if (newSize <= 1)
    return static_cast<int>(this->inputs.size());

  if (this->inputs.size() == static_cast<unsigned int>(newSize)
      && this->outputs.size() == 1
      && this->outputs[0].size() == static_cast<unsigned int>(newSize)
      && std::ranges::all_of(this->inputs,
                             [](const Bus& bus) { return bus.size() == 1; })) {
    return newSize;
  }

  std::vector<Bus> inputs;
  inputs.reserve(newSize);
  for (int i = 0; i < newSize; ++i)
    inputs.emplace_back(std::vector<Wire_ptr>(1));
  setInputs(inputs);
  setOutput(0, Bus(newSize));
  return newSize;
}

void WireMerger::simulate(SILICON::simulation::Simulator& sim)
{
  const unsigned int size  = getPropertyValue<int>("size").value();
  const int          delay = getPropertyValue<int>("delay").value();

  for (unsigned int i = 0; i < size; i++) {
    const bool  inputIsDisconnected = this->inputs[i].size() == 0 || !this->inputs[i][0];
    const State s                   = inputIsDisconnected ? State::UNKNOWN
                                                          : Wire::safeGetCurrentState(this->inputs[i][0]);

    if (this->outputs[0].size() > i) {
      sim.updateWire(this->outputs[0][i], s, delay, weak_from_this());
    }
  }
}

}  // namespace SILICON::extra
