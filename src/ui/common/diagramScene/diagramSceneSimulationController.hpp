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
#include <optional>
#include <string>
#include <vector>

#include <QStringList>

#include <core/component.hpp>
#include <core/siliconFst.hpp>

class Bus;
class DiagramScene;
class Simulator;

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
   */
  void enterSimulationMode();

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
   * @brief Returns whether FST tracing is currently enabled.
   */
  [[nodiscard]] bool isFstTracingEnabled() const { return fstTraceFile.has_value(); }

private:
  struct TraceConfiguration {
    std::vector<SiliconFstWriter::NamedBus> buses;
    int                                     inputCount = 0;
  };

  /**
   * @brief Refreshes waveform, simulator, and FST tracing from one scene snapshot.
   */
  void refreshTraceConfiguration();

  /**
   * @brief Collects ordered trace buses and their input count.
   */
  [[nodiscard]] TraceConfiguration collectTraceConfiguration() const;

  /** @brief Host scene that owns the graphical items and Qt signals */
  DiagramScene& scene;

  /** @brief Runtime simulator active only during simulation mode */
  std::unique_ptr<Simulator> simulator;

  /** @brief Optional output file used for live FST tracing */
  std::optional<std::string> fstTraceFile;
};
