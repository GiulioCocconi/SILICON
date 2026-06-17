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
const Logger simulationLog("simulation");

bool cancellationRequested(const Simulator::CancellationCheck& isCancelled)
{
  // An empty callback keeps existing synchronous callers on the zero-overhead path.
  return isCancelled && isCancelled();
}
}  // namespace

uint64_t Simulator::maxSimulationSteps          = 100000;
int      Simulator::maxTransitionsPerDeltaCycle = 1000;

Simulator::Simulator(std::shared_ptr<Circuit> c, uint64_t initialSimulationTime,
                     bool isInteractive, std::unique_ptr<SiliconFstWriter> fstWriter,
                     CancellationCheck isCancelled)
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
    // Construction can be expensive for large circuits, so it participates in the same
    // cooperative cancellation contract as later runs.
    if (!evaluateBlock(block, isCancelled))
      return;
  }
  emitTraceSnapshot();

  if (initialSimulationTime != 0)
    static_cast<void>(run(initialSimulationTime, std::move(isCancelled)));
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

void Simulator::enableFstTracing(std::string_view          fileName,
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

bool Simulator::evaluateBlock(const Circuit::SimulationBlock& block,
                              const CancellationCheck&        isCancelled)
{
  if (cancellationRequested(isCancelled))
    return false;

  if (!block.isCyclic) {
    auto validComps =
        block.executionOrder
        | std::views::transform([](const auto& weakComp) { return weakComp.lock(); })
        | std::views::filter([](const auto& comp) { return comp != nullptr; });

    for (const auto& comp : validComps) {
      if (cancellationRequested(isCancelled))
        return false;
      comp->simulate(*this);
    }
  } else {
    auto [vi_begin, vi_end] = boost::vertices(block.circuit.getGraph());
    auto cyclicComps =
        std::ranges::subrange(vi_begin, vi_end)
        | std::views::transform(
            [&g = block.circuit.getGraph()](auto v) { return g[v].component; })
        | std::views::filter([](const auto& comp) { return comp != nullptr; })
        | std::ranges::to<std::vector>();

    const bool isStable =
        std::ranges::any_of(std::views::iota(0, maxTransitionsPerDeltaCycle), [&](int) {
          // Returning true exits any_of immediately; the check below distinguishes
          // cancellation from actual convergence.
          if (cancellationRequested(isCancelled))
            return true;

          cyclicStateChanged = false;
          for (const auto& comp : cyclicComps) {
            if (cancellationRequested(isCancelled))
              return true;
            comp->simulate(*this);
          }
          return !cyclicStateChanged;
        });

    if (cancellationRequested(isCancelled))
      return false;

    if (!isStable) {
      const std::string errorMsg =
          "Delta cycle limit exceeded! Unstable zero-delay loop detected.";
      simulationLog.warning(errorMsg);
      throw std::runtime_error(errorMsg);
    }
  }

  return true;
}

Simulator::RunResult Simulator::run(uint64_t duration, CancellationCheck isCancelled)
{
  simulationLog.info(std::format("Running simulation with a duration of {}", duration));
  const uint64_t minimumEndTime = currentTime + duration;

  uint64_t processedSteps = 0;
  while (!eventQueue.empty() && eventQueue.top().time <= minimumEndTime) {
    // Stop only between event batches so a timestamp is never left half-applied.
    if (cancellationRequested(isCancelled))
      return RunResult::Cancelled;

    if (processedSteps >= maxSimulationSteps) {
      simulationLog.warning("Simulation step limit exceeded before the event queue "
                            "stabilized. This might be an unstable circuit.");
      return RunResult::StepLimitReached;
    }
    ++processedSteps;

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
        if (!evaluateBlock(block, isCancelled))
          return RunResult::Cancelled;
      }
      emitTraceSnapshot();
    }
  }

  if (currentTime < minimumEndTime) {
    currentTime = minimumEndTime;
    emitTraceSnapshot();
  }

  simulationLog.info("Circuit state stabilized (simulation complete)");
  return RunResult::Completed;
}

Simulator::RunResult Simulator::simulateWaveform(
    const uint64_t duration, std::span<const SiliconWaveformSample> inputSnapshots,
    std::span<const WaveformInputDriver> inputDrivers, CancellationCheck isCancelled)
{
  for (const auto& sample : inputSnapshots) {
    if (sample.time > currentTime) {
      const auto result = run(sample.time - currentTime, isCancelled);
      if (result != RunResult::Completed)
        return result;
    }

    const auto inputCount = std::min(sample.values.size(), inputDrivers.size());
    for (std::size_t i = 0; i < inputCount; ++i) {
      const auto& driver = inputDrivers[i];
      const auto  result = setBus(driver.bus, rawBitsToUnsignedValue(sample.values[i]),
                                  driver.source, isCancelled);
      if (result != RunResult::Completed)
        return result;
    }
  }

  if (duration > currentTime)
    return run(duration - currentTime, isCancelled);
  return RunResult::Completed;
}

void Simulator::setMaxSimulationSteps(uint64_t value)
{
  maxSimulationSteps = std::max<uint64_t>(1, value);
}

void Simulator::setMaxTransitionsPerDeltaCycle(int value)
{
  maxTransitionsPerDeltaCycle = std::max(1, value);
}

uint64_t Simulator::getMaxSimulationSteps()
{
  return maxSimulationSteps;
}

int Simulator::getMaxTransitionsPerDeltaCycle()
{
  return maxTransitionsPerDeltaCycle;
}

Simulator::RunResult Simulator::setBus(Bus bus, unsigned int value,
                                       CancellationCheck isCancelled)
{
  return setBus(std::move(bus), value, {}, std::move(isCancelled));
}

Simulator::RunResult Simulator::setBus(Bus bus, const unsigned int value,
                                       const Component_weakPtr& source,
                                       CancellationCheck        isCancelled)
{
  if (cancellationRequested(isCancelled))
    return RunResult::Cancelled;

  // Early return if bus current value == new value (only if the prev value is valid)
  if (!bus.isInErrorState() && !bus.hasUnknowns()) {
    unsigned int currentVal = bus.getCurrentValue();
    if (currentVal == value)
      return RunResult::Completed;
  }

  bus.forceSetCurrentValue(value, source);

  Circuit subCircuit = circuit->getForwardSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    if (!evaluateBlock(block, isCancelled))
      return RunResult::Cancelled;
  }
  emitTraceSnapshot();
  return RunResult::Completed;
}

Simulator::RunResult Simulator::simulateBus(const Bus& bus, CancellationCheck isCancelled)
{
  Circuit subCircuit = circuit->getBackwardsSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();

  for (const auto& block : blocks) {
    if (!evaluateBlock(block, isCancelled))
      return RunResult::Cancelled;
  }
  emitTraceSnapshot();
  return RunResult::Completed;
}
