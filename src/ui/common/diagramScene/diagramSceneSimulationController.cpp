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
#include <string>

#include <QGraphicsItem>
#include <QObject>

#include <utils/ranges_wrapper.hpp>

#include <core/circuit.hpp>
#include <core/simulator.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

namespace {

constexpr ItemCategory InputCategory  = ItemCategory::IO | ItemCategory::Input;
constexpr ItemCategory OutputCategory = ItemCategory::IO | ItemCategory::Output;

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

}  // namespace

DiagramSceneSimulationController::DiagramSceneSimulationController(DiagramScene& scene)
  : scene(scene)
{
}

DiagramSceneSimulationController::~DiagramSceneSimulationController() = default;

void DiagramSceneSimulationController::enterSimulationMode()
{
  scene.calculateWiresForComponents();

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

  // Initialize Circuit & Simulator frameworks
  scene.setCircuit(std::make_shared<Circuit>(coreComps, false));

  // The simulator constructor evaluates the circuit once at time zero. Attach tracing
  // before advancing time so waveform exports include the initial 0ns snapshot.
  simulator = std::make_unique<Simulator>(scene.getCircuit(), 0, true);
  refreshTraceConfiguration();
  simulator->run(1);

  refreshGraphicalOutputs();
  scene.update();
}

void DiagramSceneSimulationController::exitSimulationMode()
{
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
  if (!simulator)
    return;

  simulator->setBus(targetBus, value, source);
  simulator->run(20);

  refreshGraphicalOutputs();
  scene.update();
}

void DiagramSceneSimulationController::setFstTraceFile(
    std::optional<std::string> fileName)
{
  fstTraceFile = std::move(fileName);

  if (!simulator)
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

  scene.calculateWiresForComponents();
  refreshGraphicalOutputs();
  refreshTraceConfiguration();
  scene.update();
}

void DiagramSceneSimulationController::clearWaveformTrace()
{
  scene.waveformTraceReset({}, 0);
}

void DiagramSceneSimulationController::refreshTraceConfiguration()
{
  if (!simulator)
    return;

  auto trace = collectTraceConfiguration();

  QStringList names;
  names.reserve(static_cast<qsizetype>(trace.buses.size()));
  for (const auto& [name, bus] : trace.buses) {
    Q_UNUSED(bus);
    names.push_back(QString::fromStdString(name));
  }
  scene.waveformTraceReset(names, trace.inputCount);

  simulator->setTraceBuses(trace.buses);
  simulator->setTraceSink([this](uint64_t time, const std::vector<std::string>& values) {
    QStringList qtValues;
    qtValues.reserve(values.size());
    for (const auto& value : values)
      qtValues.push_back(QString::fromStdString(value));

    scene.waveformTraceSnapshot(time, qtValues);
  });

  if (!fstTraceFile) {
    simulator->setFstWriter(nullptr);
    return;
  }

  simulator->setFstWriter(std::make_unique<SiliconFstWriter>(
      *fstTraceFile, trace.buses, SiliconFstWriter::Options{}));
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

  for (const auto& [index, input] : inputs | silicon::views::enumerate) {
    const auto component = input->getComponent();
    if (component && !component->getOutputs().empty()) {
      trace.buses.emplace_back(
          componentNameOr(component, QString("input_%1").arg(index).toStdString()),
          component->getOutputs()[0]);
      ++trace.inputCount;
    }
  }

  for (const auto& [index, output] : outputs | silicon::views::enumerate) {
    const auto component = output->getComponent();
    if (component && !component->getInputs().empty()) {
      trace.buses.emplace_back(
          componentNameOr(component, QString("output_%1").arg(index).toStdString()),
          component->getInputs()[0]);
    }
  }

  return trace;
}
