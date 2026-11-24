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

  // Explicit geometry accepts canonical byte padding or a partial final word.
  // Raw bytes alone cannot distinguish padding from additional logical words.
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

Memory::Memory(const int depth)
{
  defineProperty("dataWidth", 8);
  defineProperty("depth", depth);
  defineProperty("binaryContents", std::string());
  // setPropertyCallback eagerly invokes callbacks; defer geometry until the
  // concrete memory's constructor has initialized its port-specific properties.
  propertyCallbacks["dataWidth"] = [this](const PropertyValue& value) {
    configure(std::get<int>(value), getPropertyValue<int>("depth").value(),
              getPropertyValue<std::string>("binaryContents").value(), content, true);
    return value;
  };
  propertyCallbacks["depth"] = [this](const PropertyValue& value) {
    configure(configuredDataWidth(), std::get<int>(value),
              getPropertyValue<std::string>("binaryContents").value(), content, true);
    return value;
  };
  propertyCallbacks["binaryContents"] = [this](const PropertyValue& value) {
    const auto& slug = std::get<std::string>(value);
    if (slug != getPropertyValue<std::string>("binaryContents").value())
      configure(configuredDataWidth(), getPropertyValue<int>("depth").value(), slug,
                nullptr, false);
    return value;
  };
}

int Memory::configuredDataWidth() const
{
  return getPropertyValue<int>("dataWidth").value();
}

std::size_t Memory::memoryDepth() const noexcept
{
  const auto depth = getPropertyValue<int>("depth").value();
  return depth != 0 ? static_cast<std::size_t>(depth) : image ? image->depth() : 0;
}

void Memory::setMemoryDepth(const std::size_t depth)
{
  if (depth > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid memory depth");
  setProperty("depth", static_cast<int>(depth));
}

unsigned short Memory::addressWidth() const
{
  auto width = addressWidthFor(memoryDepth());
  // Explicit geometry may have a wider address bus, notably after Yosys import.
  if (getPropertyValue<int>("depth").value() != 0 && !inputs.empty())
    width = std::max(width, static_cast<unsigned short>(inputs[0].size()));
  return width;
}

void Memory::configure(const int width, const int depth, std::string slug,
                       std::shared_ptr<const std::vector<std::uint8_t>> contents,
                       const bool                                       rejectInvalid)
{
  (void)requireValidSize("Memory dataWidth", PropertyValue{width});
  (void)requireNonNegative("Memory depth", PropertyValue{depth});
  if (contents && contents->size() > std::numeric_limits<std::size_t>::max() / 8)
    throw std::invalid_argument("Memory binary document is too large");
  const auto count =
      depth != 0 ? static_cast<std::size_t>(depth)
      : contents ? packedWordCount(contents->size() * 8, static_cast<std::size_t>(width))
                 : 0;
  if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Memory depth is too large");
  if (count != 0)
    bitCount(static_cast<std::size_t>(width), count);

  std::shared_ptr<const MemoryImage> nextImage;
  if (contents && !contents->empty() && count != 0) {
    if (!matchesPackedImage(contents->size(), count, static_cast<std::size_t>(width))) {
      if (rejectInvalid)
        throw std::invalid_argument("Memory image does not match its geometry");
    } else {
      nextImage = std::make_shared<const MemoryImage>(
          MemoryImage::fromBytes(*contents, static_cast<std::size_t>(width), count));
    }
  } else if (contents && depth != 0 && rejectInvalid) {
    throw std::invalid_argument("Memory image does not match its geometry");
  }
  commit(width, depth, std::move(slug), std::move(contents), std::move(nextImage));
}

void Memory::commit(const int width, const int depth, std::string slug,
                    std::shared_ptr<const std::vector<std::uint8_t>> contents,
                    std::shared_ptr<const MemoryImage>               nextImage)
{
  // Publish properties before notifying IO observers, including from property callbacks.
  properties.at("dataWidth")      = width;
  properties.at("depth")          = depth;
  properties.at("binaryContents") = std::move(slug);
  content                         = std::move(contents);
  image                           = std::move(nextImage);
  reshapeBuses();
}

void Memory::setBinaryDocument(std::string                                      slug,
                               std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  configure(configuredDataWidth(), getPropertyValue<int>("depth").value(),
            std::move(slug), std::move(contents), true);
}

void Memory::refreshBinaryContents(
    std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  configure(configuredDataWidth(), getPropertyValue<int>("depth").value(),
            getPropertyValue<std::string>("binaryContents").value(), std::move(contents),
            false);
}

void Memory::setMemoryImage(std::shared_ptr<const MemoryImage> nextImage)
{
  if (!nextImage || nextImage->wordWidth() > std::numeric_limits<unsigned short>::max()
      || nextImage->depth() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid memory image");
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(nextImage->bytes());
  const auto width = static_cast<int>(nextImage->wordWidth());
  const auto depth = static_cast<int>(nextImage->depth());
  commit(width, depth, getPropertyValue<std::string>("binaryContents").value(),
         std::move(bytes), std::move(nextImage));
}

RAM::RAM() : Memory(1)
{
  defineProperty("readPorts", 1);
  setPropertyCallback("readPorts", [this](const PropertyValue& value) {
    (void)requireValidSize("RAM readPorts", value);
    properties.at("readPorts") = value;
    reshapeBuses();
    return value;
  });
  reshapeBuses();
}

std::size_t RAM::readPortCount() const
{
  return static_cast<std::size_t>(getPropertyValue<int>("readPorts").value());
}

void RAM::reshapeBuses()
{
  const auto readPorts   = readPortCount();
  const auto addressBits = addressWidth();
  const auto width       = static_cast<unsigned short>(configuredDataWidth());
  auto       newInputs   = inputs;
  newInputs.resize(readPorts + 4);
  for (std::size_t port = 0; port < readPorts; ++port)
    newInputs[readAddressIndex(port)].setSize(addressBits);
  newInputs[busIndex(Inputs::WriteAddress)].setSize(addressBits);
  newInputs[busIndex(Inputs::WriteData)].setSize(width);
  newInputs[busIndex(Inputs::WriteEnable)].setSize(width);
  newInputs[busIndex(Inputs::Clock)].setSize(1);
  auto newOutputs = outputs;
  newOutputs.resize(readPorts);
  for (auto& output : newOutputs)
    output.setSize(width);
  words.clear();
  setInputs(newInputs);
  setOutputs(newOutputs);
}

void RAM::initializeWords()
{
  if (!words.empty())
    return;
  if (memoryDepth() == 0)
    throw std::runtime_error("Automatic RAM depth requires a nonempty binary document");
  words.assign(memoryDepth(), BusValue(static_cast<std::size_t>(configuredDataWidth()),
                                       State::UNKNOWN));
  if (image)
    for (std::size_t address = 0; address < words.size(); ++address)
      words[address] = image->word(address);
}

void RAM::write(const BusValue& address, const BusValue& data, const BusValue& mask,
                const State clockEnable)
{
  previousWords.clear();
  std::size_t index = 0;
  const auto status = addressState(address, index);
  if (status == State::HIGH && index >= words.size())
    return;
  const auto first = status == State::HIGH ? index : 0;
  const auto end = status == State::HIGH ? index + 1 : words.size();
  for (auto word = first; word < end; ++word) {
    if (status != State::HIGH && !addressMatches(address, word))
      continue;
    bool saved = false;
    for (std::size_t bit = 0; bit < words[word].size(); ++bit) {
      if (mask[bit] == State::LOW)
        continue;
      auto enable = mask[bit] && clockEnable;
      if (status == State::ERROR)
        enable = State::ERROR;
      else if (status == State::UNKNOWN)
        enable = enable && State::UNKNOWN;
      const auto next = mergeWrite(words[word][bit], data[bit], enable);
      if (words[word][bit] != next && !saved) {
        previousWords.emplace_back(word, words[word]);
        saved = true;
      }
      words[word][bit] = next;
    }
  }
}

void RAM::simulate(SILICON::simulation::Simulator& sim,
                    const SILICON::simulation::Context& context)
{
  using Simulator = SILICON::simulation::Simulator;
  initializeWords();
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
  if (enable != State::LOW) {
    const auto address = inputBuses()[busIndex(Inputs::WriteAddress)].getCurrentValue();
    const auto data    = inputBuses()[busIndex(Inputs::WriteData)].getCurrentValue();
    const auto mask    = inputBuses()[busIndex(Inputs::WriteEnable)].getCurrentValue();
    if (sim.beginSequentialCapture(weak_from_this(), [this]() noexcept {
          for (auto& [address, value] : previousWords)
            words[address].swap(value);
          previousWords.clear();
        }))
      write(address, data, mask, enable);
  }

  // Storage changes immediately, as in Register. Staged output wires keep every
  // same-edge consumer on the old read data until the whole pass has sampled.
  // Recompute on every visit so asynchronous address changes still take effect.
  for (std::size_t port = 0; port < readPortCount(); ++port) {
    std::size_t address = 0;
    const auto  status =
        addressState(inputBuses()[readAddressIndex(port)].getCurrentValue(), address);
    const auto value =
        status == State::HIGH && address < words.size()
            ? words[address]
            : BusValue(static_cast<std::size_t>(configuredDataWidth()),
                       status == State::ERROR ? State::ERROR : State::UNKNOWN);
    sim.updateBus(outputBuses()[port], value, 0, weak_from_this());
  }
}

ROM::ROM() : Memory(0)
{
  reshapeBuses();
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

void ROM::reshapeBuses()
{
  auto newInputs = inputs;
  newInputs.resize(3);
  newInputs[busIndex(Inputs::Address)].setSize(addressWidth());
  newInputs[busIndex(Inputs::CS)].setSize(1);
  newInputs[busIndex(Inputs::OE)].setSize(1);
  auto newOutputs = outputs;
  newOutputs.resize(1);
  newOutputs[busIndex(Outputs::Data)].setSize(
      static_cast<unsigned short>(configuredDataWidth()));
  setInputs(newInputs);
  setOutputs(newOutputs);
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
