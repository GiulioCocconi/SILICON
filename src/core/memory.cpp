/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "memory.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

#include <core/simulator.hpp>

namespace SILICON::core {
namespace {

  [[nodiscard]] std::size_t packedWordCount(const std::size_t bytes,
                                            const std::size_t width)
  {
    if (bytes == 0)
      return 0;
    if (bytes > std::numeric_limits<std::size_t>::max() / 8)
      throw std::invalid_argument("ROM binaryContents is too large");
    const std::size_t bits = bytes * 8;
    return bits / width + static_cast<std::size_t>(bits % width != 0);
  }

  [[nodiscard]] unsigned short addressWidthFor(const std::size_t words)
  {
    if (words <= 1)
      return 1;
    return static_cast<unsigned short>(std::bit_width(words - 1));
  }

}  // namespace

ROM::ROM()
{
  defineProperty("dataWidth", 8);
  defineProperty("binaryContents", std::string());

  setPropertyCallback("dataWidth", [this](const PropertyValue& value) {
    const int width = std::get<int>(requireValidSize("ROM dataWidth", value));
    configure(width, content, true, !initializingProperties);
    return value;
  });
  setPropertyCallback("binaryContents", [this](const PropertyValue& value) {
    const auto& slug = std::get<std::string>(value);
    if (!settingBinaryDocument && slug != configuredBinaryContents())
      configure(configuredDataWidth(), nullptr, false, !initializingProperties);
    return value;
  });
  initializingProperties = false;
}

ROM::ROM(Bus address, Wire_ptr chipSelect, Wire_ptr outputEnable, Bus data) : ROM()
{
  if (address.size() == 0)
    throw std::invalid_argument("ROM address bus must not be empty");
  if (data.size() == 0)
    throw std::invalid_argument("ROM data bus must not be empty");

  inputs  = {std::move(address), {std::move(chipSelect)}, {std::move(outputEnable)}};
  outputs = {std::move(data)};
  setProperty("dataWidth", static_cast<int>(outputs[0].size()));
}

int ROM::configuredDataWidth() const
{
  return getPropertyValue<int>("dataWidth").value_or(8);
}

std::string ROM::configuredBinaryContents() const
{
  return getPropertyValue<std::string>("binaryContents").value_or(std::string());
}

void ROM::configure(const int dataWidth, std::shared_ptr<const std::string> nextContent,
                    const bool rejectInvalid, const bool reshape)
{
  const int width = std::get<int>(requireValidSize("ROM dataWidth", PropertyValue{dataWidth}));

  bool        nextResolved = false;
  std::size_t nextWords    = 0;
  if (nextContent) {
    nextWords    = packedWordCount(nextContent->size(), static_cast<std::size_t>(width));
    nextResolved = nextWords != 0 && std::has_single_bit(nextWords);
    if (nextWords != 0 && !nextResolved && rejectInvalid)
      throw std::invalid_argument(
          "ROM binaryContents must contain a power-of-two number of packed words");
  }

  content         = std::move(nextContent);
  contentResolved = nextResolved;
  wordCount       = nextResolved ? nextWords : 0;
  if (reshape)
    reshapeBuses(width, wordCount);
}

void ROM::reshapeBuses(const int dataWidth, const std::size_t words)
{
  auto newInputs = inputs;
  newInputs.resize(3);
  newInputs[busIndex(Inputs::Address)].setSize(addressWidthFor(words));
  newInputs[busIndex(Inputs::CS)].setSize(1);
  newInputs[busIndex(Inputs::OE)].setSize(1);
  setInputs(newInputs);

  auto newOutputs = outputs;
  newOutputs.resize(1);
  newOutputs[busIndex(Outputs::Data)].setSize(static_cast<unsigned short>(dataWidth));
  setOutputs(newOutputs);
}

void ROM::setBinaryDocument(std::string slug, std::shared_ptr<const std::string> contents)
{
  const int width = configuredDataWidth();

  // Validate the proposed snapshot before changing either the property or the
  // current contents, so an invalid user selection is transactional.
  if (contents) {
    const std::size_t words =
        packedWordCount(contents->size(), static_cast<std::size_t>(width));
    if (words != 0 && !std::has_single_bit(words))
      throw std::invalid_argument(
          "ROM binaryContents must contain a power-of-two number of packed words");
  }

  settingBinaryDocument = true;
  try {
    setProperty("binaryContents", slug);
  } catch (...) {
    settingBinaryDocument = false;
    throw;
  }
  settingBinaryDocument = false;
  configure(width, std::move(contents), true, true);
}

void ROM::refreshBinaryContents(std::shared_ptr<const std::string> contents)
{
  configure(configuredDataWidth(), std::move(contents), false, true);
}

std::shared_ptr<const std::string> ROM::binaryContentsSnapshot() const noexcept
{
  return content;
}

void ROM::driveState(SILICON::simulation::Simulator& sim, const State state)
{
  for (unsigned short bit = 0; bit < outputBusSize(Outputs::Data); ++bit)
    sim.updateWire(outputWire(Outputs::Data, bit), state, 0, weak_from_this());
}

void ROM::simulate(SILICON::simulation::Simulator& sim)
{
  if (outputBusSize(Outputs::Data) == 0)
    return;

  const State chipSelect = inputState(Inputs::CS);
  if (chipSelect == State::LOW)
    return;
  if (chipSelect == State::UNKNOWN || chipSelect == State::ERROR) {
    driveState(sim, chipSelect);
    return;
  }
  if (!contentResolved || wordCount == 0) {
    driveState(sim, State::UNKNOWN);
    return;
  }

  std::size_t address = 0;
  if (wordCount > 1) {
    const auto addressSize = inputBuses().size() > busIndex(Inputs::Address)
                                 ? inputBuses()[busIndex(Inputs::Address)].size()
                                 : 0;
    for (std::size_t bit = 0; bit < addressSize; ++bit) {
      const State state = inputState(Inputs::Address, static_cast<unsigned short>(bit));
      if (state == State::ERROR) {
        driveState(sim, State::ERROR);
        return;
      }
      if (state == State::UNKNOWN) {
        driveState(sim, State::UNKNOWN);
        return;
      }
      if (state == State::HIGH)
        address |= std::size_t{1} << bit;
    }
  }

  const std::size_t dataWidth = static_cast<std::size_t>(configuredDataWidth());
  BusValue          value(dataWidth, State::LOW);
  const std::size_t firstBit = address * dataWidth;
  for (std::size_t bit = 0; bit < dataWidth; ++bit) {
    const std::size_t sourceBit = firstBit + bit;
    const std::size_t byteIndex = sourceBit / 8;
    if (!content || byteIndex >= content->size())
      break;
    const auto byte = static_cast<unsigned char>((*content)[byteIndex]);
    value[bit]      = (byte & (1U << (sourceBit % 8))) != 0 ? State::HIGH : State::LOW;
  }
  sim.updateBus(outputBuses()[busIndex(Outputs::Data)], value, 0, weak_from_this());
}

}  // namespace SILICON::core
