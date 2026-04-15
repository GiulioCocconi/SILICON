/*
  Copyright (C) 2026 Giulio Cocconi

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

#include "simulator.hpp"

#include <algorithm>
#include <ranges>

#include <core/component.hpp>

Simulator::Simulator(std::shared_ptr<Circuit> c) : circuit(std::move(c))
{
  if (!circuit) {
    throw std::invalid_argument("Simulator requires a valid Circuit pointer");
  }

  // 1. Arm the live-editing observer chain
  circuit->makeInteractive();
  topologyListenerId = circuit->addTopologyListener([this]() { this->recompile(); });

  // 2. Initial compile & evaluation
  recompile();
  for (const auto& block : executionBlocks) {
    evaluateBlock(block);
  }
}

Simulator::~Simulator()
{
  if (circuit) {
    circuit->removeTopologyListener(topologyListenerId);
  }
}

void Simulator::recompile()
{
  executionBlocks = circuit->splitCyclic();
}

void Simulator::updateWire(const Wire_ptr& target, State newState, uint64_t delay,
                           const Component_weakPtr& source)
{
  if (!target)
    return;

  if (delay == 0) {
    if (target->getCurrentState() != newState) {
      target->setCurrentState(newState, source);
      cyclicStateChanged = true;
    }
  } else {
    eventQueue.push({currentTime + delay, target, newState, source});
  }
}

void Simulator::evaluateBlock(const Circuit::SimulationBlock& block)
{
  if (!block.isCyclic) {
    auto validComps =
        block.executionOrder
        | std::views::transform([](const auto& weakComp) { return weakComp.lock(); })
        | std::views::filter([](const auto& comp) { return comp != nullptr; });

    std::ranges::for_each(validComps, [&](const auto& comp) { comp->simulate(*this); });
  } else {
    auto [vi_begin, vi_end] = boost::vertices(block.circuit.getGraph());
    auto cyclicComps =
        std::ranges::subrange(vi_begin, vi_end)
        | std::views::transform(
            [&g = block.circuit.getGraph()](auto v) { return g[v].component; })
        | std::views::filter([](const auto& comp) { return comp != nullptr; })
        | std::ranges::to<std::vector>();

    const bool isStable = std::ranges::any_of(std::views::iota(0, MAX_DELTA), [&](int) {
      cyclicStateChanged = false;
      std::ranges::for_each(cyclicComps,
                            [&](const auto& comp) { comp->simulate(*this); });
      return !cyclicStateChanged;
    });

    if (!isStable) {
      throw std::runtime_error(
          "Delta cycle limit exceeded! Unstable zero-delay loop detected.");
    }
  }
}

void Simulator::run(uint64_t duration)
{
  uint64_t endTime = currentTime + duration;

  while (!eventQueue.empty()) {
    if (eventQueue.top().time > endTime)
      break;

    currentTime        = eventQueue.top().time;
    bool inputsChanged = false;

    while (!eventQueue.empty() && eventQueue.top().time == currentTime) {
      auto ev = eventQueue.top();
      eventQueue.pop();

      if (ev.targetWire && ev.targetWire->getCurrentState() != ev.newState) {
        ev.targetWire->setCurrentState(ev.newState, ev.source);
        inputsChanged = true;
      }
    }

    if (inputsChanged) {
      for (const auto& block : executionBlocks) {
        evaluateBlock(block);
      }
    }
  }

  currentTime = endTime;
}

void Simulator::setBus(Bus bus, unsigned int value)
{
  if (!bus.isInErrorState()) {
    unsigned int currentVal = bus.getCurrentValue();
    if (currentVal == value)
      return;
  }
  bus.forceSetCurrentValue(value);

  Circuit subCircuit = circuit->getForwardSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    evaluateBlock(block);
  }
}

void Simulator::simulateBus(const Bus& bus)
{
  Circuit subCircuit = circuit->getBackwardsSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    evaluateBlock(block);
  }
}
