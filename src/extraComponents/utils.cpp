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

#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <numeric>
#include <stdexcept>

#include <core/simulator.hpp>

#include <utils/ranges_wrapper.hpp>

namespace SILICON::extra {
using namespace SILICON::core;

namespace {
  unsigned int parseWidth(const std::string_view text, const bool allowZero = false)
  {
    unsigned int value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()
        || (!allowZero && value == 0)
        || value > std::numeric_limits<unsigned short>::max())
      throw std::invalid_argument("Invalid splitter/merger bit range or width");
    return value;
  }

  template <typename Callback> void parseList(std::string_view text, Callback callback)
  {
    while (!text.empty()) {
      const auto comma = text.find(',');
      callback(text.substr(0, comma));
      if (comma == std::string_view::npos)
        return;
      text.remove_prefix(comma + 1);
      if (text.empty())
        throw std::invalid_argument("Splitter/merger list has a trailing comma");
    }
  }

  std::vector<Bus> resizedBuses(const std::vector<Bus>&          buses,
                                const std::vector<unsigned int>& widths)
  {
    std::vector<Bus> result;
    result.reserve(widths.size());
    for (std::size_t i = 0; i < widths.size(); ++i)
      result.push_back(i < buses.size() && buses[i].size() == widths[i]
                           ? buses[i]
                           : Bus(static_cast<unsigned short>(widths[i])));
    return result;
  }
}  // namespace

WireSplitter::WireSplitter()
{
  initializeProperties();
}

WireSplitter::WireSplitter(Bus input, const std::vector<Bus>& outputs) : WireSplitter()
{
  this->inputs  = {std::move(input)};
  this->outputs = outputs;
  setProperty("size",
              this->inputs[0].size() == 0 ? 2 : static_cast<int>(this->inputs[0].size()));
}

std::vector<DocumentationConfiguration> WireSplitter::documentationConfigurations() const
{
  return {
      {"slices",
       "Bus slices",
       "Explicit offset:width pairs expose groups of bits instead of individual bits.",
       {{"size", 4}, {"slices", std::string("0:2,2:2")}}},
      {"overlapping",
       "Overlapping slices",
       "Slices may overlap: these outputs share bits 1 and 2 of the input word.",
       {{"size", 4}, {"slices", std::string("0:3,1:3")}}}};
}

void WireSplitter::initializeProperties()
{
  // Default-constructed components are used by the registry and must keep empty
  // buses. The size callback only reshapes I/O after real buses have been attached.
  static const auto sizeDefinition =
      propertyDefinition("size", 2,
                         "Source word length; explicit slices must fit inside it. With "
                         "no slices, expose every individual bit.",
                         {.minimum = 1, .maximum = MaxBusWidth});
  defineProperty(sizeDefinition);
  defineProperty(ZeroDelayProperty);
  static const auto slicesDefinition = propertyDefinition(
      "slices", std::string{},
      "Comma-separated offset:width slices of the input; empty splits individual bits.",
      {.description = "Each offset:width pair needs a nonnegative offset and a positive "
                      "width within "
                      "the input bus. Slices may overlap."});
  defineProperty(slicesDefinition);

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int newSize = std::get<int>(value);
    for (const auto& [offset, width] : slices)
      if (offset + width > static_cast<unsigned int>(newSize))
        throw std::invalid_argument("WireSplitter slice exceeds its input width");
    if (!this->inputs.empty())
      this->setSize(newSize);
    return value;
  });
  setPropertyCallback("slices", [this](const PropertyValue& value) {
    return setSlices(std::get<std::string>(value));
  });
}

int WireSplitter::setSize(int newSize)
{
  (void)propertyDeclaration("size").normalize(newSize);
  std::vector<unsigned int> outputWidths;
  if (slices.empty()) {
    outputWidths.assign(newSize, 1);
  } else {
    for (const auto& [offset, width] : slices) {
      if (offset + width > static_cast<unsigned int>(newSize))
        throw std::invalid_argument("WireSplitter slice exceeds its input width");
      outputWidths.push_back(width);
    }
  }
  setInputs(resizedBuses(inputs, {static_cast<unsigned int>(newSize)}));
  setOutputs(resizedBuses(outputs, outputWidths));

  return newSize;
}

std::string WireSplitter::setSlices(const std::string& specification)
{
  std::vector<std::pair<unsigned int, unsigned int>> parsed;
  const auto size = getPropertyValue<int>("size").value_or(2);
  parseList(specification, [&](const std::string_view item) {
    const auto colon = item.find(':');
    if (colon == std::string_view::npos)
      throw std::invalid_argument("WireSplitter slices use offset:width pairs");
    const auto offset = parseWidth(item.substr(0, colon), true);
    const auto width  = parseWidth(item.substr(colon + 1));
    if (offset + width > static_cast<unsigned int>(size))
      throw std::invalid_argument("WireSplitter slice exceeds its input width");
    parsed.emplace_back(offset, width);
  });
  slices = std::move(parsed);
  if (!inputs.empty())
    setSize(size);
  return specification;
}

std::vector<std::pair<unsigned int, unsigned int>> WireSplitter::outputSlices() const
{
  if (!slices.empty())
    return slices;
  std::vector<std::pair<unsigned int, unsigned int>> result;
  const auto size = inputs.empty() ? getPropertyValue<int>("size").value_or(2)
                                   : static_cast<int>(inputs[0].size());
  for (int i = 0; i < size; ++i)
    result.emplace_back(i, 1);
  return result;
}

void WireSplitter::simulate(SILICON::simulation::Simulator& sim)
{
  const int          delay = getPropertyValue<int>("delay").value();
  if (inputs.empty())
    return;
  const auto ranges = outputSlices();
  for (std::size_t port = 0; port < outputs.size() && port < ranges.size(); ++port)
    for (unsigned int bit = 0; bit < outputs[port].size() && bit < ranges[port].second;
         ++bit)
      sim.updateWire(outputs[port][bit],
                     Wire::safeGetCurrentState(inputs[0][ranges[port].first + bit]),
                     delay, weak_from_this());
}

WireMerger::WireMerger()
{
  initializeProperties();
}

WireMerger::WireMerger(const std::vector<Bus>& inputs, Bus output) : WireMerger()
{
  this->inputs  = inputs;
  this->outputs = {std::move(output)};
  setProperty("size", this->outputs[0].size() == 0
                          ? 2
                          : static_cast<int>(this->outputs[0].size()));
}

std::vector<DocumentationConfiguration> WireMerger::documentationConfigurations() const
{
  return {{"buses",
           "Mixed input widths",
           "Explicit widths concatenate inputs of different sizes, least-significant "
           "input first. Their widths must sum to size.",
           {{"size", 4}, {"widths", std::string("1,3")}}}};
}

void WireMerger::initializeProperties()
{
  // Default-constructed components are used by the registry and must keep empty
  // buses. The size callback only reshapes I/O after real buses have been attached.
  static const auto sizeDefinition =
      propertyDefinition("size", 2,
                         "Concatenated output length; explicit input widths must sum to "
                         "it. With no widths, use one input per output bit.",
                         {.minimum = 1, .maximum = MaxBusWidth});
  defineProperty(sizeDefinition);
  defineProperty(ZeroDelayProperty);
  static const auto widthsDefinition = propertyDefinition(
      "widths", std::string{},
      "Comma-separated input widths summing to size; empty merges individual bits.",
      {.description =
           "Nonempty lists must contain positive integer widths summing to size."});
  defineProperty(widthsDefinition);

  setPropertyCallback("size", [this](const PropertyValue& value) {
    const int newSize = std::get<int>(value);
    if (!widths.empty()
        && std::accumulate(widths.begin(), widths.end(), 0u)
               != static_cast<unsigned int>(newSize))
      throw std::invalid_argument("WireMerger input widths must sum to its output width");
    if (!this->outputs.empty())
      this->setSize(newSize);
    return value;
  });
  setPropertyCallback("widths", [this](const PropertyValue& value) {
    return setWidths(std::get<std::string>(value));
  });
}

std::vector<unsigned int> WireMerger::inputWidths(unsigned int size) const
{
  return widths.empty() ? std::vector<unsigned int>(size, 1) : widths;
}

int WireMerger::setSize(int newSize)
{
  (void)propertyDeclaration("size").normalize(newSize);
  if (!widths.empty()
      && std::accumulate(widths.begin(), widths.end(), 0u)
             != static_cast<unsigned int>(newSize))
    throw std::invalid_argument("WireMerger input widths must sum to its output width");
  setInputs(resizedBuses(inputs, inputWidths(newSize)));
  setOutputs(resizedBuses(outputs, {static_cast<unsigned int>(newSize)}));
  return newSize;
}

std::string WireMerger::setWidths(const std::string& specification)
{
  std::vector<unsigned int> parsed;
  parseList(specification,
            [&](const std::string_view item) { parsed.push_back(parseWidth(item)); });
  const auto size = getPropertyValue<int>("size").value_or(2);
  if (!parsed.empty()
      && std::accumulate(parsed.begin(), parsed.end(), std::uint64_t{0})
             != static_cast<unsigned int>(size))
    throw std::invalid_argument("WireMerger input widths must sum to its output width");
  widths = std::move(parsed);
  if (!outputs.empty())
    setSize(size);
  return specification;
}

void WireMerger::simulate(SILICON::simulation::Simulator& sim)
{
  const int          delay = getPropertyValue<int>("delay").value();
  if (outputs.empty())
    return;
  const auto   inputWidths = this->inputWidths(outputs[0].size());
  unsigned int offset = 0;
  for (std::size_t port = 0; port < inputWidths.size(); ++port)
    for (unsigned int bit = 0; bit < inputWidths[port]; ++bit, ++offset) {
      const State state =
          port < inputs.size() && bit < inputs[port].size() && inputs[port][bit]
              ? Wire::safeGetCurrentState(inputs[port][bit])
              : State::UNKNOWN;
      if (offset < outputs[0].size())
        sim.updateWire(outputs[0][offset], state, delay, weak_from_this());
    }
}

std::vector<PortDescription> WireSplitter::portDescriptions() const
{
  const auto size = static_cast<unsigned int>(getPropertyValue<int>("size").value());
  std::vector<PortDescription> result{
      {"b", PortDirection::Input, 0, size, "Bus to split, least-significant bit first."}};
  const auto ranges = outputSlices();
  for (auto [i, slice] : ranges | SILICON::views::enumerate) {
    const auto [offset, width] = slice;
    result.push_back({width == 1 ? std::format("b[{}]", offset)
                                 : std::format("b[{}:{}]", offset + width - 1, offset),
                      PortDirection::Output, static_cast<unsigned int>(i), width,
                      "Input slice starting at bit " + std::to_string(offset) + "."});
  }
  return result;
}

std::vector<PortDescription> WireMerger::portDescriptions() const
{
  const auto                   size        = outputs.empty()
                                                 ? static_cast<unsigned int>(getPropertyValue<int>("size").value())
                                                 : static_cast<unsigned int>(outputs[0].size());
  const auto                   inputWidths = this->inputWidths(size);
  std::vector<PortDescription> result;
  unsigned int                 offset = 0;
  for (auto [i, width] : inputWidths | SILICON::views::enumerate) {
    result.push_back(
        {width == 1 ? std::format("b[{}]", offset)
                    : std::format("b[{}:{}]", offset + width - 1, offset),
         PortDirection::Input, static_cast<unsigned int>(i), width,
         "Concatenated starting at output bit " + std::to_string(offset) + "."});
    offset += width;
  }
  result.push_back({"b", PortDirection::Output, 0, size,
                    "Concatenated bus, least-significant input first."});
  return result;
}

}  // namespace SILICON::extra
