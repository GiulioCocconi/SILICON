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
#include <core/wireUtils.hpp>

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

  // A known high bit beyond size_t cannot name an in-range word.
  [[nodiscard]] bool addressMatches(const BusValue& address, const std::size_t index)
  {
    for (std::size_t bit = 0; bit < address.size(); ++bit) {
      const auto expected = bit < std::numeric_limits<std::size_t>::digits
                                && ((index >> bit) & 1) != 0
                              ? State::HIGH : State::LOW;
      if (SILICON::wireUtils::isKnownBinary(address[bit]) && address[bit] != expected)
        return false;
    }
    return true;
  }

  [[nodiscard]] State addressState(const BusValue& address, std::size_t& index)
  {
    State result = State::HIGH;
    bool overflow = false;
    index = 0;
    for (std::size_t bit = 0; bit < address.size(); ++bit) {
      const auto state = address[bit];
      if (state == State::ERROR)
        return State::ERROR;
      if (state == State::UNKNOWN)
        result = State::UNKNOWN;
      if (state == State::HIGH) {
        if (bit >= std::numeric_limits<std::size_t>::digits)
          overflow = true;
        else
          index |= std::size_t{1} << bit;
      }
    }
    if (overflow) {
      index = std::numeric_limits<std::size_t>::max();
      return State::HIGH;
    }
    return result;
  }

  [[nodiscard]] State mergeWrite(const State old, const State data, const State enable)
  {
    if (enable == State::LOW)
      return old;
    if (enable == State::HIGH)
      return data;
    if (enable == State::ERROR || old == State::ERROR || data == State::ERROR)
      return State::ERROR;
    return old == data ? old : State::UNKNOWN;
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

BusValue MemoryImage::word(const std::size_t address) const
{
  BusValue result(width, State::UNKNOWN);
  if (address < words)
    for (std::size_t bit = 0; bit < width; ++bit)
      result[bit] = data[address * width + bit] ? State::HIGH : State::LOW;
  return result;
}

RAM::RAM()
{
  defineProperty("dataWidth", 8);
  defineProperty("depth", 1);
  defineProperty("binaryContents", std::string());
  setPropertyCallback("dataWidth", [this](const PropertyValue& value) {
    const auto width = std::get<int>(requireValidSize("RAM dataWidth", value));
    configure(width, memoryDepth(), content, true);
    return value;
  });
  setPropertyCallback("depth", [this](const PropertyValue& value) {
    const auto depth = std::get<int>(value);
    if (depth <= 0)
      throw std::invalid_argument("RAM depth must be positive");
    configure(configuredDataWidth(), static_cast<std::size_t>(depth), content, true);
    return value;
  });
  setPropertyCallback("binaryContents", [this](const PropertyValue& value) {
    if (std::get<std::string>(value)
        != getPropertyValue<std::string>("binaryContents").value_or(""))
      configure(configuredDataWidth(), memoryDepth(), nullptr, false);
    return value;
  });
  configure(8, 1, nullptr, false);
}

int RAM::configuredDataWidth() const
{
  return getPropertyValue<int>("dataWidth").value_or(8);
}

std::size_t RAM::memoryDepth() const
{
  return static_cast<std::size_t>(getPropertyValue<int>("depth").value_or(1));
}

void RAM::setMemoryDepth(const std::size_t depth)
{
  if (depth == 0 || depth > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid RAM depth");
  setProperty("depth", static_cast<int>(depth));
}

void RAM::configure(const int width, const std::size_t depth,
                    std::shared_ptr<const std::vector<std::uint8_t>> contents,
                    const bool rejectInvalid)
{
  const auto bits = bitCount(static_cast<std::size_t>(width), depth);
  std::shared_ptr<const MemoryImage> nextImage;
  if (contents) {
    if (contents->size() != bits / 8 + (bits % 8 != 0)) {
      if (rejectInvalid)
        throw std::invalid_argument("RAM image does not match its geometry");
      contents.reset();
    } else {
      nextImage = std::make_shared<const MemoryImage>(
          MemoryImage::fromBytes(*contents, static_cast<std::size_t>(width), depth));
    }
  }

  auto newInputs = inputs;
  newInputs.resize(5);
  newInputs[busIndex(Inputs::ReadAddress)].setSize(addressWidthFor(depth));
  newInputs[busIndex(Inputs::WriteAddress)].setSize(addressWidthFor(depth));
  newInputs[busIndex(Inputs::WriteData)].setSize(static_cast<unsigned short>(width));
  newInputs[busIndex(Inputs::WriteEnable)].setSize(static_cast<unsigned short>(width));
  newInputs[busIndex(Inputs::Clock)].setSize(1);
  auto newOutputs = outputs;
  newOutputs.resize(1);
  newOutputs[0].setSize(static_cast<unsigned short>(width));
  content = std::move(contents);
  image = std::move(nextImage);
  words.clear();
  setInputs(newInputs);
  setOutputs(newOutputs);
}

void RAM::setBinaryDocument(std::string slug,
                            std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  // Validate before changing either the reference or the cached image.
  const auto bits = bitCount(static_cast<std::size_t>(configuredDataWidth()), memoryDepth());
  if (contents && contents->size() != bits / 8 + (bits % 8 != 0))
    throw std::invalid_argument("RAM image does not match its geometry");
  setProperty("binaryContents", std::move(slug));
  refreshBinaryContents(std::move(contents));
}

void RAM::refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  // Hydration must not resize saved ports, including wider imported addresses.
  const auto bits = bitCount(static_cast<std::size_t>(configuredDataWidth()), memoryDepth());
  image.reset();
  content.reset();
  if (contents && contents->size() == bits / 8 + (bits % 8 != 0)) {
    image = std::make_shared<const MemoryImage>(MemoryImage::fromBytes(
        *contents, static_cast<std::size_t>(configuredDataWidth()), memoryDepth()));
    content = std::move(contents);
  }
  words.clear();
}

void RAM::setMemoryImage(std::shared_ptr<const MemoryImage> nextImage)
{
  if (!nextImage || nextImage->wordWidth() > std::numeric_limits<unsigned short>::max()
      || nextImage->depth() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid RAM image");
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(nextImage->bytes());
  // Geometry changes must not attempt to reinterpret the old initializer.
  content.reset();
  setProperty("dataWidth", static_cast<int>(nextImage->wordWidth()));
  setMemoryDepth(nextImage->depth());
  image = std::move(nextImage);
  content = std::move(bytes);
  words.clear();
}

void RAM::initializeWords()
{
  if (!words.empty())
    return;
  words.assign(memoryDepth(), BusValue(static_cast<std::size_t>(configuredDataWidth()),
                                       State::UNKNOWN));
  if (image)
    for (std::size_t address = 0; address < words.size(); ++address)
      words[address] = image->word(address);
}

bool RAM::write(const BusValue& address, const BusValue& data, const BusValue& mask,
                const State clockEnable)
{
  std::size_t index = 0;
  const auto status = addressState(address, index);
  if (status == State::HIGH && index >= words.size())
    return false;
  bool changed = false;
  const auto first = status == State::HIGH ? index : 0;
  const auto end = status == State::HIGH ? index + 1 : words.size();
  for (auto word = first; word < end; ++word) {
    if (status != State::HIGH && !addressMatches(address, word))
      continue;
    for (std::size_t bit = 0; bit < words[word].size(); ++bit) {
      if (mask[bit] == State::LOW)
        continue;
      auto enable = mask[bit] && clockEnable;
      if (status == State::ERROR)
        enable = State::ERROR;
      else if (status == State::UNKNOWN)
        enable = enable && State::UNKNOWN;
      const auto next = mergeWrite(words[word][bit], data[bit], enable);
      changed |= words[word][bit] != next;
      words[word][bit] = next;
    }
  }
  return changed;
}

void RAM::simulate(SILICON::simulation::Simulator& sim,
                    const SILICON::simulation::Context& context)
{
  using Simulator = SILICON::simulation::Simulator;
  initializeWords();
  std::size_t address = 0;
  const auto status = addressState(inputBuses()[busIndex(Inputs::ReadAddress)].getCurrentValue(),
                                    address);
  const auto value = status == State::HIGH && address < words.size()
                       ? words[address]
                       : BusValue(static_cast<std::size_t>(configuredDataWidth()),
                                  status == State::ERROR ? State::ERROR : State::UNKNOWN);
  sim.updateBus(outputBuses()[0], value, 0, weak_from_this());

  const auto clock = inputWire(Inputs::Clock);
  const auto edge = Simulator::edgeType(context, clock);
  State enable = State::LOW;
  if (edge == Simulator::EdgeType::RISE)
    enable = State::HIGH;
  else if (edge == Simulator::EdgeType::UNKNOWN) {
    const auto previous = context.previousState(clock);
    const auto current = inputState(Inputs::Clock);
    if (previous && SILICON::wireUtils::mayBe(*previous, State::LOW)
        && SILICON::wireUtils::mayBe(current, State::HIGH))
      enable = *previous == State::ERROR || current == State::ERROR
                   ? State::ERROR : State::UNKNOWN;
  }
  if (enable == State::LOW)
    return;

  const auto writeAddress = inputBuses()[busIndex(Inputs::WriteAddress)].getCurrentValue();
  const auto data = inputBuses()[busIndex(Inputs::WriteData)].getCurrentValue();
  const auto mask = inputBuses()[busIndex(Inputs::WriteEnable)].getCurrentValue();
  sim.stageSequentialUpdate(weak_from_this(), [this, writeAddress, data, mask, enable] {
    return write(writeAddress, data, mask, enable);
  });
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

  std::size_t nextWords = 0;
  if (nextContent) {
    if (nextContent->size() > std::numeric_limits<std::size_t>::max() / 8)
      throw std::invalid_argument("ROM binary document is too large");
    nextWords = packedWordCount(nextContent->size() * 8, static_cast<std::size_t>(width));
    if (explicitWords != 0) {
      if (!matchesPackedImage(nextContent->size(), explicitWords,
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
  }

  auto nextImage = nextWords != 0
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

  if (contents && contents->size() > std::numeric_limits<std::size_t>::max() / 8)
    throw std::invalid_argument("ROM binary document is too large");

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
  if (!nextImage || nextImage->wordWidth() > std::numeric_limits<unsigned short>::max())
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
  if (depth > std::numeric_limits<std::size_t>::max()
                  / static_cast<std::size_t>(configuredDataWidth()))
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
  const auto  addressSize = inputBuses().size() > busIndex(Inputs::Address)
                                ? inputBuses()[busIndex(Inputs::Address)].size()
                                : 0;
  for (std::size_t bit = 0; bit < addressSize; ++bit) {
    const State state = inputState(Inputs::Address, static_cast<unsigned short>(bit));
    if (state == State::ERROR || state == State::UNKNOWN) {
      driveState(sim, state);
      return;
    }
    if (state == State::HIGH) {
      if (bit >= std::numeric_limits<std::size_t>::digits) {
        driveState(sim, State::UNKNOWN);
        return;
      }
      address |= std::size_t{1} << bit;
    }
  }
  if (address >= image->depth()) {
    driveState(sim, State::UNKNOWN);
    return;
  }

  sim.updateBus(outputBuses()[busIndex(Outputs::Data)], image->word(address), 0,
                weak_from_this());
}

}  // namespace SILICON::core
