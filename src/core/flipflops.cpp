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

#include "flipflops.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <core/wireUtils.hpp>
#include <logging/logger.hpp>

namespace SILICON::core {
namespace {

  const auto propagationDelayDefinition = propertyDefinition(
      "propagationDelay", 5, "Delay from a captured input to the output transition.",
      {.minimum = 0});
}  // namespace

FlipFlop::FlipFlop(const unsigned int clockIndex, const unsigned int clearIndex,
                   const unsigned int presetIndex)
  : clockIndex(clockIndex), clearIndex(clearIndex), presetIndex(presetIndex)
{
  defineUnconnectedInputDefault(clearIndex, State::LOW);
  defineUnconnectedInputDefault(presetIndex, State::LOW);
  initializeProperties();
}

FlipFlop::FlipFlop(std::vector<Bus> inputs, std::vector<Bus> outputs,
                   const unsigned int clockIndex, const unsigned int clearIndex,
                   const unsigned int presetIndex)
  : FlipFlop(clockIndex, clearIndex, presetIndex)
{
  this->inputs  = std::move(inputs);
  this->outputs = std::move(outputs);
}

void FlipFlop::clearState()
{
  state = State::UNKNOWN;
  lastSelectedClockEdgeTime.reset();
  lastTimingInputChangeTime.reset();
}

void FlipFlop::simulate(SILICON::simulation::Simulator&     sim,
                        const SILICON::simulation::Context& context)
{
  const State clock = inputState(clockIndex);
  const State    clear     = inputState(clearIndex);
  const State    preset    = inputState(presetIndex);
  const Wire_ptr clockWire = inputWire(clockIndex);

  const auto clockEdge     = SILICON::simulation::Simulator::edgeType(context, clockWire);
  const auto previousClock = context.previousState(clockWire);
  const bool selectedClockEdge  = clockEdge == edgeType;
  const auto currentTime        = sim.getCurrentTime();
  const bool timingInputChanged = hasTimingSensitiveInputChange(context);

  if (timingInputChanged)
    lastTimingInputChangeTime = currentTime;

  if (clear == State::HIGH && preset == State::LOW) {
    driveOutput(sim, State::LOW);
    return;
  }

  if (clear == State::LOW && preset == State::HIGH) {
    driveOutput(sim, State::HIGH);
    return;
  }

  if (clear == State::LOW && preset == State::LOW) {
    if (selectedClockEdge) {
      const bool setupViolation = violatesSetupTime(currentTime, timingInputChanged);
      lastSelectedClockEdgeTime = currentTime;
      driveOutput(sim, setupViolation ? State::UNKNOWN : captureState());
    }

    else if (violatesHoldTime(currentTime, timingInputChanged)) {
      lastTimingInputChangeTime = currentTime;
      driveOutput(sim, State::UNKNOWN);
    }

    else if (clockEdge == SILICON::simulation::Simulator::EdgeType::UNKNOWN
             && previousClock && mayHaveSelectedEdge(*previousClock, clock)) {
      // The clock transition is ambiguous (e.g., LOW -> UNKNOWN). We don't know if the
      // physical hardware would have triggered, so pessimistically invalidate the data.
      lastSelectedClockEdgeTime = currentTime;
      driveOutput(sim, State::UNKNOWN);
    }

    else if (context.initialEvaluation) {
      clearState();
      driveOutput(sim, state);
    }

    return;
  }

  driveOutput(sim, State::UNKNOWN);
}

void FlipFlop::initializeProperties()
{
  defineProperty(propagationDelayDefinition);
  static const auto setupTimeDefinition = propertyDefinition(
      "setupTime", 0, "Required input stability before the selected clock edge.",
      {.minimum = 0});
  defineProperty(setupTimeDefinition);
  static const auto holdTimeDefinition = propertyDefinition(
      "holdTime", 0, "Required input stability after the selected clock edge.",
      {.minimum = 0});
  defineProperty(holdTimeDefinition);
  static const auto triggerEdgeDefinition = stringPropertyDefinition(
      "triggerEdge", "PET", {"PET", "NET"},
      "Clock edge used to capture the input: PET rises, NET falls.");
  defineProperty(triggerEdgeDefinition);

  setPropertyCallback("propagationDelay", [this](const PropertyValue& value) {
    this->propagationDelay = static_cast<uint64_t>(std::get<int>(value));
    return value;
  });
  setPropertyCallback("setupTime", [this](const PropertyValue& value) {
    this->setupTime = static_cast<uint64_t>(std::get<int>(value));
    return value;
  });
  setPropertyCallback("holdTime", [this](const PropertyValue& value) {
    this->holdTime = static_cast<uint64_t>(std::get<int>(value));
    return value;
  });
  setPropertyCallback("triggerEdge", [this](const PropertyValue& value) {
    this->edgeType = std::get<std::string>(value) == "PET"
                         ? SILICON::simulation::Simulator::EdgeType::RISE
                         : SILICON::simulation::Simulator::EdgeType::FALL;
    return value;
  });
}

void FlipFlop::driveOutput(SILICON::simulation::Simulator& sim, State newState)
{
  if (outputBusSize(0) == 0 || outputBusSize(1) == 0)
    return;

  newState = SILICON::wireUtils::normalizeBinaryOrUnknown(newState);
  state    = newState;

  sim.updateWire(outputWire(0), newState, propagationDelay, weak_from_this());
  sim.updateWire(outputWire(1), newState == State::UNKNOWN ? State::UNKNOWN : !newState,
                 propagationDelay, weak_from_this());
}

bool FlipFlop::hasTimingSensitiveInputChange(
    const SILICON::simulation::Context& context) const
{
  for (unsigned int inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
    if (!isTimingSensitiveInput(inputIndex))
      continue;

    for (const auto& wire : inputs[inputIndex]) {
      if (context.changed(wire))
        return true;
    }
  }

  return false;
}

bool FlipFlop::violatesSetupTime(const uint64_t currentTime,
                                 const bool     timingInputChanged) const
{
  if (setupTime == 0)
    return false;

  if (timingInputChanged)
    return true;

  return lastTimingInputChangeTime
         && currentTime - *lastTimingInputChangeTime < setupTime;
}

bool FlipFlop::violatesHoldTime(const uint64_t currentTime,
                                const bool     timingInputChanged) const
{
  return holdTime != 0 && timingInputChanged && lastSelectedClockEdgeTime
         && currentTime - *lastSelectedClockEdgeTime < holdTime;
}

bool FlipFlop::mayHaveSelectedEdge(const State previousClock,
                                   const State currentClock) const
{
  const bool  isPositiveEdge = edgeType == SILICON::simulation::Simulator::EdgeType::RISE;
  const State inactiveClock  = isPositiveEdge ? State::LOW : State::HIGH;
  const State activeClock    = isPositiveEdge ? State::HIGH : State::LOW;

  return SILICON::wireUtils::mayBe(previousClock, inactiveClock)
         && SILICON::wireUtils::mayBe(currentClock, activeClock);
}

DFlipFlop::DFlipFlop(Wire_ptr d, Wire_ptr clock, Wire_ptr clear, Wire_ptr preset,
                     Wire_ptr q, Wire_ptr notQ)
  : FlipFlop(
        {{std::move(d)}, {std::move(clock)}, {std::move(clear)}, {std::move(preset)}},
        {{std::move(q)}, {std::move(notQ)}}, busIndex(Inputs::Clock),
        busIndex(Inputs::Clear), busIndex(Inputs::Preset))
{
}

State DFlipFlop::captureState() const
{
  return SILICON::wireUtils::normalizeBinaryOrUnknown(inputState(Inputs::D));
}

bool DFlipFlop::isTimingSensitiveInput(const unsigned int inputIndex) const
{
  return inputIndex == busIndex(Inputs::D);
}

EFlipFlop::EFlipFlop(Wire_ptr d, Wire_ptr enable, Wire_ptr clock, Wire_ptr clear,
                     Wire_ptr preset, Wire_ptr q, Wire_ptr notQ)
  : FlipFlop({{std::move(d)},
              {std::move(enable)},
              {std::move(clock)},
              {std::move(clear)},
              {std::move(preset)}},
             {{std::move(q)}, {std::move(notQ)}}, busIndex(Inputs::Clock),
             busIndex(Inputs::Clear), busIndex(Inputs::Preset))
{
}

State EFlipFlop::captureState() const
{
  const State enable = inputState(Inputs::Enable);
  if (enable == State::LOW)
    return latchedState();
  if (enable == State::HIGH)
    return SILICON::wireUtils::normalizeBinaryOrUnknown(inputState(Inputs::D));
  return State::UNKNOWN;
}

bool EFlipFlop::isTimingSensitiveInput(const unsigned int inputIndex) const
{
  return inputIndex == busIndex(Inputs::D) || inputIndex == busIndex(Inputs::Enable);
}

DLatch::DLatch()
{
  initializeProperties();
}

DLatch::DLatch(Wire_ptr d, Wire_ptr enable, Wire_ptr q, Wire_ptr notQ)
  : Component({{std::move(d)}, {std::move(enable)}}, {{std::move(q)}, {std::move(notQ)}})
{
  initializeProperties();
}

void DLatch::simulate(SILICON::simulation::Simulator&     sim,
                      const SILICON::simulation::Context& context)
{
  if (context.initialEvaluation)
    state = State::UNKNOWN;

  const State enable = inputState(Inputs::Enable);
  if (enable == State::LOW) {
    driveOutput(sim, state);
    return;
  }

  if (enable == State::HIGH) {
    driveOutput(sim, SILICON::wireUtils::normalizeBinaryOrUnknown(inputState(Inputs::D)));
    return;
  }

  driveOutput(sim, State::UNKNOWN);
}

void DLatch::initializeProperties()
{
  defineProperty(propagationDelayDefinition);
  setPropertyCallback("propagationDelay", [this](const PropertyValue& value) {
    propagationDelay = static_cast<uint64_t>(std::get<int>(value));
    return value;
  });
}

void DLatch::driveOutput(SILICON::simulation::Simulator& sim, State newState)
{
  if (outputBusSize(0) == 0 || outputBusSize(1) == 0)
    return;

  newState = SILICON::wireUtils::normalizeBinaryOrUnknown(newState);
  state    = newState;

  sim.updateWire(outputWire(0), newState, propagationDelay, weak_from_this());
  sim.updateWire(outputWire(1), newState == State::UNKNOWN ? State::UNKNOWN : !newState,
                 propagationDelay, weak_from_this());
}

JKFlipFlop::JKFlipFlop(Wire_ptr j, Wire_ptr k, Wire_ptr clock, Wire_ptr clear,
                       Wire_ptr preset, Wire_ptr q, Wire_ptr notQ)
  : FlipFlop({{std::move(j)},
              {std::move(k)},
              {std::move(clock)},
              {std::move(clear)},
              {std::move(preset)}},
             {{std::move(q)}, {std::move(notQ)}}, busIndex(Inputs::Clock),
             busIndex(Inputs::Clear), busIndex(Inputs::Preset))
{
  /* PIN MAP:
     J     = inputs [0][0];
     K     = inputs [1][0];
     CLK   = inputs [2][0];
     CLR   = inputs [3][0];
     PRE   = inputs [4][0];
     Q     = outputs[0][0];
     not-Q = outputs[1][0]; */
}

State JKFlipFlop::captureState() const
{
  const State j = inputState(Inputs::J);
  const State k = inputState(Inputs::K);

  if (!SILICON::wireUtils::isKnownBinary(j) || !SILICON::wireUtils::isKnownBinary(k))
    return State::UNKNOWN;

  if (j == State::LOW) {
    return (k == State::LOW) ? latchedState() : State::LOW;
  }

  if (k == State::LOW)
    return State::HIGH;

  const State current = latchedState();
  return SILICON::wireUtils::isKnownBinary(current) ? !current : State::UNKNOWN;
}

bool JKFlipFlop::isTimingSensitiveInput(const unsigned int inputIndex) const
{
  return inputIndex == busIndex(Inputs::J) || inputIndex == busIndex(Inputs::K);
}

std::vector<PortDescription> DFlipFlop::portDescriptions() const
{
  return {{"d", PortDirection::Input, busIndex(Inputs::D), 1, "Data to capture."},
          {"clk", PortDirection::Input, busIndex(Inputs::Clock), 1,
           "Capture clock; selected edge is configured by triggerEdge."},
          {"clr", PortDirection::Input, busIndex(Inputs::Clear), 1,
           "Active-high asynchronous clear; unconnected means LOW."},
          {"pre", PortDirection::Input, busIndex(Inputs::Preset), 1,
           "Active-high asynchronous preset; unconnected means LOW."},
          {"q", PortDirection::Output, 0, 1, "Stored state."},
          {"!q", PortDirection::Output, 1, 1, "Inverse of the stored state."}};
}

std::vector<PortDescription> EFlipFlop::portDescriptions() const
{
  return {{"d", PortDirection::Input, busIndex(Inputs::D), 1, "Data to capture."},
          {"en", PortDirection::Input, busIndex(Inputs::Enable), 1,
           "Active-high capture enable."},
          {"clk", PortDirection::Input, busIndex(Inputs::Clock), 1,
           "Capture clock; selected edge is configured by triggerEdge."},
          {"clr", PortDirection::Input, busIndex(Inputs::Clear), 1,
           "Active-high asynchronous clear; unconnected means LOW."},
          {"pre", PortDirection::Input, busIndex(Inputs::Preset), 1,
           "Active-high asynchronous preset; unconnected means LOW."},
          {"q", PortDirection::Output, 0, 1, "Stored state."},
          {"!q", PortDirection::Output, 1, 1, "Inverse of the stored state."}};
}

std::vector<PortDescription> JKFlipFlop::portDescriptions() const
{
  return {{"j", PortDirection::Input, busIndex(Inputs::J), 1,
           "Set control sampled on the selected clock edge."},
          {"k", PortDirection::Input, busIndex(Inputs::K), 1,
           "Reset control sampled on the selected clock edge."},
          {"clk", PortDirection::Input, busIndex(Inputs::Clock), 1,
           "Capture clock; selected edge is configured by triggerEdge."},
          {"clr", PortDirection::Input, busIndex(Inputs::Clear), 1,
           "Active-high asynchronous clear; unconnected means LOW."},
          {"pre", PortDirection::Input, busIndex(Inputs::Preset), 1,
           "Active-high asynchronous preset; unconnected means LOW."},
          {"q", PortDirection::Output, 0, 1, "Stored state."},
          {"!q", PortDirection::Output, 1, 1, "Inverse of the stored state."}};
}

std::vector<PortDescription> DLatch::portDescriptions() const
{
  return {{"d", PortDirection::Input, busIndex(Inputs::D), 1, "Data to capture."},
          {"en", PortDirection::Input, busIndex(Inputs::Enable), 1,
           "Active-high capture enable."},
          {"q", PortDirection::Output, 0, 1, "Stored state."},
          {"!q", PortDirection::Output, 1, 1, "Inverse of the stored state."}};
}

}  // namespace SILICON::core
