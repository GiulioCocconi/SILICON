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
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

/**
 * @brief Represents a timed event in the simulation.
 */
struct TimedEvent {
  uint64_t          time;
  Wire_ptr          targetWire;
  State             newState;
  Component_weakPtr source;

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
 * @note The Simulator automatically recompiles when the circuit topology changes.
 */
class Simulator {
public:
  /**
   * @brief Constructs a Simulator for the given circuit.
   * @param c The circuit to simulate
   * @throws std::invalid_argument if c is null
   */
  explicit Simulator(std::shared_ptr<Circuit> c);

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
  std::shared_ptr<Circuit>              circuit;
  std::vector<Circuit::SimulationBlock> executionBlocks;
  std::priority_queue<TimedEvent, std::vector<TimedEvent>, std::greater<>> eventQueue;

  uint64_t             topologyListenerId = 0;
  uint64_t             currentTime        = 0;
  static constexpr int MAX_DELTA          = 1000;

  bool cyclicStateChanged = false;

  /**
   * @brief Executes all components in a simulation block.
   * @param block The simulation block to evaluate
   */
  void evaluateBlock(const Circuit::SimulationBlock& block);
};
