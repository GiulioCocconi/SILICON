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

#include "simulator.hpp"

#include "utils/num_formatting.hpp"
#include "utils/ranges_wrapper.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

#include <core/component.hpp>
#include <core/wireUtils.hpp>

#include <logging/logger.hpp>

namespace SILICON::simulation {

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

namespace {
  const SILICON::logging::Logger simulationLog("simulation");

  struct StepVectorHash {
    [[nodiscard]] std::size_t
    operator()(const std::vector<std::size_t>& values) const noexcept
    {
      std::size_t seed = values.size();
      for (const auto value : values) {
        seed ^= std::hash<std::size_t>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      }
      return seed;
    }
  };

  bool cancellationRequested(const Simulator::CancellationCheck& isCancelled)
  {
    // An empty callback keeps existing synchronous callers on the zero-overhead path.
    return isCancelled && isCancelled();
  }

  void capturePreviousWireState(std::unordered_map<uint64_t, State>& previousWireStates,
                                const Wire_ptr&                      wire)
  {
    if (wire)
      previousWireStates.emplace(wire->getId(), wire->getCurrentState());
  }

  void capturePreviousBusStates(std::unordered_map<uint64_t, State>& previousWireStates,
                                const Bus&                           bus)
  {
    for (const auto& wire : bus)
      capturePreviousWireState(previousWireStates, wire);
  }

}  // namespace

uint64_t Simulator::maxSimulationSteps          = 100000;
int      Simulator::maxTransitionsPerDeltaCycle = 1000;

std::size_t Simulator::PendingTransitionKeyHash::operator()(
    const PendingTransitionKey& key) const noexcept
{
  auto seed = std::hash<uint64_t>{}(key.first);
  seed ^=
      std::hash<std::uintptr_t>{}(key.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}

class Simulator::EvaluationStateGuard {
public:
  EvaluationStateGuard(Simulator&                           simulator,
                       std::unordered_map<uint64_t, State>* activePreviousWireStates,
                       bool                                 stageSequentialOutputs)
    : simulator(simulator),
      previousActivePreviousWireStates(simulator.activePreviousWireStates),
      previousStageSequentialOutputs(simulator.stageSequentialOutputs)
  {
    simulator.activePreviousWireStates = activePreviousWireStates;
    simulator.stageSequentialOutputs   = stageSequentialOutputs;
  }

  ~EvaluationStateGuard()
  {
    simulator.activePreviousWireStates = previousActivePreviousWireStates;
    simulator.stageSequentialOutputs   = previousStageSequentialOutputs;
  }

  void setActivePreviousWireStates(
      std::unordered_map<uint64_t, State>* activePreviousWireStates) const
  {
    simulator.activePreviousWireStates = activePreviousWireStates;
  }

  void setStageSequentialOutputs(bool enabled) const
  {
    simulator.stageSequentialOutputs = enabled;
  }

private:
  Simulator&                           simulator;
  std::unordered_map<uint64_t, State>* previousActivePreviousWireStates = nullptr;
  bool                                 previousStageSequentialOutputs   = false;
};

Simulator::EdgeType Simulator::edgeType(const Context& context, const Wire_ptr& wire)
{
  const auto previousState = context.previousState(wire);
  if (!previousState || !wire)
    return EdgeType::NO_CHANGE;

  const State currentState = wire->getCurrentState();
  if (*previousState == currentState)
    return EdgeType::NO_CHANGE;

  if (*previousState == State::LOW && currentState == State::HIGH)
    return EdgeType::RISE;

  if (*previousState == State::HIGH && currentState == State::LOW)
    return EdgeType::FALL;

  return EdgeType::UNKNOWN;
}

Simulator::Simulator(std::shared_ptr<Circuit> c, uint64_t initialSimulationTime,
                     bool isInteractive, std::unique_ptr<CircuitWriter> fstWriter,
                     CancellationCheck isCancelled)
  : circuit(std::move(c)), fstWriter(std::move(fstWriter))
{
  if (!circuit) {
    throw std::invalid_argument("Simulator requires a valid Circuit pointer");
  }

  // Arm the live-editing observer chain

  if (isInteractive) {
    circuit->makeInteractive();
  }

  topologyListenerId = circuit->addTopologyListener([this]() { recompile(); });
  try {
    recompile();

    // Construction can be expensive for large circuits, so it participates in the
    // same cooperative cancellation contract as later runs.
    const Context initialContext{true, {}};
    if (!evaluateExecutionPlan(executionPlan, initialContext, isCancelled))
      return;
    emitTraceSnapshot();

    if (initialSimulationTime != 0)
      static_cast<void>(run(initialSimulationTime, std::move(isCancelled)));
  } catch (...) {
    circuit->removeTopologyListener(topologyListenerId);
    topologyListenerId = 0;
    throw;
  }
}

Simulator::~Simulator()
{
  if (circuit && topologyListenerId != 0)
    circuit->removeTopologyListener(topologyListenerId);
}

void Simulator::recompile()
{
  executionPlan.clear();
  forwardExecutionStepsByWire.clear();
  pendingTransitions.clear();
  stagedSequentialTransitions.clear();
  eventQueue = {};

  auto compiledBlocks = circuit->splitCyclic();
  executionPlan       = compileExecutionPlan(compiledBlocks);

  std::unordered_map<const Component*, std::size_t> componentToStep;
  for (const auto& [stepIndex, executionStep] :
       executionPlan | SILICON::views::enumerate) {
    for (const auto& weakComp : executionStep.components) {
      if (auto comp = weakComp.lock()) {
        componentToStep[comp.get()] = stepIndex;
      }
    }
  }

  std::vector<std::vector<std::size_t>> stepGraph(executionPlan.size());
  const auto&                           graph = circuit->getGraph();

  for (const auto edge : boost::make_iterator_range(boost::edges(graph))) {
    const auto sourceComp = graph[boost::source(edge, graph)].component;
    const auto targetComp = graph[boost::target(edge, graph)].component;
    if (!sourceComp || !targetComp)
      continue;

    const auto sourceIt = componentToStep.find(sourceComp.get());
    const auto targetIt = componentToStep.find(targetComp.get());
    if (sourceIt == componentToStep.end() || targetIt == componentToStep.end()
        || sourceIt->second == targetIt->second)
      continue;

    auto& successors = stepGraph[sourceIt->second];
    if (!std::ranges::contains(successors, targetIt->second))
      successors.push_back(targetIt->second);
  }

  std::unordered_map<uint64_t, std::vector<std::size_t>> seedStepsByWire;
  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    const auto comp = graph[vertex].component;
    if (!comp)
      continue;

    const auto stepIt = componentToStep.find(comp.get());
    if (stepIt == componentToStep.end())
      continue;

    auto inputWires =
        comp->inputBuses() | std::views::join
        | std::views::filter([](const auto& wire) { return static_cast<bool>(wire); });

    for (const auto& wire : inputWires) {
      auto& seeds = seedStepsByWire[wire->getId()];
      if (!std::ranges::contains(seeds, stepIt->second))
        seeds.push_back(stepIt->second);
    }
  }

  std::unordered_map<std::vector<std::size_t>, std::vector<std::size_t>, StepVectorHash>
      affectedStepsBySeedSet;
  affectedStepsBySeedSet.reserve(seedStepsByWire.size());
  for (auto& [wireId, seedSteps] : seedStepsByWire) {
    std::ranges::sort(seedSteps);

    const auto cachedAffectedSteps = affectedStepsBySeedSet.find(seedSteps);
    if (cachedAffectedSteps != affectedStepsBySeedSet.end()) {
      forwardExecutionStepsByWire.emplace(wireId, cachedAffectedSteps->second);
      continue;
    }

    std::vector<std::size_t> stack(seedSteps.begin(), seedSteps.end());
    std::vector<char>        visited(executionPlan.size(), false);

    while (!stack.empty()) {
      const auto stepIndex = stack.back();
      stack.pop_back();

      if (stepIndex >= executionPlan.size() || visited[stepIndex])
        continue;

      visited[stepIndex] = true;
      for (const auto successor : stepGraph[stepIndex])
        stack.push_back(successor);
    }

    std::vector<std::size_t> affectedSteps;
    affectedSteps.reserve(executionPlan.size());
    for (std::size_t i = 0; i < visited.size(); ++i) {
      if (visited[i])
        affectedSteps.push_back(i);
    }

    auto [insertedAffectedSteps, _inserted] =
        affectedStepsBySeedSet.emplace(seedSteps, std::move(affectedSteps));
    forwardExecutionStepsByWire.emplace(wireId, insertedAffectedSteps->second);
  }

  simulationLog.debug(std::format("Compiled {} simulation steps", executionPlan.size()));
}

void Simulator::enableFstTracing(std::string_view       fileName,
                                 CircuitWriter::Options options)
{
  if (!traceBuses.empty()) {
    setFstWriter(
        std::make_unique<CircuitWriter>(fileName, traceBuses, std::move(options)));
  } else {
    setFstWriter(std::make_unique<CircuitWriter>(fileName, *circuit, std::move(options)));
  }
}

void Simulator::setFstWriter(std::unique_ptr<CircuitWriter> writer)
{
  fstWriter = std::move(writer);
  emitTraceSnapshot();
}

void Simulator::setTraceBuses(std::vector<CircuitWriter::NamedBus> buses)
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

  std::vector<BusValue> values;
  values.reserve(traceBuses.size());
  for (const auto& [name, bus] : traceBuses)
    values.push_back(bus.getCurrentValue());

  traceSink(currentTime, values);
}

Simulator::PendingTransitionKey
Simulator::pendingTransitionKey(const Wire_ptr& target, const Component_weakPtr& source)
{
  const auto sourceAddress = reinterpret_cast<std::uintptr_t>(source.lock().get());
  return {target ? target->getId() : 0, sourceAddress};
}

void Simulator::scheduleDelayedWireUpdate(const Wire_ptr& target, const State newState,
                                          const uint64_t           delay,
                                          const Component_weakPtr& source)
{
  const auto key = pendingTransitionKey(target, source);

  if (target->getCurrentState() == newState) {
    pendingTransitions.erase(key);
    return;
  }

  const uint64_t eventTime = currentTime + delay;
  const auto     pending   = pendingTransitions.find(key);
  if (pending != pendingTransitions.end() && pending->second.state == newState
      && pending->second.time == eventTime)
    return;

  pendingTransitions[key] = {eventTime, newState};
  eventQueue.push({eventTime, target, newState, source});
}

bool Simulator::shouldStageSequentialOutput(const Component_weakPtr& source) const
{
  const auto sourceComponent = source.lock();
  return stageSequentialOutputs && sourceComponent
         && sourceComponent->usesStagedSequentialOutputs();
}

void Simulator::stageSequentialWireUpdate(const Wire_ptr& target, const State newState,
                                          const Component_weakPtr& source)
{
  const auto key = pendingTransitionKey(target, source);
  pendingTransitions.erase(key);

  if (target->getCurrentState() == newState) {
    stagedSequentialTransitions.erase(key);
    return;
  }

  stagedSequentialTransitions[key] = {target, newState, source};
}

std::vector<Bus> Simulator::commitStagedSequentialTransitions(
    std::unordered_map<uint64_t, State>& previousWireStates)
{
  std::vector<Bus> changedBuses;

  auto transitions = std::move(stagedSequentialTransitions);
  stagedSequentialTransitions.clear();

  for (const auto& [_key, transition] : transitions) {
    if (!transition.target || transition.target->getCurrentState() == transition.state)
      continue;

    capturePreviousWireState(previousWireStates, transition.target);
    transition.target->setCurrentState(transition.state, transition.source);
    changedBuses.push_back(Bus{transition.target});
    cyclicStateChanged = true;
  }

  return changedBuses;
}

bool Simulator::isDelayedEventPending(const TimedEvent& event) const
{
  if (!event.targetWire)
    return false;

  const auto key = pendingTransitionKey(event.targetWire, event.source);
  const auto it  = pendingTransitions.find(key);
  return it != pendingTransitions.end() && it->second.time == event.time
         && it->second.state == event.newState;
}

std::vector<Simulator::ExecutionStep>
Simulator::compileExecutionPlan(std::span<const Circuit::SimulationBlock> blocks)
{
  std::vector<ExecutionStep> plan;

  for (const auto& block : blocks) {
    if (!block.isCyclic) {
      for (const auto& weakComp : block.executionOrder) {
        if (auto comp = weakComp.lock())
          plan.push_back({false, {comp}});
      }
      continue;
    }

    std::vector<Component_weakPtr> cyclicComps;
    const auto&                    blockGraph = block.circuit.getGraph();
    auto [viBegin, viEnd]                     = boost::vertices(blockGraph);
    for (const auto vertex : std::ranges::subrange(viBegin, viEnd)) {
      if (auto comp = blockGraph[vertex].component)
        cyclicComps.push_back(comp);
    }

    if (!cyclicComps.empty())
      plan.push_back({.isCyclic = true, .components = std::move(cyclicComps)});
  }

  return plan;
}

bool Simulator::evaluateExecutionStep(const ExecutionStep& step, const Context& context,
                                      const CancellationCheck& isCancelled)
{
  if (cancellationRequested(isCancelled))
    return false;

  if (!step.isCyclic) {
    for (const auto& weakComp : step.components) {
      if (cancellationRequested(isCancelled))
        return false;

      if (auto comp = weakComp.lock())
        comp->simulate(*this, context);
    }
    return true;
  }

  auto cyclicComps =
      step.components
      | std::views::transform([](const auto& weakComp) { return weakComp.lock(); })
      | std::views::filter([](const auto& comp) { return comp != nullptr; })
      | std::ranges::to<std::vector>();

  for (int i = 0; i < maxTransitionsPerDeltaCycle; ++i) {
    if (cancellationRequested(isCancelled))
      return false;

    cyclicStateChanged = false;
    for (const auto& comp : cyclicComps) {
      if (cancellationRequested(isCancelled))
        return false;
      comp->simulate(*this, context);
    }

    if (!cyclicStateChanged)
      return true;
  }

  const std::string errorMsg =
      "Delta cycle limit exceeded! Unstable zero-delay loop detected.";
  simulationLog.warning(errorMsg);
  throw std::runtime_error(errorMsg);
}

bool Simulator::evaluateExecutionPlan(std::span<const ExecutionStep> steps,
                                      const Context&                 context,
                                      const CancellationCheck&       isCancelled)
{
  for (const auto& step : steps) {
    if (!evaluateExecutionStep(step, context, isCancelled))
      return false;
  }
  return true;
}

bool Simulator::evaluateExecutionStepIndices(std::span<const std::size_t> stepIndices,
                                             const Context&               context,
                                             const CancellationCheck&     isCancelled)
{
  for (const auto stepIndex : stepIndices) {
    if (stepIndex >= executionPlan.size())
      continue;

    if (!evaluateExecutionStep(executionPlan[stepIndex], context, isCancelled))
      return false;
  }
  return true;
}

Simulator::RunResult
Simulator::evaluateExecutionStepIndicesAndTrace(std::span<const std::size_t> stepIndices,
                                                const Context&               context,
                                                const CancellationCheck&     isCancelled)
{
  if (!evaluateExecutionStepIndices(stepIndices, context, isCancelled))
    return RunResult::Cancelled;

  emitTraceSnapshot();
  return RunResult::Completed;
}

std::vector<std::size_t>
Simulator::getForwardExecutionSteps(std::span<const Bus> changedBuses) const
{
  std::vector<std::size_t> steps;

  for (const auto& bus : changedBuses) {
    for (const auto& wire : bus) {
      if (!wire)
        continue;

      const auto it = forwardExecutionStepsByWire.find(wire->getId());
      if (it == forwardExecutionStepsByWire.end())
        continue;

      steps.insert(steps.end(), it->second.begin(), it->second.end());
    }
  }

  std::ranges::sort(steps);
  const auto [uniqueBegin, uniqueEnd] = std::ranges::unique(steps);
  steps.erase(uniqueBegin, uniqueEnd);
  return steps;
}

void Simulator::updateWire(const Wire_ptr& target, const State newState,
                           const uint64_t delay, const Component_weakPtr& source)
{
  if (!target)
    return;

  if (delay != 0) {
    scheduleDelayedWireUpdate(target, newState, delay, source);
    return;
  }

  if (shouldStageSequentialOutput(source)) {
    stageSequentialWireUpdate(target, newState, source);
    return;
  }

  pendingTransitions.erase(pendingTransitionKey(target, source));

  if (target->getCurrentState() == newState)
    return;

  if (activePreviousWireStates)
    capturePreviousWireState(*activePreviousWireStates, target);

  target->setCurrentState(newState, source);
  cyclicStateChanged = true;
}

void Simulator::updateBus(const Bus& bus, const BusValue& value, const uint64_t delay,
                          const Component_weakPtr& source)
{
  const auto normalized = SILICON::wireUtils::normalizeBusValue(value, bus.size());
  for (auto i = 0uz; i < bus.size(); ++i) {
    updateWire(bus[i], normalized[i], delay, source);
  }
}

Simulator::RunResult
Simulator::evaluateExecutionPlanAndTrace(std::span<const ExecutionStep> steps,
                                         const Context&                 context,
                                         const CancellationCheck&       isCancelled)
{
  if (!evaluateExecutionPlan(steps, context, isCancelled))
    return RunResult::Cancelled;

  emitTraceSnapshot();
  return RunResult::Completed;
}

Simulator::RunResult Simulator::evaluateForwardConeAndTrace(
    std::span<const Bus>                changedBuses,
    std::unordered_map<uint64_t, State> previousWireStates,
    const CancellationCheck& isCancelled, const bool enableSequentialStaging)
{
  Context                    context{false, changedBuses, std::move(previousWireStates)};
  const auto                 steps = getForwardExecutionSteps(changedBuses);
  const EvaluationStateGuard evaluationState(*this, &context.previousWireStates,
                                             enableSequentialStaging);

  try {
    const auto completed = evaluateExecutionStepIndices(steps, context, isCancelled);
    if (!completed) {
      stagedSequentialTransitions.clear();
      return RunResult::Cancelled;
    }

    evaluationState.setStageSequentialOutputs(false);
    std::unordered_map<uint64_t, State> stagedPreviousWireStates;
    const auto                          stagedChangedBuses =
        enableSequentialStaging
                                     ? commitStagedSequentialTransitions(stagedPreviousWireStates)
                                     : std::vector<Bus>{};

    if (!stagedChangedBuses.empty()) {
      auto stagedContext =
          Context{false, stagedChangedBuses, std::move(stagedPreviousWireStates)};
      const auto stagedSteps = getForwardExecutionSteps(stagedChangedBuses);
      evaluationState.setActivePreviousWireStates(&stagedContext.previousWireStates);
      if (!evaluateExecutionStepIndices(stagedSteps, stagedContext, isCancelled)) {
        return RunResult::Cancelled;
      }
    }

    emitTraceSnapshot();
    return RunResult::Completed;
  } catch (...) {
    stagedSequentialTransitions.clear();
    throw;
  }
}

Simulator::RunResult Simulator::run(uint64_t duration, CancellationCheck isCancelled)
{
  simulationLog.info(std::format("Running simulation with a duration of {}", duration));
  const uint64_t minimumEndTime = currentTime + duration;

  uint64_t processedSteps = 0;
  while (!eventQueue.empty() && eventQueue.top().time <= minimumEndTime) {
    if (processedSteps >= maxSimulationSteps) {
      simulationLog.warning("Simulation step limit exceeded before the event queue "
                            "stabilized. This might be an unstable circuit.");
      return RunResult::StepLimitReached;
    }
    ++processedSteps;

    const auto result = processNextEventBatch(isCancelled);
    if (result != RunResult::Completed)
      return result;
  }

  if (currentTime < minimumEndTime) {
    currentTime = minimumEndTime;
    emitTraceSnapshot();
  }

  simulationLog.info("Circuit state stabilized (simulation complete)");
  return RunResult::Completed;
}

Simulator::RunResult Simulator::runUntilIdle(CancellationCheck isCancelled)
{
  simulationLog.info("Running simulation until pending events are idle");

  uint64_t processedSteps = 0;
  while (!eventQueue.empty()) {
    if (processedSteps >= maxSimulationSteps) {
      simulationLog.warning("Simulation step limit exceeded before the event queue "
                            "stabilized. This might be an unstable circuit.");
      return RunResult::StepLimitReached;
    }
    ++processedSteps;

    const auto result = processNextEventBatch(isCancelled);
    if (result != RunResult::Completed)
      return result;
  }

  simulationLog.info("Circuit state stabilized (simulation complete)");
  return RunResult::Completed;
}

Simulator::RunResult
Simulator::processNextEventBatch(const CancellationCheck& isCancelled)
{
  // Stop only between event batches so a timestamp is never left half-applied.
  if (cancellationRequested(isCancelled))
    return RunResult::Cancelled;

  currentTime = eventQueue.top().time;
  std::vector<Bus>                    changedBuses;
  std::unordered_map<uint64_t, State> previousWireStates;

  while (!eventQueue.empty() && eventQueue.top().time == currentTime) {
    auto ev = eventQueue.top();
    eventQueue.pop();

    if (!isDelayedEventPending(ev))
      continue;

    capturePreviousWireState(previousWireStates, ev.targetWire);
    pendingTransitions.erase(pendingTransitionKey(ev.targetWire, ev.source));

    if (ev.targetWire && ev.targetWire->getCurrentState() != ev.newState) {
      ev.targetWire->setCurrentState(ev.newState, ev.source);
      changedBuses.push_back(Bus{ev.targetWire});
    }
  }

  if (changedBuses.empty())
    return RunResult::Completed;

  return evaluateForwardConeAndTrace(changedBuses, std::move(previousWireStates),
                                     isCancelled);
}

Simulator::RunResult Simulator::simulateWaveform(
    const uint64_t duration, std::span<const Sample> inputSnapshots,
    std::span<const WaveformInputDriver> inputDrivers, CancellationCheck isCancelled)
{
  for (std::size_t sampleIndex = 0; sampleIndex < inputSnapshots.size();) {
    const uint64_t sampleTime = inputSnapshots[sampleIndex].time;
    if (sampleTime > currentTime) {
      const auto result = run(sampleTime - currentTime, isCancelled);
      if (result != RunResult::Completed)
        return result;
    }

    std::vector<BusValue> values = inputSnapshots[sampleIndex].values;
    ++sampleIndex;
    while (sampleIndex < inputSnapshots.size()
           && inputSnapshots[sampleIndex].time == sampleTime) {
      const auto& sameTimeValues = inputSnapshots[sampleIndex].values;
      if (values.size() < sameTimeValues.size())
        values.resize(sameTimeValues.size());
      for (std::size_t i = 0; i < sameTimeValues.size(); ++i)
        values[i] = sameTimeValues[i];
      ++sampleIndex;
    }

    const auto result = applyWaveformInputSample(values, inputDrivers, isCancelled);
    if (result != RunResult::Completed)
      return result;
  }

  if (duration > currentTime)
    return run(duration - currentTime, isCancelled);
  return RunResult::Completed;
}

Simulator::RunResult
Simulator::applyWaveformInputSample(std::span<const BusValue>            values,
                                    std::span<const WaveformInputDriver> inputDrivers,
                                    const CancellationCheck&             isCancelled)
{
  if (cancellationRequested(isCancelled))
    return RunResult::Cancelled;

  bool             inputsChanged = false;
  const auto       inputCount    = std::min(values.size(), inputDrivers.size());
  std::vector<Bus> changedBuses;
  std::unordered_map<uint64_t, State> previousWireStates;
  for (std::size_t i = 0; i < inputCount; ++i) {
    const auto& driver     = inputDrivers[i];
    auto        bus    = driver.bus;
    const auto  normalized = SILICON::wireUtils::normalizeBusValue(values[i], bus.size());

    if (bus.getCurrentValue() != normalized) {
      inputsChanged = true;
      changedBuses.push_back(bus);
      capturePreviousBusStates(previousWireStates, bus);
    }
    (void)bus.forceSetCurrentValue(normalized, driver.source);
  }

  if (!inputsChanged)
    return RunResult::Completed;

  return evaluateForwardConeAndTrace(changedBuses, std::move(previousWireStates),
                                     isCancelled);
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

Simulator::RunResult Simulator::setBus(Bus bus, BusValue value,
                                       CancellationCheck isCancelled)
{
  return setBus(std::move(bus), value, {}, std::move(isCancelled));
}

Simulator::RunResult Simulator::setBus(Bus bus, BusValue value,
                                       const Component_weakPtr& source,
                                       CancellationCheck        isCancelled)
{
  if (cancellationRequested(isCancelled))
    return RunResult::Cancelled;

  value = SILICON::wireUtils::normalizeBusValue(value, bus.size());

  // Early return if bus current value == new value (only if the prev value is valid)
  if (!bus.isInErrorState() && !bus.hasUnknowns()) {
    BusValue currentVal = bus.getCurrentValue();
    if (currentVal == value)
      return RunResult::Completed;
  }

  std::vector<Bus>                    changedBuses{bus};
  std::unordered_map<uint64_t, State> previousWireStates;
  capturePreviousBusStates(previousWireStates, bus);

  (void)bus.forceSetCurrentValue(value, source);

  return evaluateForwardConeAndTrace(changedBuses, std::move(previousWireStates),
                                     isCancelled);
}

Simulator::RunResult Simulator::simulateBus(const Bus& bus, CancellationCheck isCancelled)
{
  Circuit subCircuit = circuit->getBackwardsSubgraph(bus);
  auto    blocks     = subCircuit.splitCyclic();
  auto    plan       = compileExecutionPlan(blocks);

  const Context context{true, {}};
  return evaluateExecutionPlanAndTrace(plan, context, isCancelled);
}

}  // namespace SILICON::simulation
