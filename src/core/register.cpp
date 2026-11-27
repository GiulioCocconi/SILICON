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

#include "register.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <core/wireUtils.hpp>

namespace SILICON::core {

namespace {

  bool needsLoadInput(const std::string& inputType, const std::string& outputType)
  {
    return inputType == Register::PARALLEL_TYPE && outputType == Register::SERIAL_TYPE;
  }

  State normalizedInputState(const State state)
  {
    return SILICON::wireUtils::normalizeBinaryOrUnknown(state);
  }

  unsigned short dataBusSize(const std::string& inputType, const int size)
  {
    return inputType == Register::PARALLEL_TYPE ? static_cast<unsigned short>(size) : 1;
  }

  unsigned short registerOutputBusSize(const std::string& outputType, const int size)
  {
    return outputType == Register::PARALLEL_TYPE ? static_cast<unsigned short>(size) : 1;
  }

}  // namespace

Register::Register()
{
  initializeProperties();
}

std::vector<DocumentationConfiguration> Register::documentationConfigurations() const
{
  return {{"piso",
           "Parallel input, serial output (PISO)",
           "The load pin selects parallel capture or serial shifting.",
           {{"outputType", std::string(SERIAL_TYPE)}}},
          {"sipo",
           "Serial input, parallel output (SIPO)",
           "Serial data shifts into the register and is exposed as a parallel word.",
           {{"inputType", std::string(SERIAL_TYPE)}}},
          {"siso",
           "Serial input, serial output (SISO)",
           "Both data ports transfer one bit at a time.",
           {{"inputType", std::string(SERIAL_TYPE)},
            {"outputType", std::string(SERIAL_TYPE)}}}};
}

Register::Register(Bus data, Wire_ptr clock, Wire_ptr enable, Wire_ptr clear, Bus output)
  : Register()
{
  const auto initialDataSize   = data.size();
  const auto initialOutputSize = output.size();

  inputs = {std::move(data), {std::move(clock)}, {std::move(enable)}, {std::move(clear)}};
  outputs = {std::move(output)};

  const int inferredSize = static_cast<int>(std::max(initialDataSize, initialOutputSize));
  setProperty("size", std::max(2, inferredSize));
  setProperty("inputType", initialDataSize == 1 && initialOutputSize > 1
                               ? std::string(SERIAL_TYPE)
                               : std::string(PARALLEL_TYPE));
  setProperty("outputType", initialOutputSize == 1 && initialDataSize > 1
                                ? std::string(SERIAL_TYPE)
                                : std::string(PARALLEL_TYPE));
}

int Register::configuredSize() const
{
  return getPropertyValue<int>("size").value_or(2);
}

std::string Register::configuredInputType() const
{
  return getPropertyValue<std::string>("inputType").value_or(std::string(PARALLEL_TYPE));
}

std::string Register::configuredOutputType() const
{
  return getPropertyValue<std::string>("outputType").value_or(std::string(PARALLEL_TYPE));
}

bool Register::parallelInput() const
{
  return configuredInputType() == PARALLEL_TYPE;
}

bool Register::parallelOutput() const
{
  return configuredOutputType() == PARALLEL_TYPE;
}

void Register::initializeProperties()
{
  defineUnconnectedInputDefault(busIndex(Inputs::Clear), State::LOW);
  defineProperty(ZeroDelayProperty);
  static const auto inputTypeDefinition = stringPropertyDefinition(
      "inputType", std::string(PARALLEL_TYPE),
      {std::string(PARALLEL_TYPE), std::string(SERIAL_TYPE)},
      "Parallel input captures a complete word; serial input shifts toward bit zero and "
      "inserts the new bit at the most-significant position.");
  defineProperty(inputTypeDefinition);
  static const auto outputTypeDefinition =
      stringPropertyDefinition("outputType", std::string(PARALLEL_TYPE),
                               {std::string(PARALLEL_TYPE), std::string(SERIAL_TYPE)},
                               "Parallel output exposes the complete stored word; serial "
                               "output exposes the least-significant stored bit.");
  defineProperty(outputTypeDefinition);
  static const auto sizeDefinition = propertyDefinition(
      "size", 2,
      "Stored word length; parallel ports expose the full word while serial transfers "
      "shift toward the least-significant output bit.",
      {.minimum = 2});
  defineProperty(sizeDefinition);

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int size = std::get<int>(value);
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
  (void)propertyDeclaration("size").normalize(size);
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
  newInputs.resize(needsLoadInput(inputType, outputType) ? busIndex(Inputs::Load) + 1
                                                         : busIndex(Inputs::Clear) + 1);
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

void Register::driveOutput(SILICON::simulation::Simulator& sim)
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

void Register::simulate(SILICON::simulation::Simulator&     sim,
                        const SILICON::simulation::Context& context)
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
  if (SILICON::simulation::Simulator::edgeType(context, clockWire)
      == SILICON::simulation::Simulator::EdgeType::RISE) {
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

std::vector<PortDescription> Register::portDescriptions() const
{
  const auto size       = configuredSize();
  const auto inputType  = configuredInputType();
  const auto outputType = configuredOutputType();
  auto       result     = std::vector<PortDescription>{
      {parallelInput() ? "d" : "si", PortDirection::Input, busIndex(Inputs::Data),
       dataBusSize(inputType, size),
       "Parallel word to load, or serial bit shifted into the most-significant "
                 "position."},
      {"clk", PortDirection::Input, busIndex(Inputs::Clock), 1,
                 "Rising-edge capture clock."},
      {"en", PortDirection::Input, busIndex(Inputs::Enable), 1,
                 "Active-high capture or shift enable."},
      {"clr", PortDirection::Input, busIndex(Inputs::Clear), 1,
                 "Active-high asynchronous clear; unconnected means LOW."}};
  if (needsLoadInput(inputType, outputType))
    result.push_back(
        {"ld", PortDirection::Input, busIndex(Inputs::Load), 1,
         "Present only for parallel-input/serial-output mode. HIGH loads the word; LOW "
         "shifts right and inserts LOW at the most-significant position."});
  result.push_back(
      {parallelOutput() ? "q" : "so", PortDirection::Output, busIndex(Outputs::Out),
       registerOutputBusSize(outputType, size),
       "Stored word in parallel mode, or its least-significant bit in serial mode."});
  return result;
}

}  // namespace SILICON::core
