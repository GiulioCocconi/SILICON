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

#include "memory.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

#include <core/simulator.hpp>

namespace SILICON::core {
namespace {

  std::size_t bitCount(const std::size_t width, const std::size_t depth)
  {
    if (width == 0 || depth == 0
        || depth > std::numeric_limits<std::size_t>::max() / width)
      throw std::invalid_argument("Memory image geometry is invalid");
    return width * depth;
  }

  [[nodiscard]] std::size_t packedWordCount(const std::size_t bits,
                                            const std::size_t width)
  {
    return bits / width + static_cast<std::size_t>(bits % width != 0);
  }

  // Binary documents contain whole bytes; ROM geometry excludes byte padding.
  [[nodiscard]] bool matchesPackedImage(const std::size_t bytes, const std::size_t words,
                                        const std::size_t width)
  {
    if (words > std::numeric_limits<std::size_t>::max() / width)
      return false;
    const auto required = words * width;
    return bytes == required / 8 + (required % 8 != 0)
           || (bytes <= std::numeric_limits<std::size_t>::max() / 8
               && packedWordCount(bytes * 8, width) == words);
  }

  [[nodiscard]] unsigned short addressWidthFor(const std::size_t words)
  {
    if (words <= 1)
      return 1;
    return static_cast<unsigned short>(std::bit_width(words - 1));
  }

}  // namespace

MemoryImage::MemoryImage(Bits bits, const std::size_t wordWidth, const std::size_t depth)
  : data(std::move(bits)), width(wordWidth), words(depth)
{
  if (data.size() != bitCount(width, words))
    throw std::invalid_argument("Memory image bits do not match its geometry");
}

MemoryImage MemoryImage::fromBytes(const std::span<const std::uint8_t> bytes,
                                   const std::size_t width, const std::size_t depth)
{
  const auto count = bitCount(width, depth);
  if (bytes.size() > count / 8 + (count % 8 != 0))
    throw std::invalid_argument("Memory image contains too many bytes");
  Bits bits(bytes.begin(), bytes.end());
  bits.resize(count);
  return MemoryImage(std::move(bits), width, depth);
}

std::vector<std::uint8_t> MemoryImage::bytes() const
{
  std::vector<std::uint8_t> result(data.num_blocks());
  boost::to_block_range(data, result.begin());
  return result;
}

ROM::ROM()
{
  defineProperty("dataWidth", 8);
  defineProperty("binaryContents", std::string());

  setPropertyCallback("dataWidth", [this](const PropertyValue& value) {
    const int width = std::get<int>(requireValidSize("ROM dataWidth", value));
    if (!settingMemoryImage)
      configure(width, content, true, false,
                width == configuredDataWidth() ? imageDepth : 0);
    return value;
  });
  setPropertyCallback("binaryContents", [this](const PropertyValue& value) {
    const auto& slug = std::get<std::string>(value);
    if (!settingBinaryDocument && slug != configuredBinaryContents())
      configure(configuredDataWidth(), nullptr, false);
    return value;
  });
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

void ROM::configure(const int                                        dataWidth,
                    std::shared_ptr<const std::vector<std::uint8_t>> nextContent,
                    const bool rejectInvalid, const bool preferAddressDepth,
                    const std::size_t explicitWords)
{
  const int width = std::get<int>(requireValidSize("ROM dataWidth", PropertyValue{dataWidth}));

  bool        nextResolved = false;
  std::size_t nextWords    = 0;
  if (nextContent) {
    if (nextContent->size() > std::numeric_limits<std::size_t>::max() / 8)
      throw std::invalid_argument("ROM binary document is too large");
    nextWords = packedWordCount(nextContent->size() * 8, static_cast<std::size_t>(width));
    if (explicitWords != 0) {
      if (!std::has_single_bit(explicitWords)
          || !matchesPackedImage(nextContent->size(), explicitWords,
                                 static_cast<std::size_t>(width))) {
        if (rejectInvalid)
          throw std::invalid_argument("ROM image does not match its explicit word count");
        nextWords = 0;
      } else {
        nextWords = explicitWords;
      }
    } else if (preferAddressDepth && !nextContent->empty() && !inputs.empty()) {
      const auto& address = inputs[busIndex(Inputs::Address)];
      const auto addressBits = address.size();
      if (addressBits < std::numeric_limits<std::size_t>::digits) {
        const auto candidate = std::size_t{1} << addressBits;
        if (matchesPackedImage(nextContent->size(), candidate,
                               static_cast<std::size_t>(width)))
          nextWords = candidate;
      }
    }
    nextResolved = nextWords != 0 && std::has_single_bit(nextWords);
    if (nextWords != 0 && !nextResolved && rejectInvalid)
      throw std::invalid_argument(
          "ROM binaryContents must contain a power-of-two number of packed words");
  }

  auto nextImage = nextResolved
                       ? std::make_shared<const MemoryImage>(MemoryImage::fromBytes(
                             *nextContent, static_cast<std::size_t>(width), nextWords))
                       : nullptr;
  content        = std::move(nextContent);
  image          = std::move(nextImage);
  imageDepth     = explicitWords;
  reshapeBuses(width, resolvedWordCount());
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

void ROM::setBinaryDocument(std::string                                      slug,
                            std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  const int width = configuredDataWidth();

  // Validate the proposed snapshot before changing either the property or the
  // current contents, so an invalid user selection is transactional.
  if (contents) {
    if (contents->size() > std::numeric_limits<std::size_t>::max() / 8)
      throw std::invalid_argument("ROM binary document is too large");
    const auto words =
        packedWordCount(contents->size() * 8, static_cast<std::size_t>(width));
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
  configure(width, std::move(contents), true);
}

void ROM::refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  // Preserve saved geometry on hydration, but keep document replacement/resizing
  // consistent with a newly selected binary file.
  const auto depth =
      content && contents && content->size() != contents->size() ? 0 : imageDepth;
  configure(configuredDataWidth(), std::move(contents), false, true, depth);
}

void ROM::refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents,
                                const std::size_t                                words)
{
  configure(configuredDataWidth(), std::move(contents), true, false, words);
}

std::shared_ptr<const std::vector<std::uint8_t>>
ROM::binaryContentsSnapshot() const noexcept
{
  return content;
}

std::size_t ROM::resolvedWordCount() const noexcept
{
  return image ? image->depth() : 0;
}

std::size_t ROM::memoryDepth() const noexcept
{
  return image ? image->depth() : imageDepth;
}

void ROM::setMemoryImage(std::shared_ptr<const MemoryImage> nextImage)
{
  if (!nextImage || !std::has_single_bit(nextImage->depth())
      || nextImage->wordWidth() > std::numeric_limits<unsigned short>::max())
    throw std::invalid_argument("Invalid ROM image");
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(nextImage->bytes());
  settingMemoryImage = true;
  try {
    setProperty("dataWidth", static_cast<int>(nextImage->wordWidth()));
  } catch (...) {
    settingMemoryImage = false;
    throw;
  }
  settingMemoryImage = false;
  imageDepth         = nextImage->depth();
  image              = std::move(nextImage);
  content            = std::move(bytes);
  reshapeBuses(configuredDataWidth(), image->depth());
}

void ROM::setMemoryDepth(const std::size_t depth)
{
  if (depth != 0
      && (!std::has_single_bit(depth)
          || depth > std::numeric_limits<std::size_t>::max()
                         / static_cast<std::size_t>(configuredDataWidth())))
    throw std::invalid_argument("Invalid ROM depth");
  if (content)
    configure(configuredDataWidth(), content, true, false, depth);
  else
    imageDepth = depth;
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
  if (!image) {
    driveState(sim, State::UNKNOWN);
    return;
  }

  std::size_t address = 0;
  if (image->depth() > 1) {
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
    if (sourceBit >= image->bits().size())
      break;
    value[bit] = image->bits()[sourceBit] ? State::HIGH : State::LOW;
  }
  sim.updateBus(outputBuses()[busIndex(Outputs::Data)], value, 0, weak_from_this());
}

}  // namespace SILICON::core
