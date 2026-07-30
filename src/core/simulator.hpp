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

#pragma once
#include <core/circuit.hpp>
#include <core/component.hpp>
#include <core/siliconFst.hpp>
#include <core/siliconWaveform.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SILICON::simulation {

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

/**
 * @brief Represents a timed event in the simulation.
 */
struct TimedEvent {
  /** @brief Simulation time at which the event occurs */
  uint64_t time;

  /** @brief The wire to update */
  Wire_ptr targetWire;

  /** @brief The new state to set on the wire */
  State newState;

  /** @brief The component that is the source of this state change */
  Component_weakPtr source;

  /** @brief Comparison operator for priority queue ordering */
  bool operator>(const TimedEvent& other) const { return time > other.time; }
};

/**
 * @class Simulator
 * @brief Executes event-driven simulation of a Circuit.
 *
 * The Simulator manages the execution of a digital circuit using an event-driven
 * simulation paradigm. It handles both acyclic circuit parts (executed once in
 * topological order) and cyclic parts (executed iteratively using delta cycles until
 * convergence).
 *
 * @note The simulator compiles the circuit topology into a reusable execution plan.
 * In interactive mode topology changes notify the simulator to rebuild that plan.
 */
class Simulator {
public:
  /** @brief Receives encoded waveform values for a simulation timestamp. */
  using TraceSink = std::function<void(uint64_t, const std::vector<std::string>&)>;

  /** @brief Bus driven by one column of an input waveform. */
  struct WaveformInputDriver {
    Bus               bus;
    Component_weakPtr source;
  };

  /**
   * @brief Framework-independent cooperative cancellation callback.
   * @return True when the active operation should stop at its next checkpoint
   */
  using CancellationCheck = std::function<bool()>;

  /** @brief Outcome of a cancellable simulator operation. */
  enum class RunResult {
    Completed,       /**< The requested operation completed normally */
    Cancelled,       /**< The caller requested cancellation */
    StepLimitReached /**< The configured simulation step limit was reached */
  };

  /** @brief Logical edge observed for one wire during a reactive evaluation. */
  enum class EdgeType {
    RISE,     /**< LOW -> HIGH */
    FALL,     /**< HIGH -> LOW */
    UNKNOWN,  /**< A changed transition involving UNKNOWN or ERROR state */
    NO_CHANGE /**< No captured change for this evaluation */
  };

  /**
   * @brief Classifies one wire's transition in a simulation context.
   *
   * @param context Reactive evaluation context with previous wire states
   * @param wire Wire to inspect
   * @return RISE, FALL, UNKNOWN, or NO_CHANGE for this evaluation
   */
  [[nodiscard]] static EdgeType edgeType(const Context& context, const Wire_ptr& wire);

  /**
   * @brief Constructs a Simulator for the given circuit.
   * @param c The circuit to simulate
   * @param initialSimulationTime If != 0 also simulate the circuit using
   * initialSimulationTime as duration
   * @param isInteractive If true make the circuit interactive
   * @param fstWriter Optional waveform writer used to trace simulation snapshots
   * @param isCancelled Optional cancellation callback used during initial evaluation
   * @throws std::invalid_argument If c is null
   */
  explicit Simulator(std::shared_ptr<Circuit> c, uint64_t initialSimulationTime = 0,
                     bool                           isInteractive = false,
                     std::unique_ptr<CircuitWriter> fstWriter     = nullptr,
                     CancellationCheck              isCancelled   = {});
  /**
   * @brief Destructor - removes topology listener
   */
  ~Simulator();

  /**
   * @brief Executes the simulation for a specified duration.
   *
   * Uses an event-driven simulation paradigm with a priority queue sorted by time.
   * Processes events in chronological order, re-evaluating blocks when wire states
   * change.
   *
   * @param duration The simulation duration in time units
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion, cancellation, or step-limit outcome
   */
  RunResult run(uint64_t duration, CancellationCheck isCancelled = {});

  /**
   * @brief Runs pending timed events until no further propagation is scheduled.
   *
   * Unlike run(duration), this is not bounded by a caller-provided time window. It is
   * intended for interactive settling after direct input changes, where the full
   * propagation delay depends on the circuit. Unstable circuits are still bounded by
   * maxSimulationSteps.
   *
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion, cancellation, or step-limit outcome
   */
  RunResult runUntilIdle(CancellationCheck isCancelled = {});

  /**
   * @brief Drives input buses from a sampled internal waveform and runs to duration.
   *
   * Snapshot values are matched to input drivers by index. Missing values and extra
   * values are ignored, mirroring the viewer's input-only waveform behavior.
   *
   * @param duration Total simulation duration in time units
   * @param inputSnapshots Timestamped input values ordered like inputDrivers
   * @param inputDrivers Ordered buses to drive from each snapshot
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion, cancellation, or step-limit outcome
   */
  RunResult simulateWaveform(uint64_t duration, std::span<const Sample> inputSnapshots,
                             std::span<const WaveformInputDriver> inputDrivers,
                             CancellationCheck                    isCancelled = {});

  /**
   * @brief Rebuilds the cached execution plan after topology changes.
   *
   * Topological sorting, cyclic grouping, and forward-cone reachability are compiled
   * once here instead of during every event batch.
   */
  void recompile();

  /**
   * @brief Enables FST tracing for this simulator.
   *
   * The writer is initialized from the simulator's circuit and immediately receives a
   * snapshot for the current simulation time.
   */
  void enableFstTracing(std::string_view fileName, CircuitWriter::Options options = {});

  /**
   * @brief Installs a caller-created FST writer.
   *
   * Ownership transfers to the simulator. Passing nullptr disables tracing.
   */
  void setFstWriter(std::unique_ptr<CircuitWriter> writer);

  /**
   * @brief Configures bus-level waveform snapshots emitted by the simulator.
   *
   * The same registered buses are used for UI waveform snapshots and optional FST
   * export, ensuring both observe exactly the same simulation timing.
   */
  void setTraceBuses(std::vector<CircuitWriter::NamedBus> buses);
  void setTraceSink(TraceSink sink);

  /**
   * @brief Forces a bus to a specific value and propagates the change through the
   * circuit.
   *
   * Optimized for pass-by-value buses (copies the bus).
   * Evaluates the cached forward execution steps affected by the bus.
   *
   * @param bus The bus to set
   * @param value The value to set
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion or cancellation outcome
   */
  RunResult setBus(Bus bus, unsigned int value, CancellationCheck isCancelled = {});

  /**
   * @brief Forces a bus to a specific value with a source component and propagates
   * the change through the circuit.
   *
   * Tracks the source component that authorized this state change for proper
   * event propagation. Evaluates the cached forward execution steps affected by the
   * bus.
   *
   * @param bus The bus to set
   * @param value The value to set
   * @param source The component that authorized this change
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion or cancellation outcome
   */
  RunResult setBus(Bus bus, unsigned int value, const Component_weakPtr& source,
                   CancellationCheck isCancelled = {});

  /**
   * @brief Propagates changes from a bus through the circuit in reverse direction.
   *
   * Used when the bus value has changed externally and needs to update upstream
   * components. Extracts backwards subgraph and evaluates each block.
   *
   * @param bus The bus that changed
   * @param isCancelled Optional cooperative cancellation callback
   * @return Completion or cancellation outcome
   */
  RunResult simulateBus(const Bus& bus, CancellationCheck isCancelled = {});

  /**
   * @brief Updates a wire with a new state and optional delay.
   *
   * @param target The wire to update
   * @param newState The new state
   * @param delay Delay in time units (0 for immediate)
   * @param source The component that is the source of this change
   */
  void updateWire(const Wire_ptr& target, State newState, uint64_t delay,
                  const Component_weakPtr& source);

  /**
   * @brief Gets the current simulation time.
   * @return Current time in simulation units
   */
  [[nodiscard]] uint64_t getCurrentTime() const { return currentTime; }

  static void setMaxSimulationSteps(uint64_t value);
  static void setMaxTransitionsPerDeltaCycle(int value);

  [[nodiscard]] static uint64_t getMaxSimulationSteps();
  [[nodiscard]] static int      getMaxTransitionsPerDeltaCycle();

private:
  using PendingTransitionKey = std::pair<uint64_t, std::uintptr_t>;

  struct PendingTransitionKeyHash {
    [[nodiscard]] std::size_t operator()(const PendingTransitionKey& key) const noexcept;
  };

  struct PendingTransition {
    uint64_t time  = 0;
    State    state = State::ERROR;
  };

  struct StagedSequentialTransition {
    Wire_ptr          target;
    State             state = State::ERROR;
    Component_weakPtr source;
  };

  struct ExecutionStep {
    bool                           isCyclic = false;
    std::vector<Component_weakPtr> components;
  };

  class EvaluationStateGuard;

  /** @brief The circuit being simulated */
  std::shared_ptr<Circuit> circuit;

  /** @brief Listener ID used to unregister from circuit topology notifications */
  uint64_t topologyListenerId = 0;

  /** @brief Compiled component/SCC execution plan in topological order */
  std::vector<ExecutionStep> executionPlan;

  /** @brief Cached downstream execution step indices for each input wire ID */
  std::unordered_map<uint64_t, std::vector<std::size_t>> forwardExecutionStepsByWire;

  /** @brief Priority queue of timed events sorted by time */
  std::priority_queue<TimedEvent, std::vector<TimedEvent>, std::greater<>> eventQueue;

  /** @brief Latest valid delayed transition for each component-output wire */
  std::unordered_map<PendingTransitionKey, PendingTransition, PendingTransitionKeyHash>
      pendingTransitions;

  /** @brief Zero-delay sequential writes waiting for the active pass to finish */
  std::unordered_map<PendingTransitionKey, StagedSequentialTransition,
                     PendingTransitionKeyHash>
      stagedSequentialTransitions;

  /** @brief Current simulation time */
  uint64_t currentTime = 0;

  /** @brief Optional waveform writer for simulation tracing */
  std::unique_ptr<CircuitWriter> fstWriter;

  /** @brief Bus list observed by waveform tracing */
  std::vector<CircuitWriter::NamedBus> traceBuses;

  /** @brief Optional callback for live waveform viewers */
  TraceSink traceSink;

  /** @brief Previous-state index being populated during the active reactive pass. */
  std::unordered_map<uint64_t, State>* activePreviousWireStates = nullptr;

  /** @brief True while reactive evaluation should stage opted-in zero-delay writes. */
  bool stageSequentialOutputs = false;

  static uint64_t maxSimulationSteps;
  static int      maxTransitionsPerDeltaCycle;

  bool cyclicStateChanged = false;

  /**
   * @brief Emits a waveform snapshot when tracing is enabled.
   */
  void emitTraceSnapshot();

  [[nodiscard]] static std::string encodeTraceBusValue(const Bus& bus);

  [[nodiscard]] static PendingTransitionKey
  pendingTransitionKey(const Wire_ptr& target, const Component_weakPtr& source);

  void scheduleDelayedWireUpdate(const Wire_ptr& target, State newState, uint64_t delay,
                                 const Component_weakPtr& source);

  [[nodiscard]] bool shouldStageSequentialOutput(const Component_weakPtr& source) const;

  void stageSequentialWireUpdate(const Wire_ptr& target, State newState,
                                 const Component_weakPtr& source);

  [[nodiscard]] std::vector<Bus> commitStagedSequentialTransitions(
      std::unordered_map<uint64_t, State>& previousWireStates);

  [[nodiscard]] RunResult
  evaluateForwardConeAndTrace(std::span<const Bus>                changedBuses,
                              std::unordered_map<uint64_t, State> previousWireStates,
                              const CancellationCheck&            isCancelled = {},
                              bool enableSequentialStaging                    = true);

  [[nodiscard]] bool isDelayedEventPending(const TimedEvent& event) const;

  [[nodiscard]] static std::vector<ExecutionStep>
  compileExecutionPlan(std::span<const Circuit::SimulationBlock> blocks);

  [[nodiscard]] bool evaluateExecutionStep(const ExecutionStep&     step,
                                           const Context&           context,
                                           const CancellationCheck& isCancelled = {});

  [[nodiscard]] bool evaluateExecutionPlan(std::span<const ExecutionStep> steps,
                                           const Context&                 context,
                                           const CancellationCheck& isCancelled = {});

  [[nodiscard]] bool
  evaluateExecutionStepIndices(std::span<const std::size_t> stepIndices,
                               const Context&               context,
                               const CancellationCheck&     isCancelled = {});

  [[nodiscard]] RunResult
  evaluateExecutionStepIndicesAndTrace(std::span<const std::size_t> stepIndices,
                                       const Context&               context,
                                       const CancellationCheck&     isCancelled = {});

  [[nodiscard]] std::vector<std::size_t>
  getForwardExecutionSteps(std::span<const Bus> changedBuses) const;

  [[nodiscard]] RunResult
  applyWaveformInputSample(std::span<const std::string>         values,
                           std::span<const WaveformInputDriver> inputDrivers,
                           const CancellationCheck&             isCancelled);

  [[nodiscard]] RunResult processNextEventBatch(const CancellationCheck& isCancelled);

  /**
   * @brief Evaluates a plan and emits a trace snapshot on success.
   */
  [[nodiscard]] RunResult
  evaluateExecutionPlanAndTrace(std::span<const ExecutionStep> steps,
                                const Context&                 context,
                                const CancellationCheck&       isCancelled = {});
};

}  // namespace SILICON::simulation
