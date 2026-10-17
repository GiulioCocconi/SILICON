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

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <core/simulator.hpp>

namespace SILICON::simulation {

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

/**
 * @brief Elaborates a source design once and owns its runtime Simulator.
 *
 * Source components are cloned and subcircuits are expanded before simulation.
 * Root wires remain shared so UI handles can drive inputs and observe outputs.
 * The runtime topology is fixed; create a new session after design changes.
 * Saved circuit JSON and UI component state remain source-level data.
 */
class Session {
public:
  /**
   * @brief Creates a simulation session for a source circuit.
   *
   * @param sourceCircuit Source/editable circuit to simulate.
   * @param isCancelled Optional cancellation callback used during initial runtime
   * construction.
   * @param resolver Optional resolver used only during construction.
   * @throws std::invalid_argument if @p sourceCircuit is null.
   */
  explicit Session(std::shared_ptr<Circuit>              sourceCircuit,
                   Simulator::CancellationCheck          isCancelled = {},
                   const SILICON::core::CircuitResolver* resolver    = nullptr);

  ~Session() = default;

  Session(const Session&)            = delete;
  Session& operator=(const Session&) = delete;

  /**
   * @brief Runs the active simulation for a bounded duration.
   */
  Simulator::RunResult run(uint64_t                     duration,
                           Simulator::CancellationCheck isCancelled = {});

  /**
   * @brief Runs pending simulation events until the runtime circuit is idle.
   */
  Simulator::RunResult runUntilIdle(Simulator::CancellationCheck isCancelled = {});

  /**
   * @brief Drives input buses from waveform samples and simulates the runtime circuit.
   */
  Simulator::RunResult
  simulateWaveform(uint64_t duration, std::span<const Sample> inputSnapshots,
                   std::span<const Simulator::WaveformInputDriver> inputDrivers,
                   Simulator::CancellationCheck                    isCancelled = {});

  /**
   * @brief Forces a bus value and propagates the change through the runtime circuit.
   */
  Simulator::RunResult setBus(Bus bus, const BusValue& value,
                              Simulator::CancellationCheck isCancelled = {});

  /**
   * @brief Forces a bus value with an explicit source component authorization.
   */
  Simulator::RunResult setBus(Bus bus, const BusValue& value,
                              const Component_weakPtr&     source,
                              Simulator::CancellationCheck isCancelled = {});

  /**
   * @brief Configures the named buses emitted by trace snapshots.
   */
  void setTraceBuses(std::vector<CircuitWriter::NamedBus> buses);

  /**
   * @brief Configures the callback that receives encoded trace snapshots.
   */
  void setTraceSink(Simulator::TraceSink sink);

  /**
   * @brief Installs or clears the active FST writer on the current runtime simulator.
   */
  void setFstWriter(std::unique_ptr<CircuitWriter> writer);

  /**
   * @brief Creates and installs an FST writer for the current runtime simulator.
   */
  void enableFstTracing(std::string_view fileName, CircuitWriter::Options options = {});

  /**
   * @brief Returns the current runtime simulator time.
   */
  [[nodiscard]] uint64_t getCurrentTime() const;

private:
  std::unique_ptr<Simulator> runtimeSimulator;
};

}  // namespace SILICON::simulation
