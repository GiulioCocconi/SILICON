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

#include "register.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <core/wireUtils.hpp>

namespace {

void validateSize(const int size)
{
  if (size <= 1)
    throw std::invalid_argument("Register size must be greater than 1");
}

void validateDelay(const PropertyValue& value)
{
  if (std::get<int>(value) < 0)
    throw std::invalid_argument("Register delay must be non-negative");
}

State normalizedInputState(const State state)
{
  return silicon::wire::normalizeBinaryOrUnknown(state);
}

unsigned short dataBusSize(const std::string& inputType, const int size)
{
  return inputType == Register::ParallelType ? static_cast<unsigned short>(size) : 1;
}

unsigned short registerOutputBusSize(const std::string& outputType, const int size)
{
  return outputType == Register::ParallelType ? static_cast<unsigned short>(size) : 1;
}

}  // namespace

Register::Register()
{
  initializeProperties();
}

Register::Register(Bus data, Wire_ptr clock, Wire_ptr enable, Wire_ptr clear, Bus output)
  : Register()
{
  const auto initialDataSize   = data.size();
  const auto initialOutputSize = output.size();

  inputs  = {std::move(data), {std::move(clock)}, {std::move(enable)},
             {std::move(clear)}};
  outputs = {std::move(output)};

  const int inferredSize = static_cast<int>(std::max(initialDataSize, initialOutputSize));
  setProperty("size", std::max(2, inferredSize));
  setProperty("inputType", initialDataSize == 1 && initialOutputSize > 1
                           ? std::string(SerialType)
                           : std::string(ParallelType));
  setProperty("outputType", initialOutputSize == 1 && initialDataSize > 1
                            ? std::string(SerialType)
                            : std::string(ParallelType));
}

int Register::configuredSize() const
{
  return getPropertyValue<int>("size").value_or(2);
}

std::string Register::configuredInputType() const
{
  return getPropertyValue<std::string>("inputType").value_or(std::string(ParallelType));
}

std::string Register::configuredOutputType() const
{
  return getPropertyValue<std::string>("outputType").value_or(std::string(ParallelType));
}

bool Register::parallelInput() const
{
  return configuredInputType() == ParallelType;
}

bool Register::parallelOutput() const
{
  return configuredOutputType() == ParallelType;
}

void Register::initializeProperties()
{
  defineProperty("delay", 0);
  defineStringListProperty("inputType", std::string(ParallelType),
                           {std::string(ParallelType), std::string(SerialType)});
  defineStringListProperty("outputType", std::string(ParallelType),
                           {std::string(ParallelType), std::string(SerialType)});
  defineProperty("size", 2);

  setPropertyCallback("delay", [](const PropertyValue& value) {
    validateDelay(value);
    return value;
  });
  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
    validateSize(size);
    if (!inputs.empty() && !outputs.empty())
      setSize(size);
    return value;
  });
  setPropertyCallback("inputType", [this](const PropertyValue& value) {
    const std::string inputType = std::get<std::string>(value);
    if (!inputs.empty() && !outputs.empty())
      setInputType(inputType);
    return value;
  });
  setPropertyCallback("outputType", [this](const PropertyValue& value) {
    const std::string outputType = std::get<std::string>(value);
    if (!inputs.empty() && !outputs.empty())
      setOutputType(outputType);
    return value;
  });
}

int Register::setSize(const int size)
{
  validateSize(size);
  if (state.size() != static_cast<std::size_t>(size))
    state.assign(static_cast<std::size_t>(size), State::UNKNOWN);
  reshapeBuses(size, configuredInputType(), configuredOutputType());
  return size;
}

std::string Register::setInputType(std::string inputType)
{
  reshapeBuses(configuredSize(), inputType, configuredOutputType());
  return inputType;
}

std::string Register::setOutputType(std::string outputType)
{
  reshapeBuses(configuredSize(), configuredInputType(), outputType);
  return outputType;
}

void Register::reshapeBuses()
{
  reshapeBuses(configuredSize(), configuredInputType(), configuredOutputType());
}

void Register::reshapeBuses(const int size, const std::string& inputType,
                            const std::string& outputType)
{
  std::vector<Bus> newInputs = inputs;
  newInputs.resize(inputType == ParallelType && outputType == SerialType ? 5 : 4);
  newInputs[busIndex(Inputs::Data)].setSize(dataBusSize(inputType, size));
  newInputs[busIndex(Inputs::Clock)].setSize(1);
  newInputs[busIndex(Inputs::Enable)].setSize(1);
  newInputs[busIndex(Inputs::Clear)].setSize(1);
  if (newInputs.size() > busIndex(Inputs::Load))
    newInputs[busIndex(Inputs::Load)].setSize(1);
  setInputs(newInputs);

  std::vector<Bus> newOutputs = outputs;
  newOutputs.resize(1);
  newOutputs[busIndex(Outputs::Out)].setSize(registerOutputBusSize(outputType, size));
  setOutputs(newOutputs);

  if (state.size() != static_cast<std::size_t>(size))
    state.assign(static_cast<std::size_t>(size), State::UNKNOWN);
}

void Register::clearState()
{
  state.assign(static_cast<std::size_t>(configuredSize()), State::LOW);
}

void Register::driveOutput(Simulator& sim)
{
  if (outputs.empty() || outputBusSize(Outputs::Out) == 0)
    return;

  const auto delay = static_cast<uint64_t>(getPropertyValue<int>("delay").value_or(0));

  if (parallelOutput()) {
    for (unsigned short bit = 0; bit < outputBusSize(Outputs::Out); ++bit) {
      const State bitState = bit < state.size() ? state[bit] : State::ERROR;
      sim.updateWire(outputWire(Outputs::Out, bit), bitState, delay, weak_from_this());
    }
    return;
  }

  const State bitState = state.empty() ? State::ERROR : state.front();
  sim.updateWire(outputWire(Outputs::Out), bitState, delay, weak_from_this());
}

void Register::simulate(Simulator& sim, const SimulationContext& context)
{
  if (state.size() != static_cast<std::size_t>(configuredSize()))
    state.assign(static_cast<std::size_t>(configuredSize()), State::UNKNOWN);

  const State clear = inputState(Inputs::Clear);
  if (clear == State::HIGH) {
    clearState();
    driveOutput(sim);
    return;
  }
  if (clear == State::UNKNOWN || clear == State::ERROR) {
    // assign() does not reallocate memory if the size remains the same
    state.assign(state.size(), State::UNKNOWN);
    driveOutput(sim);
    return;
  }

  const State enable = inputState(Inputs::Enable);
  if (enable == State::UNKNOWN || enable == State::ERROR) {
    state.assign(state.size(), State::UNKNOWN);
    driveOutput(sim);
    return;
  }
  if (enable == State::LOW) {
    if (context.initialEvaluation)
      driveOutput(sim);
    return;
  }

  const Wire_ptr clockWire = inputWire(Inputs::Clock);
  if (Simulator::edgeType(context, clockWire) == Simulator::EdgeType::RISE) {
    
    if (parallelInput()) {
      if (parallelOutput()) {
        // PIPO: Overwrite state directly from inputs
        for (std::size_t bit = 0; bit < state.size(); ++bit)
          state[bit] = normalizedInputState(inputState(Inputs::Data, bit));
      } else {
        // PISO
        const State load = inputState(Inputs::Load);
        if (load == State::UNKNOWN || load == State::ERROR) {
          state.assign(state.size(), State::UNKNOWN);
        } else if (load == State::HIGH) {
          // Load Parallel Data directly into state
          for (std::size_t bit = 0; bit < state.size(); ++bit)
            state[bit] = normalizedInputState(inputState(Inputs::Data, bit));
        } else {
          // Shift Right In-Place
          for (std::size_t bit = 0; bit + 1 < state.size(); ++bit)
            state[bit] = state[bit + 1];
          if (!state.empty())
            state.back() = State::LOW;
        }
      }
    } else {
      // Serial Input (SISO / SIPO): Shift Right In-Place
      if (!state.empty()) {
        for (std::size_t bit = 0; bit + 1 < state.size(); ++bit)
          state[bit] = state[bit + 1];
        state.back() = normalizedInputState(inputState(Inputs::Data));
      }
    }

    driveOutput(sim);
    return;
  }

  if (context.initialEvaluation)
    driveOutput(sim);
}
