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

#include <core/elaboration.hpp>
#include <core/simulator.hpp>

namespace SILICON::simulation {

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

/**
 * @brief Owns the source design elaboration and its runtime Simulator.
 *
 * Simulator remains concerned only with executing a runtime circuit. This session
 * handles the higher-level simulation lifecycle: elaborate source modules, construct
 * the runtime simulator, and rebuild when the source topology changes.
 *
 * @details Session is the runtime workflow facade:
 *
 * 1. The caller provides the source Circuit, which may contain source-level module
 *    placeholders such as subcircuits.
 * 2. The session creates a CircuitElaborator using the active component registry.
 * 3. @ref rebuild() elaborates the source circuit into a runtime circuit and creates a
 *    Simulator for that runtime circuit only.
 * 4. The UI or workflow calls @ref rebuild() explicitly after source topology changes.
 * 5. Trace bus/sink configuration is stored on the session and reapplied whenever the
 *    runtime simulator is rebuilt.
 *
 * Saved circuit JSON and UI component state remain source-level data; the runtime
 * circuit is an implementation detail of the active simulation session.
 */
class Session {
public:
  /**
   * @brief Creates a simulation session for a source circuit.
   *
   * @param sourceCircuit Source/editable circuit to simulate.
   * @param isCancelled Optional cancellation callback used during initial runtime
   * construction.
   * @throws std::invalid_argument if @p sourceCircuit is null.
   */
  explicit Session(std::shared_ptr<Circuit>     sourceCircuit,
                   Simulator::CancellationCheck isCancelled = {});

  ~Session() = default;

  Session(const Session&)            = delete;
  Session& operator=(const Session&) = delete;

  /**
   * @brief Rebuilds the runtime circuit and runtime simulator from the source circuit.
   *
   * @details This discards pending events and simulation time, because a topology change
   * invalidates the previous runtime circuit. Stored trace configuration is immediately
   * reapplied to the new runtime simulator.
   */
  void rebuild(Simulator::CancellationCheck isCancelled = {});

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
   * @brief Stores and applies the named buses emitted by trace snapshots.
   *
   * The stored configuration is reapplied after runtime simulator rebuilds.
   */
  void setTraceBuses(std::vector<CircuitWriter::NamedBus> buses);

  /**
   * @brief Stores and applies the callback that receives encoded trace snapshots.
   *
   * The stored callback is reapplied after runtime simulator rebuilds.
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
  std::shared_ptr<Circuit>   sourceCircuit;
  std::shared_ptr<Circuit>   runtime;
  std::unique_ptr<Simulator> runtimeSimulator;
  CircuitElaborator          elaborator;

  std::vector<CircuitWriter::NamedBus> traceBuses;
  Simulator::TraceSink                 traceSink;
};

}  // namespace SILICON::simulation
