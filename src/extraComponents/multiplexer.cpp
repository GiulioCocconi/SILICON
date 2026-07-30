/*
 Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include "multiplexer.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <core/simulator.hpp>

namespace SILICON::extra {
using namespace SILICON::core;

namespace {
  constexpr int MaxSelectionSize = 15;

  // Converts an N-bit selection bus into the width of a packed one-bit mux/demux bus.
  unsigned short dataWidthForSelectionSize(const int selectionSize)
  {
    return static_cast<unsigned short>(1u << selectionSize);
  }

  // Converts an N-bit selection bus into the number of exposed data/output buses.
  std::size_t dataBusCountForSelectionSize(const int selectionSize)
  {
    return std::size_t{1} << selectionSize;
  }

  void validateSelectionSize(const std::string_view componentName,
                             const int              selectionSize)
  {
    if (selectionSize < 1)
      throw std::invalid_argument(std::string(componentName)
                                  + " selectionSize must be at least 1");
    if (selectionSize > MaxSelectionSize)
      throw std::invalid_argument(std::string(componentName)
                                  + " selectionSize is too large");
  }

  void validateBusSize(const std::string_view componentName, const int busSize)
  {
    if (busSize <= 0)
      throw std::invalid_argument(std::string(componentName)
                                  + " busSize must be positive");
    if (busSize > std::numeric_limits<unsigned short>::max())
      throw std::invalid_argument(std::string(componentName) + " busSize is too large");
  }

  void validateDelay(const std::string_view componentName, const PropertyValue& value)
  {
    if (std::get<int>(value) < 0)
      throw std::invalid_argument(std::string(componentName)
                                  + " delay must be non-negative");
  }

  void validateSelectionBus(const std::string_view componentName, const Bus& selection)
  {
    if (selection.size() == 0)
      throw std::invalid_argument(std::string(componentName)
                                  + " selection bus must not be empty");
    if (selection.size() > MaxSelectionSize)
      throw std::invalid_argument(std::string(componentName)
                                  + " selection bus is too large");
  }

  struct MuxShapeProperties {
    int selectionSize;
    int busSize;
  };

  MuxShapeProperties
  checkedShapeProperties(const Component& component, const std::string_view componentName,
                         const std::optional<int> selectionSizeOverride = std::nullopt,
                         const std::optional<int> busSizeOverride       = std::nullopt)
  {
    // Property callbacks validate one changed value at a time; reshaping needs the
    // candidate value paired with the component's current value for the other axis.
    const int selectionSize = selectionSizeOverride.value_or(
        component.getPropertyValue<int>("selectionSize").value_or(1));
    const int busSize =
        busSizeOverride.value_or(component.getPropertyValue<int>("busSize").value_or(1));

    validateSelectionSize(componentName, selectionSize);
    validateBusSize(componentName, busSize);
    return {selectionSize, busSize};
  }

  void applyShape(Component& component, std::vector<Bus> inputs, std::vector<Bus> outputs)
  {
    // Keep input/output replacement paired so property reshapes cannot update only
    // one side of the component topology.
    // Note: setInputs/setOutputs take std::vector<Bus>&, so we pass lvalues directly
    component.setInputs(inputs);
    component.setOutputs(outputs);
  }

  template <typename InputStateForBit>
  State selectedIndexFromSelection(InputStateForBit     inputStateForBit,
                                   const unsigned short selectionSize,
                                   std::size_t&         selectedIndex)
  {
    // Selection bits are little-endian: bit 0 contributes the low bit of the index.
    // UNKNOWN/ERROR short-circuit because the selected lane cannot be known.
    selectedIndex = 0;
    for (unsigned short bit = 0; bit < selectionSize; ++bit) {
      const State selectionBit = inputStateForBit(bit);
      if (selectionBit == State::ERROR)
        return State::ERROR;
      if (selectionBit == State::UNKNOWN)
        return State::UNKNOWN;
      if (selectionBit == State::HIGH)
        selectedIndex |= std::size_t{1} << bit;
    }

    return State::LOW;
  }

  template <typename UpdateOutputBit>
  void updateAllOutputs(const std::size_t outputSize, UpdateOutputBit updateOutputBit,
                        const State state)
  {
    for (unsigned short bit = 0; bit < outputSize; ++bit)
      updateOutputBit(bit, state);
  }

  template <typename UpdateOutputBit>
  void updateAllOutputBuses(const std::vector<Bus>& outputs,
                            UpdateOutputBit updateOutputBit, const State state)
  {
    // Used by multi-bus demux paths where a single state must fan out over every
    // bit of every destination bus.
    for (std::size_t output = 0; output < outputs.size(); ++output)
      updateAllOutputs(
          outputs[output].size(),
          [output, &updateOutputBit](const unsigned short bit, const State bitState) {
            updateOutputBit(output, bit, bitState);
          },
          state);
  }

  bool propagatesAsBusState(const State state)
  {
    // ERROR and UNKNOWN describe the whole selected value, not just one data bit.
    return state == State::ERROR || state == State::UNKNOWN;
  }

  std::size_t multiplexerSelectionInputIndex(const int selectionSize, const int busSize)
  {
    // Packed one-bit muxes keep Selection at enum index 1; multi-bus muxes append
    // Selection after all selectable data buses.
    if (busSize == 1)
      return std::to_underlying(Multiplexer::Inputs::Selection);

    return dataBusCountForSelectionSize(selectionSize);
  }

  std::size_t demultiplexerSelectionInputIndex(const int busSize)
  {
    // Demux input count is stable, but this helper mirrors the mux shape helpers
    // and keeps bus-size-dependent selection lookup in one place.
    return busSize == 1 ? std::to_underlying(Demultiplexer::Inputs::Selection) : 1;
  }

  std::vector<Bus> resizedPorts(const std::vector<Bus>& currentPorts,
                                const std::size_t       portCount,
                                const unsigned short    portWidth)
  {
    // Preserve existing wire identities where possible so resizing does not discard
    // established connections for ports that still exist after the shape change.
    std::vector<Bus> newPorts(portCount);
    for (std::size_t port = 0; port < portCount; ++port) {
      if (port < currentPorts.size())
        newPorts[port] = currentPorts[port];
      newPorts[port].setSize(portWidth);
    }
    return newPorts;
  }

  std::vector<Bus> shapedMultiplexerInputs(const std::vector<Bus>& currentInputs,
                                           const int selectionSize, const int busSize)
  {
    std::vector<Bus> newInputs;

    // 1. Identify and safely isolate the selection bus to prevent accidental
    // data-lane duplication during up-sizing.
    Bus selectionBus;
    if (currentInputs.size() > 2) {
      selectionBus = currentInputs.back();
    } else if (currentInputs.size()
               > std::to_underlying(Multiplexer::Inputs::Selection)) {
      selectionBus = currentInputs[std::to_underlying(Multiplexer::Inputs::Selection)];
    }

    // 2. Handle packed mode (busSize == 1)
    if (busSize == 1) {
      newInputs.resize(2);
      if (!currentInputs.empty()) {
        newInputs[std::to_underlying(Multiplexer::Inputs::Data)] = currentInputs[0];
      }

      newInputs[std::to_underlying(Multiplexer::Inputs::Selection)] =
          std::move(selectionBus);
      newInputs[std::to_underlying(Multiplexer::Inputs::Data)].setSize(
          dataWidthForSelectionSize(selectionSize));
      newInputs[std::to_underlying(Multiplexer::Inputs::Selection)].setSize(
          static_cast<unsigned short>(selectionSize));
      return newInputs;
    }

    // 3. Handle multi-bus mode (busSize > 1)
    const std::size_t dataBusCount = dataBusCountForSelectionSize(selectionSize);
    newInputs.resize(dataBusCount + 1);

    // Copy only the old data lanes (avoid copying the old selection bus into a data lane)
    const std::size_t oldDataBusCount = currentInputs.size() > 2
                                            ? currentInputs.size() - 1
                                            : (currentInputs.size() == 2 ? 1 : 0);
    const std::size_t lanesToCopy     = std::min(dataBusCount, oldDataBusCount);

    for (std::size_t i = 0; i < lanesToCopy; ++i) {
      newInputs[i] = currentInputs[i];
    }
    for (std::size_t i = 0; i < dataBusCount; ++i) {
      newInputs[i].setSize(static_cast<unsigned short>(busSize));
    }

    // Place the selection bus safely at the final index
    newInputs[dataBusCount] = std::move(selectionBus);
    newInputs[dataBusCount].setSize(static_cast<unsigned short>(selectionSize));

    return newInputs;
  }

  std::vector<Bus> shapedMultiplexerOutputs(const std::vector<Bus>& currentOutputs,
                                            const int               busSize)
  {
    // A mux always has one output bus; busSize only changes its width.
    auto newOutputs = currentOutputs;
    if (newOutputs.empty())
      newOutputs.resize(1);
    newOutputs[std::to_underlying(Multiplexer::Outputs::Out)].setSize(
        static_cast<unsigned short>(busSize));
    return newOutputs;
  }

  std::vector<Bus> shapedDemultiplexerInputs(const std::vector<Bus>& currentInputs,
                                             const int selectionSize, const int busSize)
  {
    // A demux always has data plus selection inputs; busSize changes only the data
    // width while selectionSize changes only the selector width.
    auto newInputs = currentInputs;
    if (newInputs.size() < 2)
      newInputs.resize(2);
    newInputs[std::to_underlying(Demultiplexer::Inputs::Data)].setSize(
        static_cast<unsigned short>(busSize));
    newInputs[demultiplexerSelectionInputIndex(busSize)].setSize(
        static_cast<unsigned short>(selectionSize));
    return newInputs;
  }

  std::vector<Bus> shapedDemultiplexerOutputs(const std::vector<Bus>& currentOutputs,
                                              const int selectionSize, const int busSize)
  {
    if (busSize == 1) {
      // Single-bit demuxes expose the selectable destinations as bits of one bus.
      auto newOutputs = currentOutputs;
      newOutputs.resize(1);
      newOutputs[std::to_underlying(Demultiplexer::Outputs::Out)].setSize(
          dataWidthForSelectionSize(selectionSize));
      return newOutputs;
    }

    const std::size_t outputBusCount = dataBusCountForSelectionSize(selectionSize);
    // Multi-bit demuxes expose one output bus per selectable destination.
    return resizedPorts(currentOutputs, outputBusCount,
                        static_cast<unsigned short>(busSize));
  }
}  // namespace

Multiplexer::Multiplexer()
{
  defineProperty("selectionSize", 1);
  defineProperty("busSize", 1);
  defineProperty("delay", 0);

  setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    const int selectionSize = std::get<int>(value);
    validateSelectionSize("Multiplexer", selectionSize);

    if (!inputs.empty() && !outputs.empty())
      setSelectionSize(selectionSize);

    return value;
  });

  setPropertyCallback("busSize", [this](const PropertyValue& value) {
    const int busSize = std::get<int>(value);
    validateBusSize("Multiplexer", busSize);

    if (!inputs.empty() && !outputs.empty())
      setBusSize(busSize);

    return value;
  });

  setPropertyCallback("delay", [](const PropertyValue& value) {
    validateDelay("Multiplexer", value);
    return value;
  });
}

Multiplexer::Multiplexer(Bus data, Bus selection, Wire_ptr output) : Multiplexer()
{
  validateSelectionBus("Multiplexer", selection);

  const auto expectedDataWidth =
      dataWidthForSelectionSize(static_cast<int>(selection.size()));
  if (data.size() != expectedDataWidth)
    throw std::invalid_argument("Multiplexer data bus width must be 2^selection width");

  inputs  = {std::move(data), std::move(selection)};
  outputs = {{std::move(output)}};
  setProperty("selectionSize",
              static_cast<int>(inputs[busIndex(Inputs::Selection)].size()));
  setProperty("busSize", 1);
}

int Multiplexer::setSelectionSize(const int selectionSize)
{
  const auto shape = checkedShapeProperties(*this, "Multiplexer", selectionSize);
  applyShape(*this,
             shapedMultiplexerInputs(getInputs(), shape.selectionSize, shape.busSize),
             shapedMultiplexerOutputs(getOutputs(), shape.busSize));

  return selectionSize;
}

int Multiplexer::setBusSize(const int busSize)
{
  const auto shape = checkedShapeProperties(*this, "Multiplexer", std::nullopt, busSize);
  applyShape(*this,
             shapedMultiplexerInputs(getInputs(), shape.selectionSize, shape.busSize),
             shapedMultiplexerOutputs(getOutputs(), shape.busSize));

  return busSize;
}

void Multiplexer::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBuses().empty() || outputBusSize(Outputs::Out) == 0)
    return;

  // Note: Consider caching delay, busSize, and selectionSize in class members via
  // setPropertyCallback to avoid hash map lookups on the simulation hot path.
  const int         busSize         = getPropertyValue<int>("busSize").value_or(1);
  const int         propertySelSize = getPropertyValue<int>("selectionSize").value_or(1);
  const std::size_t selectionIndex =
      multiplexerSelectionInputIndex(propertySelSize, busSize);
  const auto selectionSize =
      inputBuses().size() > selectionIndex ? inputBuses()[selectionIndex].size() : 0;
  const auto dataSize = inputBuses().size() > busIndex(Inputs::Data)
                            ? inputBuses()[busIndex(Inputs::Data)].size()
                            : 0;
  const int  delay    = getPropertyValue<int>("delay").value_or(0);

  if (selectionSize == 0 || dataSize == 0) {
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        State::ERROR);
    return;
  }

  std::size_t selectedIndex  = 0;
  const State selectionState = selectedIndexFromSelection(
      [this, selectionIndex](const unsigned short bit) {
        return inputState(static_cast<unsigned int>(selectionIndex), bit);
      },
      selectionSize, selectedIndex);

  if (propagatesAsBusState(selectionState)) {
    // Ambiguous selection means every output bit is ambiguous; no data lane can
    // be chosen deterministically.
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        selectionState);
    return;
  }

  if (busSize == 1) {
    // Packed mode selects a bit from the data bus. Multi-bus mode below selects
    // an entire input bus and copies it to the output bus.
    const State selectedState =
        selectedIndex < dataSize
            ? inputState(Inputs::Data, static_cast<unsigned short>(selectedIndex))
            : State::ERROR;
    sim.updateWire(outputWire(Outputs::Out), selectedState, delay, weak_from_this());
    return;
  }

  for (unsigned short bit = 0; bit < outputBusSize(Outputs::Out); ++bit) {
    const State selectedState =
        selectedIndex < inputBuses().size() && bit < inputBuses()[selectedIndex].size()
            ? inputState(static_cast<unsigned int>(selectedIndex), bit)
            : State::ERROR;
    sim.updateWire(outputWire(Outputs::Out, bit), selectedState, delay, weak_from_this());
  }
}

Demultiplexer::Demultiplexer()
{
  defineProperty("selectionSize", 1);
  defineProperty("busSize", 1);
  defineProperty("delay", 0);

  setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    const int selectionSize = std::get<int>(value);
    validateSelectionSize("Demultiplexer", selectionSize);

    if (!inputs.empty() && !outputs.empty())
      setSelectionSize(selectionSize);

    return value;
  });

  setPropertyCallback("busSize", [this](const PropertyValue& value) {
    const int busSize = std::get<int>(value);
    validateBusSize("Demultiplexer", busSize);

    if (!inputs.empty() && !outputs.empty())
      setBusSize(busSize);

    return value;
  });

  setPropertyCallback("delay", [](const PropertyValue& value) {
    validateDelay("Demultiplexer", value);
    return value;
  });
}

Demultiplexer::Demultiplexer(Bus data, Bus selection, Bus output) : Demultiplexer()
{
  validateSelectionBus("Demultiplexer", selection);
  if (data.size() != 1)
    throw std::invalid_argument("Demultiplexer data bus must be single-bit");

  const auto expectedOutputWidth =
      dataWidthForSelectionSize(static_cast<int>(selection.size()));
  if (output.size() != expectedOutputWidth)
    throw std::invalid_argument(
        "Demultiplexer output bus width must be 2^selection width");

  inputs  = {std::move(data), std::move(selection)};
  outputs = {std::move(output)};
  setProperty("selectionSize",
              static_cast<int>(inputs[busIndex(Inputs::Selection)].size()));
  setProperty("busSize", 1);
}

int Demultiplexer::setSelectionSize(const int selectionSize)
{
  const auto shape = checkedShapeProperties(*this, "Demultiplexer", selectionSize);
  applyShape(
      *this, shapedDemultiplexerInputs(getInputs(), shape.selectionSize, shape.busSize),
      shapedDemultiplexerOutputs(getOutputs(), shape.selectionSize, shape.busSize));

  return selectionSize;
}

int Demultiplexer::setBusSize(const int busSize)
{
  const auto shape =
      checkedShapeProperties(*this, "Demultiplexer", std::nullopt, busSize);
  applyShape(
      *this, shapedDemultiplexerInputs(getInputs(), shape.selectionSize, shape.busSize),
      shapedDemultiplexerOutputs(getOutputs(), shape.selectionSize, shape.busSize));

  return busSize;
}

void Demultiplexer::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBuses().empty())
    return;

  const int         busSize        = getPropertyValue<int>("busSize").value_or(1);
  const std::size_t selectionIndex = demultiplexerSelectionInputIndex(busSize);
  const auto        selectionSize =
      inputBuses().size() > selectionIndex ? inputBuses()[selectionIndex].size() : 0;
  const int delay = getPropertyValue<int>("delay").value_or(0);

  if (selectionSize == 0) {
    if (busSize == 1) {
      updateAllOutputs(
          outputBusSize(Outputs::Out),
          [this, &sim, delay](const unsigned short bit, const State state) {
            sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
          },
          State::ERROR);
    } else {
      // With no usable selection bus, every bit of every destination bus is invalid.
      updateAllOutputBuses(
          outputBuses(),
          [this, &sim, delay](const std::size_t output, const unsigned short bit,
                              const State state) {
            sim.updateWire(outputWire(output, bit), state, delay, weak_from_this());
          },
          State::ERROR);
    }
    return;
  }

  std::size_t selectedIndex  = 0;
  const State selectionState = selectedIndexFromSelection(
      [this, selectionIndex](const unsigned short bit) {
        return inputState(static_cast<unsigned int>(selectionIndex), bit);
      },
      selectionSize, selectedIndex);

  if (propagatesAsBusState(selectionState)) {
    if (busSize == 1) {
      updateAllOutputs(
          outputBusSize(Outputs::Out),
          [this, &sim, delay](const unsigned short bit, const State state) {
            sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
          },
          selectionState);
    } else {
      // UNKNOWN/ERROR selection fans out to every destination because no single
      // selected output can be trusted.
      updateAllOutputBuses(
          outputBuses(),
          [this, &sim, delay](const std::size_t output, const unsigned short bit,
                              const State state) {
            sim.updateWire(outputWire(output, bit), state, delay, weak_from_this());
          },
          selectionState);
    }
    return;
  }

  if (busSize == 1) {
    const State dataState = inputState(Inputs::Data);
    for (unsigned short bit = 0; bit < outputBusSize(Outputs::Out); ++bit) {
      const State outputState =
          static_cast<std::size_t>(bit) == selectedIndex ? dataState : State::LOW;
      sim.updateWire(outputWire(Outputs::Out, bit), outputState, delay, weak_from_this());
    }
    return;
  }

  // Loop unswitched to prevent checking equality during hot bitwise iteration
  for (std::size_t output = 0; output < outputBuses().size(); ++output) {
    if (output == selectedIndex) {
      for (unsigned short bit = 0; bit < outputBuses()[output].size(); ++bit) {
        sim.updateWire(outputWire(output, bit), inputState(Inputs::Data, bit), delay,
                       weak_from_this());
      }
    } else {
      for (unsigned short bit = 0; bit < outputBuses()[output].size(); ++bit) {
        sim.updateWire(outputWire(output, bit), State::LOW, delay, weak_from_this());
      }
    }
  }
}

Decoder::Decoder()
{
  defineProperty("selectionSize", 1);
  defineProperty("delay", 0);

  setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    const int selectionSize = std::get<int>(value);
    validateSelectionSize("Decoder", selectionSize);

    if (!inputs.empty() && !outputs.empty())
      setSelectionSize(selectionSize);

    return value;
  });

  setPropertyCallback("delay", [](const PropertyValue& value) {
    validateDelay("Decoder", value);
    return value;
  });
}

Decoder::Decoder(Bus enable, Bus selection, Bus output) : Decoder()
{
  validateSelectionBus("Decoder", selection);
  if (enable.size() != 1)
    throw std::invalid_argument("Decoder enable bus must be single-bit");

  const auto expectedOutputWidth =
      dataWidthForSelectionSize(static_cast<int>(selection.size()));
  if (output.size() != expectedOutputWidth)
    throw std::invalid_argument("Decoder output bus width must be 2^selection width");

  inputs  = {std::move(enable), std::move(selection)};
  outputs = {std::move(output)};
  setProperty("selectionSize",
              static_cast<int>(inputs[busIndex(Inputs::Selection)].size()));
}

int Decoder::setSelectionSize(const int selectionSize)
{
  validateSelectionSize("Decoder", selectionSize);

  auto newInputs = getInputs();
  if (newInputs.size() < 2)
    newInputs.resize(2);
  if (newInputs[busIndex(Inputs::Enable)].size() != 1)
    newInputs[busIndex(Inputs::Enable)].setSize(1);
  newInputs[busIndex(Inputs::Selection)].setSize(
      static_cast<unsigned short>(selectionSize));

  auto newOutputs = getOutputs();
  if (newOutputs.empty())
    newOutputs.resize(1);
  newOutputs[busIndex(Outputs::Out)].setSize(dataWidthForSelectionSize(selectionSize));

  applyShape(*this, newInputs, newOutputs);

  return selectionSize;
}

void Decoder::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBusSize(Outputs::Out) == 0)
    return;

  const auto  selectionSize = inputBuses().size() > busIndex(Inputs::Selection)
                                  ? inputBuses()[busIndex(Inputs::Selection)].size()
                                  : 0;
  const int   delay         = getPropertyValue<int>("delay").value_or(0);
  const State enable        = inputState(Inputs::Enable);

  if (propagatesAsBusState(enable)) {
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        enable);
    return;
  }

  if (selectionSize == 0) {
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        State::ERROR);
    return;
  }

  if (enable == State::LOW) {
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        State::LOW);
    return;
  }

  std::size_t selectedIndex  = 0;
  const State selectionState = selectedIndexFromSelection(
      [this](const unsigned short bit) { return inputState(Inputs::Selection, bit); },
      selectionSize, selectedIndex);

  if (propagatesAsBusState(selectionState)) {
    updateAllOutputs(
        outputBusSize(Outputs::Out),
        [this, &sim, delay](const unsigned short bit, const State state) {
          sim.updateWire(outputWire(Outputs::Out, bit), state, delay, weak_from_this());
        },
        selectionState);
    return;
  }

  for (unsigned short bit = 0; bit < outputBusSize(Outputs::Out); ++bit) {
    const State outputState =
        (static_cast<std::size_t>(bit) == selectedIndex) ? State::HIGH : State::LOW;
    sim.updateWire(outputWire(Outputs::Out, bit), outputState, delay, weak_from_this());
  }
}

}  // namespace SILICON::extra
