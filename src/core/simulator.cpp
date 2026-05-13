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
#include <utility>

#include <core/component.hpp>

#include <logging/logger.hpp>

namespace {
Logger simulationLog("simulation");
}

Simulator::Simulator(std::shared_ptr<Circuit> c, uint64_t initialSimulationTime,
                     bool isInteractive, std::unique_ptr<SiliconFstWriter> fstWriter)
  : circuit(std::move(c)), fstWriter(std::move(fstWriter))
{
  if (!circuit) {
    throw std::invalid_argument("Simulator requires a valid Circuit pointer");
  }

  // Arm the live-editing observer chain

  if (isInteractive) {
    circuit->makeInteractive();
    topologyListenerId = circuit->addTopologyListener([this]() { this->recompile(); });
  }

  // 2. Initial compile & evaluation
  recompile();
  for (const auto& block : executionBlocks) {
    evaluateBlock(block);
  }
  emitTraceSnapshot();

  if (initialSimulationTime != 0)
    run(initialSimulationTime);
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

void Simulator::enableFstTracing(const std::string&        fileName,
                                 SiliconFstWriter::Options options)
{
  if (!traceBuses.empty()) {
    setFstWriter(
        std::make_unique<SiliconFstWriter>(fileName, traceBuses, std::move(options)));
  } else {
    setFstWriter(
        std::make_unique<SiliconFstWriter>(fileName, *circuit, std::move(options)));
  }
}

void Simulator::setFstWriter(std::unique_ptr<SiliconFstWriter> writer)
{
  fstWriter = std::move(writer);
  emitTraceSnapshot();
}

void Simulator::setTraceBuses(std::vector<SiliconFstWriter::NamedBus> buses)
{
  traceBuses = std::move(buses);
  emitTraceSnapshot();
}

void Simulator::setTraceSink(TraceSink sink)
{
  traceSink = std::move(sink);
  emitTraceSnapshot();
}

void Simulator::emitTraceSnapshot()
{
  if (fstWriter) {
    fstWriter->emitSnapshot(currentTime);
    fstWriter->flush();
  }

  if (!traceSink || traceBuses.empty())
    return;

  std::vector<std::string> values;
  values.reserve(traceBuses.size());
  for (const auto& [name, bus] : traceBuses)
    values.push_back(encodeTraceBusValue(bus));

  traceSink(currentTime, values);
}

std::string Simulator::encodeTraceBusValue(const Bus& bus)
{
  std::string value;
  value.reserve(bus.size());

  for (auto it = bus.end(); it != bus.begin();) {
    --it;
    if (!*it) {
      value.push_back('x');
    } else {
      value.push_back(SiliconFstWriter::stateToFstValue((*it)->getCurrentState()));
    }
  }

  return value;
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
      const std::string errorMsg =
          "Delta cycle limit exceeded! Unstable zero-delay loop detected.";
      simulationLog.warning(errorMsg);
      throw std::runtime_error(errorMsg);
    }
  }
}

void Simulator::run(uint64_t duration)
{
  simulationLog.info(std::format("Running simulation with a duration of {}", duration));
  const uint64_t minimumEndTime = currentTime + duration;

  // Safeguard: Prevent GUI thread freezing if the circuit contains an oscillator.
  // Allows signals to propagate far into the future, but eventually stops.
  const uint64_t safetyTimeout = minimumEndTime + 100000;

  while (!eventQueue.empty()) {
    if (eventQueue.top().time > safetyTimeout) {
      simulationLog.warning("Simulation time got past timeout but the simulation queue "
                            "is not empty. This might be an unstable circuit, if that's "
                            "not the case please increase the safety timeout");
      return;
    }

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
      emitTraceSnapshot();
    }
  }

  if (currentTime < minimumEndTime) {
    currentTime = minimumEndTime;
    emitTraceSnapshot();
  }

  simulationLog.info("Circuit state stabilized (simulation complete)");
}

void Simulator::setBus(Bus bus, unsigned int value)
{
  setBus(std::move(bus), value, {});
}

void Simulator::setBus(Bus bus, const unsigned int value, const Component_weakPtr& source)
{
  // Early return if bus current value == new value (only if the prev value is valid)
  if (!bus.isInErrorState() && !bus.hasUnknowns()) {
    unsigned int currentVal = bus.getCurrentValue();
    if (currentVal == value)
      return;
  }

  bus.forceSetCurrentValue(value, source);

  Circuit subCircuit = circuit->getForwardSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    evaluateBlock(block);
  }
  emitTraceSnapshot();
}

void Simulator::simulateBus(const Bus& bus)
{
  Circuit subCircuit = circuit->getBackwardsSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    evaluateBlock(block);
  }
  emitTraceSnapshot();
}
