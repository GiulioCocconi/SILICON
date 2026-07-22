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

#ifndef __EMSCRIPTEN__
  #include <thread>
#endif

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QList>
#include <QPair>
#include <QStringList>

#include <core/component.hpp>
#include <core/siliconFst.hpp>
#include <core/siliconWaveform.hpp>
#include <core/simulationSession.hpp>

class Bus;
class DiagramScene;
class QProgressDialog;
class QTimer;

/**
 * @class DiagramSceneSimulationController
 * @brief Manages simulation lifecycle and waveform tracing for DiagramScene.
 *
 * This helper owns the temporary runtime-only simulation state used by the editor,
 * including the Simulator instance and optional FST trace output. It keeps
 * DiagramScene focused on interaction and scene management while centralizing
 * simulation entry/exit, live input toggles, output refreshes, and waveform updates.
 */
class DiagramSceneSimulationController {
public:
  /**
   * @brief Constructs a simulation controller bound to a scene.
   * @param scene Host scene that provides items, signals, and repaint hooks
   */
  explicit DiagramSceneSimulationController(DiagramScene& scene);

  /**
   * @brief Destroys the controller and releases any simulator resources.
   */
  ~DiagramSceneSimulationController();

  /**
   * @brief Enters interactive simulation mode for the scene.
   *
   * Recalculates wire-to-component assignments before creating runtime simulation
   * state. If that recalculation fails while assigning an input, startup is aborted
   * and the caller should leave the scene in normal mode.
   *
   * @return True when simulation startup succeeds.
   */
  bool enterSimulationMode();

  /**
   * @brief Leaves interactive simulation mode and clears runtime state.
   */
  void exitSimulationMode();

  /**
   * @brief Applies a user input toggle to the running simulator.
   * @param targetBus Logic bus driven by the input widget
   * @param value New value to inject
   * @param source Component that originated the change
   */
  void handleInputToggled(Bus targetBus, unsigned int value, Component_weakPtr source);

  /**
   * @brief Enables or disables FST tracing for the active simulation session.
   * @param fileName Destination file or std::nullopt to disable tracing
   */
  void setFstTraceFile(std::optional<std::string> fileName);

  /**
   * @brief Refreshes the visual state of all graphical outputs from bus values.
   */
  void refreshGraphicalOutputs();

  /**
   * @brief Reacts to wire topology changes while simulation mode is active.
   */
  void handleTopologyChanged();

  /**
   * @brief Clears waveform state after the scene has been fully emptied.
   */
  void clearWaveformTrace();

  /**
   * @brief Runs the circuit from an edited input waveform and publishes output trace.
   * @param duration Total waveform duration to simulate
   * @param inputSnapshots Input-only timestamped values ordered like waveform inputs
   */
  void simulateEditedWaveform(qulonglong                         duration,
                              std::vector<SiliconWaveformSample> inputSnapshots);

  /**
   * @brief Returns whether FST tracing is currently enabled.
   */
  [[nodiscard]] bool isFstTracingEnabled() const { return fstTraceFile.has_value(); }

private:
  /** @brief Ordered buses and group metadata used by waveform tracing. */
  struct TraceConfiguration {
    std::vector<SiliconFstWriter::NamedBus> buses;          /**< Buses to sample */
    QList<int>                              widths;         /**< Bus widths */
    int                                     inputCount = 0; /**< Leading input buses */
  };

  /**
   * @brief Refreshes waveform, simulator, and FST tracing from one scene snapshot.
   */
  void refreshTraceConfiguration();

  /**
   * @brief Collects ordered trace buses and their input count.
   */
  [[nodiscard]] TraceConfiguration collectTraceConfiguration() const;

  /**
   * @brief Starts one cancellable simulation operation and its progress UI.
   * @param job Operation that checks jobCancellationRequested cooperatively
   */
  void startJob(std::function<Simulator::RunResult()> job);

  /**
   * @brief Configures progress dialog, timers, and locks the views before a job.
   */
  void setupJobUI();

  /** @brief Finalizes a completed job on the GUI thread. */
  void finishJob();

  /** @brief Requests cancellation and waits for the active job before teardown. */
  void cancelAndWait();

  /** @brief Returns whether the active job has completed. */
  [[nodiscard]] bool isJobFinished() const;

  /** @brief Publishes whether the active job has completed. */
  void setJobFinished(bool finished);

  /** @brief Returns whether cancellation has been requested for the active job. */
  [[nodiscard]] bool isJobCancellationRequested() const;

  /** @brief Publishes a cancellation request for the active job. */
  void requestJobCancellation();

  /** @brief Clears cancellation state before starting a job. */
  void resetJobCancellation();

  /** @brief Rebuilds the waveform viewer's signal-name configuration. */
  void resetWaveformTrace(const TraceConfiguration& trace);

  /**
   * @brief Applies trace buses, snapshot collection, and optional FST output.
   * @param trace Trace buses collected from the scene
   * @param traceFile Optional destination for FST output
   */
  void configureSimulatorTrace(const TraceConfiguration&         trace,
                               const std::optional<std::string>& traceFile);

  /** @brief Host scene that owns the graphical items and Qt signals */
  DiagramScene& scene;

  /** @brief Runtime simulation session active only during simulation mode */
  std::unique_ptr<silicon::simulation::SimulationSession> simulator;

  /** @brief Optional output file used for live FST tracing */
  std::optional<std::string> fstTraceFile;

#ifndef __EMSCRIPTEN__
  /** @brief Native worker synchronization */
  std::jthread worker;
#endif

  /** @brief Platform-agnostic thread-safe job state */
  std::atomic_bool jobCancellationRequested = false;
  std::atomic_bool jobFinished              = true;

  /** @brief Result produced by the latest simulation operation */
  Simulator::RunResult jobResult = Simulator::RunResult::Completed;

  /** @brief Exception captured during a job and reported on the GUI thread */
  std::exception_ptr jobException;

  /** @brief Delayed cancellation dialog for long-running jobs */
  QProgressDialog* progressDialog = nullptr;

  /** @brief GUI timer that detects job completion without blocking the event loop */
  QTimer* completionTimer = nullptr;

  /** @brief GUI timer that prevents the progress dialog flashing for short jobs */
  QTimer* dialogTimer = nullptr;

  /** @brief Worker-produced snapshots delivered to the viewer as one GUI batch */
  QList<QPair<qulonglong, QStringList>> pendingWaveformSnapshots;
};
