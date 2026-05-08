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
#include <core/siliconFst.hpp>
#include <functional>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

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
 * @note The Simulator automatically recompiles when the circuit topology changes (if
 * constructed with `isInteractive`) .
 */
class Simulator {
public:
  using TraceSink = std::function<void(uint64_t, const std::vector<std::string>&)>;

  /**
   * @brief Constructs a Simulator for the given circuit.
   * @param c The circuit to simulate
   * @param initialSimulationTime If != 0 also simulate the circuit using
   * initialSimulationTime as duration
   * @param isInteractive If true make the circuit interactive
   * @param fstWriter Optional waveform writer used to trace simulation snapshots
   * @throws std::invalid_argument If c is null
   */
  explicit Simulator(std::shared_ptr<Circuit> c, uint64_t initialSimulationTime = 0,
                     bool                              isInteractive = false,
                     std::unique_ptr<SiliconFstWriter> fstWriter     = nullptr);
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
   */
  void run(uint64_t duration);

  /**
   * @brief Recompiles the execution blocks from the circuit topology.
   */
  void recompile();

  /**
   * @brief Enables FST tracing for this simulator.
   *
   * The writer is initialized from the simulator's circuit and immediately receives a
   * snapshot for the current simulation time.
   */
  void enableFstTracing(const std::string&        fileName,
                        SiliconFstWriter::Options options = {});

  /**
   * @brief Installs a caller-created FST writer.
   *
   * Ownership transfers to the simulator. Passing nullptr disables tracing.
   */
  void setFstWriter(std::unique_ptr<SiliconFstWriter> writer);

  /**
   * @brief Configures bus-level waveform snapshots emitted by the simulator.
   *
   * The same registered buses are used for UI waveform snapshots and optional FST
   * export, ensuring both observe exactly the same simulation timing.
   */
  void setTraceBuses(std::vector<SiliconFstWriter::NamedBus> buses);
  void setTraceSink(TraceSink sink);

  /**
   * @brief Forces a bus to a specific value and propagates the change through the
   * circuit.
   *
   * Optimized for pass-by-value buses (copies the bus).
   * Extracts forward subgraph and evaluates each block.
   *
   * @param bus The bus to set
   * @param value The value to set
   */
  /**
   * @brief Forces a bus to a specific value and propagates the change through the
   * circuit.
   *
   * Optimized for pass-by-value buses (copies the bus).
   * Extracts forward subgraph and evaluates each block.
   *
   * @param bus The bus to set
   * @param value The value to set
   */
  void setBus(Bus bus, unsigned int value);

  /**
   * @brief Forces a bus to a specific value with a source component and propagates
   * the change through the circuit.
   *
   * Tracks the source component that authorized this state change for proper
   * event propagation. Extracts forward subgraph and evaluates each block.
   *
   * @param bus The bus to set
   * @param value The value to set
   * @param source The component that authorized this change
   */
  void setBus(Bus bus, unsigned int value, const Component_weakPtr& source);

  /**
   * @brief Propagates changes from a bus through the circuit in reverse direction.
   *
   * Used when the bus value has changed externally and needs to update upstream
   * components. Extracts backwards subgraph and evaluates each block.
   *
   * @param bus The bus that changed
   */
  void simulateBus(const Bus& bus);

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

private:
  /** @brief The circuit being simulated */
  std::shared_ptr<Circuit> circuit;

  /** @brief Pre-compiled execution blocks split by cyclic/acyclic parts */
  std::vector<Circuit::SimulationBlock> executionBlocks;

  /** @brief Priority queue of timed events sorted by time */
  std::priority_queue<TimedEvent, std::vector<TimedEvent>, std::greater<>> eventQueue;

  /** @brief ID of the topology change listener registered with the circuit */
  uint64_t topologyListenerId = 0;

  /** @brief Current simulation time */
  uint64_t currentTime = 0;

  /** @brief Optional waveform writer for simulation tracing */
  std::unique_ptr<SiliconFstWriter> fstWriter;

  /** @brief Bus list observed by waveform tracing */
  std::vector<SiliconFstWriter::NamedBus> traceBuses;

  /** @brief Optional callback for live waveform viewers */
  TraceSink traceSink;

  /** @brief Maximum number of delta cycles for convergence */
  static constexpr int MAX_DELTA = 1000;

  bool cyclicStateChanged = false;

  /**
   * @brief Emits a waveform snapshot when tracing is enabled.
   */
  void emitTraceSnapshot();

  [[nodiscard]] static std::string encodeTraceBusValue(const Bus& bus);

  /**
   * @brief Executes all components in a simulation block.
   * @param block The simulation block to evaluate
   */
  void evaluateBlock(const Circuit::SimulationBlock& block);
};
