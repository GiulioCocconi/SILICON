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

  // Restore inputs to neutral state & inject LOW into the logic bus
  // Do this BEFORE creating the Simulator to avoid double-evaluations
  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
      // Updates visual shape
      input->setState(State::LOW);

      // Manually push the LOW state directly to the logic bus
      auto bus = input->getComponent()->getOutputs()[0];
      bus.forceSetCurrentValue(0, input->getComponent()->weak_from_this());

      // Now connect the signal for user clicks during runtime
      QObject::connect(input, &GraphicalInput::inputToggled, &scene,
                       &DiagramScene::handleInputToggled, Qt::UniqueConnection);
    }
  }

  // 2. Gather core components
  Component_set coreComps;
  for (auto* item : scene.items()) {
    const auto* comp =
        category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
    if (comp && comp->getComponent())
      coreComps.insert(comp->getComponent());
  }

  // 3. Initialize Circuit & Simulator frameworks
  scene.setCircuit(std::make_shared<Circuit>(coreComps, false));

  // The simulator constructor evaluates the circuit once at time zero. Attach tracing
  // before advancing time so waveform exports include the initial 0ns snapshot.
  simulator = std::make_unique<Simulator>(scene.getCircuit(), 0, true);
  resetWaveformTrace();
  configureSimulatorTrace();
  applyFstTraceWriter();
  simulator->run(1);

  refreshGraphicalOutputs();
  scene.update();
}

void DiagramSceneSimulationController::exitSimulationMode()
{
  // Disconnect signals from inputs
  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
      QObject::disconnect(input, &GraphicalInput::inputToggled, &scene,
                          &DiagramScene::handleInputToggled);
    }
  }

  simulator.reset();
  scene.setCircuit(nullptr);

  for (auto* item : scene.items()) {
    if (auto* out = category_cast<GraphicalOutputSingle>(item, ItemCategory::Output))
      out->setState(State::UNKNOWN);
  }

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

  resetWaveformTrace();
  configureSimulatorTrace();
  applyFstTraceWriter();
}

void DiagramSceneSimulationController::refreshGraphicalOutputs()
{
  for (auto* item : scene.items()) {
    if (auto* out = category_cast<GraphicalOutputSingle>(item, ItemCategory::Output)) {
      if (!out->getComponent())
        continue;

      auto bus = out->getComponent()->getInputs()[0];

      State s = State::UNKNOWN;
      if (bus.isInErrorState())
        s = State::ERROR;
      else if (!bus.hasUnknowns())
        s = (bus.getCurrentValue() > 0) ? State::HIGH : State::LOW;

      out->setState(s);
    }
  }
}

void DiagramSceneSimulationController::handleTopologyChanged()
{
  if (scene.getInteractionMode() != InteractionMode::SIMULATION_MODE)
    return;

  scene.calculateWiresForComponents();
  refreshGraphicalOutputs();
  resetWaveformTrace();
  configureSimulatorTrace();
  applyFstTraceWriter();
  scene.update();
}

void DiagramSceneSimulationController::clearWaveformTrace()
{
  scene.waveformTraceReset({}, 0);
}

void DiagramSceneSimulationController::applyFstTraceWriter()
{
  if (!simulator)
    return;

  if (!fstTraceFile) {
    simulator->setFstWriter(nullptr);
    return;
  }

  simulator->setFstWriter(std::make_unique<SiliconFstWriter>(
      *fstTraceFile, collectTraceBuses(), SiliconFstWriter::Options{}));
}

void DiagramSceneSimulationController::configureSimulatorTrace()
{
  if (!simulator)
    return;

  simulator->setTraceBuses(collectTraceBuses());
  simulator->setTraceSink([this](uint64_t time, const std::vector<std::string>& values) {
    QStringList qtValues;
    qtValues.reserve(values.size());
    for (const auto& value : values)
      qtValues.push_back(QString::fromStdString(value));

    scene.waveformTraceSnapshot(time, qtValues);
  });
}

void DiagramSceneSimulationController::resetWaveformTrace()
{
  QStringList names;
  for (const auto& [name, bus] : collectTraceBuses()) {
    Q_UNUSED(bus);
    names.push_back(QString::fromStdString(name));
  }

  scene.waveformTraceReset(names, collectTraceInputCount());
}

int DiagramSceneSimulationController::collectTraceInputCount() const
{
  int count = 0;
  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input)) {
      const auto component = input->getComponent();
      if (component && !component->getOutputs().empty())
        ++count;
    }
  }

  return count;
}

std::vector<SiliconFstWriter::NamedBus>
DiagramSceneSimulationController::collectTraceBuses() const
{
  std::vector<GraphicalInput*>        inputs;
  std::vector<GraphicalOutputSingle*> outputs;

  for (auto* item : scene.items()) {
    if (auto* input = category_cast<GraphicalInput>(item, ItemCategory::Input))
      inputs.push_back(input);
    else if (auto* output =
                 category_cast<GraphicalOutputSingle>(item, ItemCategory::Output)) {
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

  std::vector<SiliconFstWriter::NamedBus> buses;
  buses.reserve(inputs.size() + outputs.size());

  for (const auto& [index, input] : inputs | silicon::views::enumerate) {
    const auto component = input->getComponent();
    if (component && !component->getOutputs().empty()) {
      buses.emplace_back(
          componentNameOr(component, QString("input_%1").arg(index).toStdString()),
          component->getOutputs()[0]);
    }
  }

  for (const auto& [index, output] : outputs | silicon::views::enumerate) {
    const auto component = output->getComponent();
    if (component && !component->getInputs().empty()) {
      buses.emplace_back(
          componentNameOr(component, QString("output_%1").arg(index).toStdString()),
          component->getInputs()[0]);
    }
  }

  return buses;
}
