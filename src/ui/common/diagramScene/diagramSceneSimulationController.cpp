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

#include "diagramSceneSimulationController.hpp"

#include <algorithm>
#include <format>
#include <string>

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QObject>
#include <QProgressDialog>
#include <QTimer>
#include <QWidget>

#include <utils/ranges_wrapper.hpp>

#include <core/circuit.hpp>
#include <core/simulationSession.hpp>
#include <core/simulator.hpp>
#include <logging/logger.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::simulation;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

namespace {

constexpr ItemCategory InputCategory  = ItemCategory::IO | ItemCategory::Input;
constexpr ItemCategory OutputCategory = ItemCategory::IO | ItemCategory::Output;
const SILICON::logging::Logger           simulationUiLog("simulation-ui");

std::string componentNameOr(const Component_ptr& component, const std::string& fallback)
{
  if (!component)
    return fallback;

  const auto property = component->getProperty("name");
  if (!property)
    return fallback;

  if (const auto* name = std::get_if<std::string>(&*property); name && !name->empty())
    return *name;

  return fallback;
}

Simulator::RunResult
settleInteractiveSimulation(SILICON::simulation::Session& simulator,
                            const Simulator::CancellationCheck&     isCancelled)
{
  const auto result = simulator.runUntilIdle(isCancelled);
  if (result != Simulator::RunResult::Completed)
    return result;

  // Advance one display tick after settling so successive interactive changes do not
  // collapse into one same-timestamp waveform sample.
  return simulator.run(1, isCancelled);
}

}  // namespace

DiagramSceneSimulationController::DiagramSceneSimulationController(DiagramScene& scene)
  : scene(scene)
{
}

DiagramSceneSimulationController::~DiagramSceneSimulationController()
{
  cancelAndWait();
}

bool DiagramSceneSimulationController::enterSimulationMode()
{
  scene.clearInputAssignmentErrors();

  if (!scene.calculateWiresForComponents())
    return false;

  // Initialize all wires to UNKNOWN state (will be overridden by connected inputs)
  for (const auto& wire : scene.getWireManager().wires())
    wire->clearBusState();

  std::vector<GraphicalIO*> inputs;
  std::vector<GraphicalIO*> outputs;
  Component_set             coreComps;

  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalIO>(item, InputCategory))
      inputs.push_back(input);
    else if (auto* output = category_cast<GraphicalIO>(item, OutputCategory))
      outputs.push_back(output);

    if (const auto* component =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        component && component->getComponent()) {
      coreComps.insert(component->getComponent());
    }
  }

  // Reset visible outputs first, then inject input start values. Doing this in two
  // phases prevents output reset from overwriting a shared bus after an input starts it.
  for (auto* output : outputs)
    output->resetSimulationState();

  // Do this BEFORE creating the Simulator to avoid double-evaluations.
  for (auto* input : inputs) {
    input->applyStartValue();
    QObject::connect(input, &GraphicalIO::inputToggled, &scene,
                     &DiagramScene::handleInputToggled, Qt::UniqueConnection);
  }

  // Initialize Circuit & Simulator frameworks.
  scene.setCircuit(std::make_shared<Circuit>(coreComps, false));

  auto trace = collectTraceConfiguration();
  resetTrace(trace);
  const auto circuit   = scene.getCircuit();
  const auto traceFile = fstTraceFile;

  startJob([this, circuit, trace = std::move(trace), traceFile]() mutable {
    const auto isCancelled = [this]() {
#ifdef __EMSCRIPTEN__
      // The web build has no worker thread, so periodically service the dialog and
      // cancellation button while simulation runs cooperatively on the GUI thread.
      QApplication::processEvents();
#endif
      return isJobCancellationRequested();
    };

    simulator =
        std::make_unique<SILICON::simulation::Session>(circuit, isCancelled);
    configureSimulatorTrace(trace, traceFile);
    return settleInteractiveSimulation(*simulator, isCancelled);
  });

  return true;
}

void DiagramSceneSimulationController::exitSimulationMode()
{
  cancelAndWait();

  for (auto* item : scene.items()) {
    if (auto* io = category_cast<GraphicalIO>(item, ItemCategory::IO)) {
      QObject::disconnect(io, &GraphicalIO::inputToggled, &scene,
                          &DiagramScene::handleInputToggled);
      io->resetSimulationState();
    }
  }

  simulator.reset();
  scene.setCircuit(nullptr);

  // Clear bus states to reset visual wire colors
  for (const auto& wire : scene.getWireManager().wires())
    wire->clearBusState();

  scene.update();
}

void DiagramSceneSimulationController::handleInputToggled(Bus               targetBus,
                                                          unsigned int      value,
                                                          Component_weakPtr source)
{
  if (!simulator || !isJobFinished())
    return;

  startJob([this, targetBus = std::move(targetBus), value,
            source = std::move(source)]() mutable {
    const auto isCancelled = [this]() {
#ifdef __EMSCRIPTEN__
      // Keep the synchronous WebAssembly fallback responsive at cancellation points.
      QApplication::processEvents();
#endif
      return isJobCancellationRequested();
    };

    auto result = simulator->setBus(std::move(targetBus), value, source, isCancelled);
    if (result != Simulator::RunResult::Completed)
      return result;
    return settleInteractiveSimulation(*simulator, isCancelled);
  });
}

void DiagramSceneSimulationController::setFstTraceFile(
    std::optional<std::string> fileName)
{
  if (!isJobFinished())
    return;

  fstTraceFile = std::move(fileName);

  if (!simulator || !isJobFinished())
    return;

  refreshTraceConfiguration();
}

void DiagramSceneSimulationController::refreshGraphicalOutputs()
{
  for (auto* item : scene.items()) {
    auto* output = category_cast<GraphicalIO>(item, OutputCategory);
    if (!output)
      continue;

    if (auto* out = dynamic_cast<GraphicalOutputSingle*>(output)) {
      if (!out->getComponent())
        continue;

      auto bus = out->getComponent()->getInputs()[0];

      State s = State::UNKNOWN;
      if (bus.isInErrorState())
        s = State::ERROR;
      else if (!bus.hasUnknowns())
        s = (bus.getCurrentValue() > 0) ? State::HIGH : State::LOW;

      out->setState(s);
    } else if (auto* busOut = dynamic_cast<GraphicalBusOutput*>(output)) {
      if (!busOut->getComponent() || busOut->getComponent()->getInputs().empty())
        continue;

      busOut->setBusState(busOut->getComponent()->getInputs()[0]);
    }
  }
}

void DiagramSceneSimulationController::handleTopologyChanged()
{
  if (scene.getInteractionMode() != InteractionMode::SIMULATION_MODE)
    return;
  if (!isJobFinished())
    return;

  if (!scene.calculateWiresForComponents()) {
    scene.setInteractionMode(InteractionMode::NORMAL_MODE);
    return;
  }
  if (simulator)
    simulator->rebuild();
  refreshGraphicalOutputs();
  refreshTraceConfiguration();
  scene.update();
}

void DiagramSceneSimulationController::clearWaveformTrace()
{
  scene.waveformTraceReset({}, 0, {});
}

void DiagramSceneSimulationController::simulateEditedWaveform(
    const qulonglong duration, std::vector<Sample> inputSnapshots)
{
  if (!isJobFinished())
    return;

  scene.clearInputAssignmentErrors();
  if (!scene.calculateWiresForComponents())
    return;

  for (const auto& wire : scene.getWireManager().wires())
    wire->clearBusState();

  std::vector<GraphicalIO*> inputs;
  std::vector<GraphicalIO*> outputs;
  Component_set             coreComps;

  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalIO>(item, InputCategory))
      inputs.push_back(input);
    else if (auto* output = category_cast<GraphicalIO>(item, OutputCategory))
      outputs.push_back(output);

    if (const auto* component =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        component && component->getComponent()) {
      coreComps.insert(component->getComponent());
    }
  }

  const auto byPosition = [](const auto* a, const auto* b) {
    if (a->scenePos().y() != b->scenePos().y())
      return a->scenePos().y() < b->scenePos().y();
    return a->scenePos().x() < b->scenePos().x();
  };

  std::ranges::sort(inputs, byPosition);
  std::ranges::sort(outputs, byPosition);

  for (auto* output : outputs)
    output->resetSimulationState();

  std::vector<Simulator::WaveformInputDriver> inputDrivers;
  inputDrivers.reserve(inputs.size());
  for (auto* input : inputs) {
    const auto component = input->getComponent();
    if (!component || component->getOutputs().empty())
      continue;
    inputDrivers.push_back({component->getOutputs()[0], component->weak_from_this()});
  }

  scene.setCircuit(std::make_shared<Circuit>(coreComps, false));
  auto trace = collectTraceConfiguration();
  resetTrace(trace);
  const auto circuit   = scene.getCircuit();
  const auto traceFile = fstTraceFile;

  startJob([this, circuit, trace = std::move(trace), traceFile,
            inputDrivers   = std::move(inputDrivers), duration,
            inputSnapshots = std::move(inputSnapshots)]() mutable {
    const auto isCancelled = [this]() {
#ifdef __EMSCRIPTEN__
      QApplication::processEvents();
#endif
      return isJobCancellationRequested();
    };

    simulator =
        std::make_unique<SILICON::simulation::Session>(circuit, isCancelled);
    configureSimulatorTrace(trace, traceFile);

    return simulator->simulateWaveform(duration, inputSnapshots, inputDrivers,
                                       isCancelled);
  });
}

void DiagramSceneSimulationController::refreshTraceConfiguration()
{
  if (!simulator)
    return;

  auto trace = collectTraceConfiguration();
  resetTrace(trace);
  configureSimulatorTrace(trace, fstTraceFile);
  if (!pendingWaveformSnapshots.isEmpty()) {
    // Trace configuration can emit an immediate snapshot outside a worker job.
    scene.waveformTraceSnapshots(std::move(pendingWaveformSnapshots));
    pendingWaveformSnapshots.clear();
  }
}

void DiagramSceneSimulationController::resetTrace(const TraceConfiguration& trace)
{
  QStringList names;
  names.reserve(static_cast<qsizetype>(trace.buses.size()));
  for (const auto& [name, bus] : trace.buses) {
    Q_UNUSED(bus);
    names.push_back(QString::fromStdString(name));
  }
  scene.waveformTraceReset(names, trace.inputCount, trace.widths);
}

void DiagramSceneSimulationController::configureSimulatorTrace(
    const TraceConfiguration& trace, const std::optional<std::string>& traceFile)
{
  simulator->setTraceBuses(trace.buses);
  simulator->setTraceSink([this](uint64_t time, const std::vector<std::string>& values) {
    QStringList qtValues;
    qtValues.reserve(values.size());
    for (const auto& value : values)
      qtValues.push_back(QString::fromStdString(value));

    // Collect snapshots on the worker instead of posting one GUI event per timestamp.
    // finishJob() publishes the completed batch after joining the worker.
    pendingWaveformSnapshots.emplaceBack(time, std::move(qtValues));
  });

  if (!traceFile) {
    simulator->setFstWriter(nullptr);
    return;
  }

  simulator->setFstWriter(std::make_unique<CircuitWriter>(
      *traceFile, trace.buses, CircuitWriter::Options{}));
}

void DiagramSceneSimulationController::startJob(std::function<Simulator::RunResult()> job)
{
  if (!isJobFinished())
    return;

  // 1. Reset job state
  setJobFinished(false);
  resetJobCancellation();
  jobException = nullptr;
  pendingWaveformSnapshots.clear();

  // 2. Prepare progress dialog, polling timers, and lock the view
  setupJobUI();

  // 3. Wrap the job to handle exceptions and state completion safely
  auto executeAndCatch = [this, job = std::move(job)]() {
    try {
      jobResult = job();
    } catch (...) {
      jobException = std::current_exception();
    }
    setJobFinished(true);
  };

  // 4. Dispatch the job
#ifdef __EMSCRIPTEN__
  // Emscripten keeps the existing non-threaded build; cancellation remains cooperative.
  progressDialog->open();
  executeAndCatch();
  finishJob();
#else
  // Desktop simulation mutates only core objects asynchronously.
  worker = std::jthread(std::move(executeAndCatch));
#endif
}

void DiagramSceneSimulationController::setupJobUI()
{
  for (auto* view : scene.views())
    view->setEnabled(false);

  // Create and operate all Qt objects on the GUI thread.
  auto* parent   = scene.views().isEmpty() ? nullptr : scene.views().first()->window();
  progressDialog = new QProgressDialog(QObject::tr("Simulation is running..."),
                                       QObject::tr("Cancel"), 0, 0, parent);
  progressDialog->setWindowTitle(QObject::tr("Simulation"));
  progressDialog->setWindowModality(Qt::WindowModal);
  progressDialog->setAutoClose(false);
  progressDialog->setAutoReset(false);

  QObject::connect(progressDialog, &QProgressDialog::canceled, &scene,
                   [this]() { requestJobCancellation(); });

  completionTimer = new QTimer(&scene);
  completionTimer->setInterval(25);
  QObject::connect(completionTimer, &QTimer::timeout, &scene, [this]() {
    if (isJobFinished())
      finishJob();
  });
  completionTimer->start();

  dialogTimer = new QTimer(&scene);
  dialogTimer->setSingleShot(true);
  dialogTimer->setInterval(300);
  QObject::connect(dialogTimer, &QTimer::timeout, &scene, [this]() {
    if (!isJobFinished() && progressDialog)
      progressDialog->open();
  });
  dialogTimer->start();
}

void DiagramSceneSimulationController::finishJob()
{
  if (!isJobFinished())
    return;

  // Reassigning to an empty jthread implicitly joins to guarantee the worker is cleanly
  // stopped
#ifndef __EMSCRIPTEN__
  worker = std::jthread{};
#endif

  if (!pendingWaveformSnapshots.isEmpty()) {
    // Deliver one batch after the join so the viewer never observes concurrently-mutated
    // sample data and the event loop is not flooded by individual timestamps.
    scene.waveformTraceSnapshots(std::move(pendingWaveformSnapshots));
    pendingWaveformSnapshots.clear();
  }

  // Use deferred deletion to prevent access violations if events are still pending in the
  // queue
  if (completionTimer) {
    completionTimer->stop();
    completionTimer->deleteLater();
    completionTimer = nullptr;
  }
  if (dialogTimer) {
    dialogTimer->stop();
    dialogTimer->deleteLater();
    dialogTimer = nullptr;
  }
  if (progressDialog) {
    progressDialog->close();
    progressDialog->deleteLater();
    progressDialog = nullptr;
  }

  for (auto* view : scene.views())
    view->setEnabled(true);
  refreshGraphicalOutputs();
  scene.update();

  if (!jobException)
    return;

  try {
    std::rethrow_exception(jobException);
  } catch (const std::exception& error) {
    simulationUiLog.error(std::format("Simulation failed: {}", error.what()));
  } catch (...) {
    simulationUiLog.error("Simulation failed with an unknown error");
  }
  jobException = nullptr;
}

void DiagramSceneSimulationController::cancelAndWait()
{
  // Teardown must outlive the worker because its callbacks capture controller state.
  if (!isJobFinished())
    requestJobCancellation();

#ifndef __EMSCRIPTEN__
  // Assigning an empty std::jthread forces a block and automatically joins the active
  // worker
  worker = std::jthread{};
#endif

  setJobFinished(true);
  finishJob();
}

bool DiagramSceneSimulationController::isJobFinished() const
{
  return jobFinished.load(std::memory_order_acquire);
}

void DiagramSceneSimulationController::setJobFinished(const bool finished)
{
  jobFinished.store(finished, std::memory_order_release);
}

bool DiagramSceneSimulationController::isJobCancellationRequested() const
{
  return jobCancellationRequested.load(std::memory_order_acquire);
}

void DiagramSceneSimulationController::requestJobCancellation()
{
  jobCancellationRequested.store(true, std::memory_order_release);
}

void DiagramSceneSimulationController::resetJobCancellation()
{
  jobCancellationRequested.store(false, std::memory_order_release);
}

DiagramSceneSimulationController::TraceConfiguration
DiagramSceneSimulationController::collectTraceConfiguration() const
{
  std::vector<GraphicalIO*> inputs;
  std::vector<GraphicalIO*> outputs;

  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalIO>(item, InputCategory)) {
      inputs.push_back(input);
    } else if (auto* output = category_cast<GraphicalIO>(item, OutputCategory)) {
      outputs.push_back(output);
    }
  }

  const auto byPosition = [](const auto* a, const auto* b) {
    if (a->scenePos().y() != b->scenePos().y())
      return a->scenePos().y() < b->scenePos().y();
    return a->scenePos().x() < b->scenePos().x();
  };

  std::ranges::sort(inputs, byPosition);
  std::ranges::sort(outputs, byPosition);

  TraceConfiguration trace;
  trace.buses.reserve(inputs.size() + outputs.size());

  for (const auto& [index, input] : inputs | SILICON::views::enumerate) {
    const auto component = input->getComponent();
    if (component && !component->getOutputs().empty()) {
      trace.buses.emplace_back(
          componentNameOr(component, QString("input_%1").arg(index).toStdString()),
          component->getOutputs()[0]);
      trace.widths.push_back(static_cast<int>(component->getOutputs()[0].size()));
      ++trace.inputCount;
    }
  }

  for (const auto& [index, output] : outputs | SILICON::views::enumerate) {
    const auto component = output->getComponent();
    if (component && !component->getInputs().empty()) {
      trace.buses.emplace_back(
          componentNameOr(component, QString("output_%1").arg(index).toStdString()),
          component->getInputs()[0]);
      trace.widths.push_back(static_cast<int>(component->getInputs()[0].size()));
    }
  }

  return trace;
}

}  // namespace ui
}  // namespace SILICON
