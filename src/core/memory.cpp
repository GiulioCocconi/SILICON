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
    if (width == 0 || width > std::numeric_limits<unsigned short>::max() || depth == 0
        || depth > std::numeric_limits<std::size_t>::max() / width)
      throw std::invalid_argument("Memory image geometry is invalid");
    if (depth > Memory::MaxDepth || width * depth > Memory::MaxCapacityBits)
      throw std::invalid_argument(
          "Memory geometry exceeds the supported allocation limit");
    return width * depth;
  }

  void validatePackedBytes(const std::span<const std::uint8_t> bytes,
                           const std::size_t                   capacity)
  {
    if (bytes.size() > capacity / 8 + (capacity % 8 != 0))
      throw std::invalid_argument("Binary document exceeds memory capacity");
    if (capacity % 8 != 0 && bytes.size() == capacity / 8 + 1
        && (bytes.back() >> (capacity % 8)) != 0)
      throw std::invalid_argument(
          "Unused high bits of the final padding byte must be zero");
  }

  [[nodiscard]] std::size_t packedWordCount(const std::size_t bits,
                                            const std::size_t width)
  {
    return bits / width + static_cast<std::size_t>(bits % width != 0);
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
      const auto expected =
          bit < std::numeric_limits<std::size_t>::digits && ((index >> bit) & 1) != 0
              ? State::HIGH
              : State::LOW;
      if (SILICON::wireUtils::isKnownBinary(address[bit]) && address[bit] != expected)
        return false;
    }
    return true;
  }

  [[nodiscard]] State addressState(const BusValue& address, std::size_t& index)
  {
    State result   = State::HIGH;
    bool  overflow = false;
    index          = 0;
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
  : MemoryImage(std::move(bits), wordWidth, depth, bitCount(wordWidth, depth))
{
}

MemoryImage::MemoryImage(Bits bits, const std::size_t wordWidth, const std::size_t depth,
                         const std::size_t knownPrefixBits)
  : data(std::move(bits)), width(wordWidth), words(depth), knownBits(knownPrefixBits)
{
  if (data.size() != bitCount(width, words) || knownBits > data.size())
    throw std::invalid_argument("Memory image bits do not match its geometry");
}

MemoryImage MemoryImage::fromBytes(const std::span<const std::uint8_t> bytes,
                                   const std::size_t width, const std::size_t depth,
                                   const bool unknownPadding)
{
  const auto count = bitCount(width, depth);
  validatePackedBytes(bytes, count);
  Bits       bits(bytes.begin(), bytes.end());
  const auto known = unknownPadding ? std::min(count, bits.size()) : count;
  bits.resize(count);
  return MemoryImage(std::move(bits), width, depth, known);
}

std::vector<std::uint8_t> MemoryImage::bytes() const
{
  // Unknown tails are not encoded as binary zeroes in project documents.
  std::vector<std::uint8_t> result(data.num_blocks());
  boost::to_block_range(data, result.begin());
  result.resize(knownBits / 8 + (knownBits % 8 != 0));
  if (knownBits % 8 != 0)
    result.back() &= static_cast<std::uint8_t>((1u << (knownBits % 8)) - 1);
  return result;
}

BusValue MemoryImage::word(const std::size_t address) const
{
  BusValue result(width, State::UNKNOWN);
  if (address < words)
    for (std::size_t bit = 0; bit < width; ++bit) {
      const auto index = address * width + bit;
      if (index < knownBits)
        result[bit] = data[index] ? State::HIGH : State::LOW;
    }
  return result;
}

Memory::Memory()
{
  defineStringListProperty("type", std::string("ROM"), {"ROM", "RAM"});
  defineProperty("readPorts", 1);
  defineProperty("addressWidth", 1);
  defineProperty("dataWidth", 8);
  defineProperty("depthOverride", 0);
  defineProperty("binaryContents", std::string());
  defineStringListProperty(
      "initialization", std::string(Uninitialized),
      {std::string(Uninitialized), std::string(Zero), std::string(BinaryDocument)});
  defineStringListProperty("padding", std::string(Zero),
                           {std::string(Zero), std::string(Unknown)});
  for (const auto key :
       {"type", "readPorts", "addressWidth", "dataWidth", "depthOverride",
        "binaryContents", "initialization", "padding"}) {
    propertyCallbacks[key] = [this, key](const PropertyValue& value) {
      auto next        = properties;
      next.at(key)     = value;
      auto nextContent = content;
      if (std::string_view(key) == "binaryContents" && value != properties.at(key)) {
        nextContent.reset();
        next.at("initialization") = std::string(
            std::get<std::string>(value).empty() ? Uninitialized : BinaryDocument);
      }
      const auto geometryEdit = std::string_view(key) == "addressWidth"
                                || std::string_view(key) == "dataWidth"
                                || std::string_view(key) == "depthOverride";
      commit(std::move(next), std::move(nextContent), geometryEdit);
      return value;
    };
  }
  reshapeBuses();
}

void Memory::setProperties(const PropertyMap& values)
{
  auto next = properties;
  for (const auto& [key, value] : values) {
    const auto it = properties.find(key);
    if (it == properties.end())
      throw std::invalid_argument("Unknown memory property: " + key);
    validatePropertyValue(key, it->second, value);
    next.at(key) = value;
  }
  const auto contents =
      next.at("binaryContents") == properties.at("binaryContents") ? content : nullptr;
  // Restore configuration without selecting/resizing a document. External image
  // changes may leave initialization unresolved; execution/export diagnose that.
  commit(std::move(next), contents, false);
}

std::vector<PropertyInfo> Memory::propertyInfo() const
{
  std::vector<PropertyInfo> result;
  const bool                binary =
      getPropertyValue<std::string>("initialization").value() == BinaryDocument;
  for (const auto key : {"type", "addressWidth", "dataWidth", "readPorts",
                         "initialization", "binaryContents", "padding"}) {
    if (!binary
        && (std::string_view(key) == "binaryContents"
            || std::string_view(key) == "padding"))
      continue;
    if (const auto value = getProperty(key))
      result.push_back({key, *value});
  }
  result.push_back({"effectiveDepth",
                    std::to_string(memoryDepth()) + " words",
                    "Effective depth",
                    {},
                    true});
  result.push_back({"capacity",
                    std::to_string(capacityBits()) + " bits ("
                        + std::to_string((capacityBits() + 7) / 8) + " bytes)",
                    "Capacity",
                    {},
                    true});
  if (const auto error = initializationError(); !error.empty())
    result.push_back({"initializationStatus", error, "Initialization status", {}, true});
  result.push_back({"depthOverride", *getProperty("depthOverride"), "Depth override",
                    "0: use 2^addressWidth words. Positive values override depth and "
                    "must fit within the address space.",
                    false, getPropertyValue<int>("depthOverride").value() == 0});
  return result;
}

int Memory::configuredDataWidth() const
{
  return getPropertyValue<int>("dataWidth").value();
}

unsigned short Memory::addressWidth() const
{
  return static_cast<unsigned short>(getPropertyValue<int>("addressWidth").value());
}

std::size_t Memory::memoryDepth() const noexcept
{
  const auto depth = getPropertyValue<int>("depthOverride").value();
  return depth != 0 ? static_cast<std::size_t>(depth) : std::size_t{1} << addressWidth();
}

std::size_t Memory::capacityBits() const noexcept
{
  return memoryDepth() * static_cast<std::size_t>(configuredDataWidth());
}

void Memory::commit(PropertyMap                                      next,
                    std::shared_ptr<const std::vector<std::uint8_t>> contents,
                    const bool                                       rejectInvalid)
{
  const auto width = std::get<int>(next.at("dataWidth"));
  const auto abits = std::get<int>(next.at("addressWidth"));
  const auto depth = std::get<int>(next.at("depthOverride"));
  (void)requireValidSize("Memory dataWidth", PropertyValue{width});
  (void)requireValidSize("Memory addressWidth", PropertyValue{abits});
  (void)requireNonNegative("Memory depth override", PropertyValue{depth});
  if (depth == 0 && abits >= static_cast<int>(std::numeric_limits<std::size_t>::digits))
    throw std::invalid_argument(
        "Derived memory depth overflows; use an explicit depth override");
  const auto count =
      depth != 0 ? static_cast<std::size_t>(depth) : std::size_t{1} << abits;
  const auto capacity = bitCount(width, count);
  if (abits < static_cast<int>(std::numeric_limits<std::size_t>::digits)
      && count > (std::size_t{1} << abits))
    throw std::invalid_argument("Memory depth override exceeds the address space");
  (void)requireValidSize("Memory readPorts", next.at("readPorts"));
  const auto ports   = static_cast<std::size_t>(std::get<int>(next.at("readPorts")));
  const auto perPort = static_cast<std::size_t>(abits) + width;
  if (ports == 0 || ports > MaxPortBits / perPort
      || ports * perPort + abits + 2 * width + 2 > MaxPortBits)
    throw std::invalid_argument("Memory ports exceed the supported allocation limit");

  const auto& policy  = std::get<std::string>(next.at("initialization"));
  const auto& padding = std::get<std::string>(next.at("padding"));
  std::shared_ptr<const MemoryImage> nextImage;
  if (policy == Zero) {
    nextImage =
        std::make_shared<const MemoryImage>(MemoryImage::Bits(capacity), width, count);
  } else if (policy == BinaryDocument && contents) {
    try {
      nextImage = std::make_shared<const MemoryImage>(
          MemoryImage::fromBytes(*contents, width, count, padding == Unknown));
    } catch (const std::invalid_argument&) {
      if (rejectInvalid)
        throw;
    }
  }
  // Keep property entries stable: Component::setProperty retains an iterator while
  // invoking callbacks. All validation/allocation happens before publishing changes.
  for (auto& [key, value] : next)
    properties.at(key) = std::move(value);
  content = std::move(contents);
  image   = std::move(nextImage);
  reshapeBuses();
}

void Memory::setGeometry(const int width, const int abits, const int depthOverride)
{
  auto next                = properties;
  next.at("dataWidth")     = width;
  next.at("addressWidth")  = abits;
  next.at("depthOverride") = depthOverride;
  commit(std::move(next), content, true);
}

void Memory::setBinaryDocument(std::string                                      slug,
                               std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  auto next                 = properties;
  next.at("initialization") = std::string(slug.empty() ? Uninitialized : BinaryDocument);
  next.at("binaryContents") = std::move(slug);
  if (std::get<std::string>(next.at("initialization")) == BinaryDocument && contents) {
    if (contents->size() > MaxCapacityBits / 8)
      throw std::invalid_argument(
          "Binary document exceeds the supported allocation limit");
    try {
      validatePackedBytes(*contents, capacityBits());
    } catch (const std::invalid_argument&) {
      const auto words = packedWordCount(contents->size() * 8, configuredDataWidth());
      next.at("addressWidth") =
          static_cast<int>(std::max(addressWidth(), addressWidthFor(words)));
      // A user selection may extend an explicit override; hydration/import never do.
      if (std::get<int>(next.at("depthOverride")) != 0)
        next.at("depthOverride") = static_cast<int>(words);
    }
  }
  commit(std::move(next), std::move(contents), true);
}

void Memory::refreshBinaryContents(
    std::shared_ptr<const std::vector<std::uint8_t>> contents)
{
  commit(properties, std::move(contents), false);
}

std::string Memory::initializationError() const
{
  if (getPropertyValue<std::string>("initialization").value() != BinaryDocument)
    return {};
  if (!content)
    return "Binary initialization document is missing";
  if (!image)
    return "Memory geometry mismatch: binary initialization exceeds capacity or has "
           "nonzero padding bits. Increase address width or select a smaller image.";
  return {};
}

void Memory::validateInitialization() const
{
  if (const auto error = initializationError(); !error.empty())
    throw std::runtime_error(error);
}

void Memory::setMemoryImage(std::shared_ptr<const MemoryImage> nextImage)
{
  if (!nextImage || nextImage->wordWidth() > std::numeric_limits<unsigned short>::max()
      || nextImage->depth() > MaxDepth || nextImage->bits().size() > MaxCapacityBits)
    throw std::invalid_argument("Invalid memory image");
  if (nextImage->knownBitCount() != nextImage->bits().size()
      && nextImage->knownBitCount() % 8 != 0)
    throw std::invalid_argument(
        "Binary initialization requires a byte-aligned known prefix");
  auto next                = properties;
  next.at("dataWidth")     = static_cast<int>(nextImage->wordWidth());
  next.at("depthOverride") = static_cast<int>(nextImage->depth());
  next.at("addressWidth") =
      static_cast<int>(std::max(addressWidth(), addressWidthFor(nextImage->depth())));
  next.at("initialization") = std::string(BinaryDocument);
  next.at("padding")        = std::string(
      nextImage->knownBitCount() == nextImage->bits().size() ? Zero : Unknown);
  auto bytes = std::make_shared<const std::vector<std::uint8_t>>(nextImage->bytes());
  commit(std::move(next), std::move(bytes), true);
  image = std::move(nextImage);
}

bool Memory::isWritable() const
{
  return getPropertyValue<std::string>("type").value() == "RAM";
}

std::size_t Memory::readPortCount() const
{
  return static_cast<std::size_t>(getPropertyValue<int>("readPorts").value());
}

void Memory::reshapeBuses()
{
  const auto       readPorts   = readPortCount();
  const auto       addressBits = addressWidth();
  const auto       width       = static_cast<unsigned short>(configuredDataWidth());
  const auto       oldReadPorts = outputs.size();
  std::vector<Bus> newInputs(readPorts + (isWritable() ? 4 : 0));
  for (std::size_t port = 0; port < readPorts; ++port) {
    auto& address = newInputs[port];
    if (port < oldReadPorts)
      address = inputs[port];
    address.setSize(addressBits);
  }
  if (isWritable()) {
    for (const auto input : {WriteInputs::Address, WriteInputs::Data, WriteInputs::Enable,
                             WriteInputs::Clock}) {
      if (shapedWritable)
        newInputs[writeInputIndex(input)] = inputs[oldReadPorts + busIndex(input)];
    }
    newInputs[writeInputIndex(WriteInputs::Address)].setSize(addressBits);
    newInputs[writeInputIndex(WriteInputs::Data)].setSize(width);
    newInputs[writeInputIndex(WriteInputs::Enable)].setSize(width);
    newInputs[writeInputIndex(WriteInputs::Clock)].setSize(1);
  }
  shapedWritable  = isWritable();
  auto newOutputs = outputs;
  newOutputs.resize(readPorts);
  for (auto& output : newOutputs)
    output.setSize(width);
  // Release runtime storage too when switching a RAM back to ROM.
  words         = std::vector<BusValue>();
  previousWords = std::vector<std::pair<std::size_t, BusValue>>();
  setInputs(newInputs);
  setOutputs(newOutputs);
}

void Memory::initializeWords()
{
  if (!words.empty())
    return;
  words.assign(memoryDepth(),
               BusValue(static_cast<std::size_t>(configuredDataWidth()), State::UNKNOWN));
  if (image)
    for (std::size_t address = 0; address < words.size(); ++address)
      words[address] = image->word(address);
}

BusValue Memory::readWord(const std::size_t address) const
{
  if (isWritable())
    return words[address];
  if (image)
    return image->word(address);
  return BusValue(static_cast<std::size_t>(configuredDataWidth()), State::UNKNOWN);
}

void Memory::write(const BusValue& address, const BusValue& data, const BusValue& mask,
                   const State clockEnable)
{
  previousWords.clear();
  std::size_t index  = 0;
  const auto  status = addressState(address, index);
  if (status == State::HIGH && index >= words.size())
    return;
  const auto first = status == State::HIGH ? index : 0;
  const auto end   = status == State::HIGH ? index + 1 : words.size();
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

void Memory::simulate(SILICON::simulation::Simulator&     sim,
                      const SILICON::simulation::Context& context)
{
  using Simulator = SILICON::simulation::Simulator;
  validateInitialization();
  if (isWritable()) {
    initializeWords();
    const auto clock  = inputWire(writeInputIndex(WriteInputs::Clock));
    const auto edge   = Simulator::edgeType(context, clock);
    State      enable = State::LOW;
    if (edge == Simulator::EdgeType::RISE)
      enable = State::HIGH;
    else if (edge == Simulator::EdgeType::UNKNOWN) {
      const auto previous = context.previousState(clock);
      const auto current  = inputState(writeInputIndex(WriteInputs::Clock));
      if (previous && SILICON::wireUtils::mayBe(*previous, State::LOW)
          && SILICON::wireUtils::mayBe(current, State::HIGH))
        enable = *previous == State::ERROR || current == State::ERROR ? State::ERROR
                                                                      : State::UNKNOWN;
    }
    if (enable != State::LOW) {
      const auto address =
          inputBuses()[writeInputIndex(WriteInputs::Address)].getCurrentValue();
      const auto data =
          inputBuses()[writeInputIndex(WriteInputs::Data)].getCurrentValue();
      const auto mask =
          inputBuses()[writeInputIndex(WriteInputs::Enable)].getCurrentValue();
      if (sim.beginSequentialCapture(weak_from_this(), [this]() noexcept {
            for (auto& [address, value] : previousWords)
              words[address].swap(value);
            previousWords.clear();
          }))
        write(address, data, mask, enable);
    }
  }

  // Storage changes immediately, as in Register. Staged output wires keep every
  // same-edge consumer on the old read data until the whole pass has sampled.
  // Recompute on every visit so asynchronous address changes still take effect.
  for (std::size_t port = 0; port < readPortCount(); ++port) {
    std::size_t address = 0;
    const auto  status =
        addressState(inputBuses()[readAddressIndex(port)].getCurrentValue(), address);
    const auto value =
        status == State::HIGH && address < memoryDepth()
            ? readWord(address)
            : BusValue(static_cast<std::size_t>(configuredDataWidth()),
                       status == State::ERROR ? State::ERROR : State::UNKNOWN);
    sim.updateBus(outputBuses()[port], value, 0, weak_from_this());
  }
}

}  // namespace SILICON::core
