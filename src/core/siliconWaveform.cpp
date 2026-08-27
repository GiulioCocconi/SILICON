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

#include "siliconWaveform.hpp"

#include <core/wire.hpp>
#include <utils/num_formatting.hpp>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace SILICON::waveform {

namespace {

  [[nodiscard]] std::vector<core::BusValue> defaultInputValues(const Trace& trace)
  {
    std::vector<core::BusValue> values;
    const auto                  inputCount =
        std::min<int>(trace.inputCount, static_cast<int>(trace.signalDefinitions.size()));
    values.reserve(static_cast<std::size_t>(inputCount));

    for (int i = 0; i < inputCount; ++i)
      values.emplace_back(signalWidth(trace, i), core::State::LOW);

    return values;
  }

  [[nodiscard]] std::vector<core::BusValue> valuesAt(const Trace& trace, uint64_t time)
  {
    auto values = defaultInputValues(trace);
    for (const auto& sample : trace.samples) {
      if (sample.time > time)
        break;
      values = sample.values;
    }
    return values;
  }

}  // namespace

void resetTrace(Trace& trace, std::vector<Signal> signalDefinitions, const int inputCount)
{
  trace.signalDefinitions = std::move(signalDefinitions);
  trace.samples.clear();
  trace.inputCount =
      std::clamp(inputCount, 0, static_cast<int>(trace.signalDefinitions.size()));

  for (auto& signal : trace.signalDefinitions)
    signal.width = std::max<std::size_t>(1, signal.width);
}

void appendSnapshot(Trace& trace, const uint64_t time, std::vector<core::BusValue> values)
{
  if (!trace.samples.empty() && trace.samples.back().time == time) {
    trace.samples.back().values = std::move(values);
    return;
  }

  trace.samples.push_back({time, std::move(values)});
}

void appendSnapshots(Trace& trace, std::span<const Sample> snapshots)
{
  trace.samples.reserve(trace.samples.size() + snapshots.size());
  for (const auto& [time, values] : snapshots)
    appendSnapshot(trace, time, values);
}

void clearSamples(Trace& trace)
{
  trace.samples.clear();
}

std::size_t signalWidth(const Trace& trace, const int signalIndex)
{
  if (signalIndex < 0 || signalIndex >= static_cast<int>(trace.signalDefinitions.size()))
    return 0;

  std::size_t width =
      trace.signalDefinitions[static_cast<std::size_t>(signalIndex)].width;
  for (const auto& sample : trace.samples) {
    if (signalIndex < static_cast<int>(sample.values.size())) {
      width =
          std::max(width, sample.values[static_cast<std::size_t>(signalIndex)].size());
    }
  }

  return std::max<std::size_t>(1, width);
}

void rebuildEditableTrace(Trace& trace, const uint64_t duration)
{
  trace.samples.clear();

  if (trace.inputCount <= 0)
    return;

  const auto defaultValues = defaultInputValues(trace);
  trace.samples.push_back({0, defaultValues});
  trace.samples.push_back({duration, defaultValues});
}

void applyEditInterval(Trace& trace, uint64_t duration, int signalIndex,
                       uint64_t startTime, uint64_t endTime,
                       const core::BusValue& rawValue)
{
  if (signalIndex < 0 || signalIndex >= trace.inputCount || endTime <= startTime)
    return;

  duration  = std::max<uint64_t>(1, duration);
  startTime = std::min(startTime, duration);
  endTime   = std::min(endTime, duration);
  if (endTime <= startTime)
    return;

  if (trace.samples.empty())
    rebuildEditableTrace(trace, duration);

  const auto startValues = valuesAt(trace, startTime);
  const auto endValues   = valuesAt(trace, endTime);

  auto ensureBoundary = [&](const uint64_t                     time,
                            const std::vector<core::BusValue>& values) {
    const auto it = std::ranges::lower_bound(trace.samples, time, {}, &Sample::time);
    if (it != trace.samples.end() && it->time == time) {
      it->values = values;
      return;
    }
    trace.samples.insert(it, {time, values});
  };

  ensureBoundary(startTime, startValues);
  ensureBoundary(endTime, endValues);

  for (auto& sample : trace.samples) {
    if (sample.time < startTime || sample.time >= endTime)
      continue;

    while (sample.values.size() < static_cast<std::size_t>(trace.inputCount))
      sample.values.push_back({core::State::LOW});
    sample.values[static_cast<std::size_t>(signalIndex)] = rawValue;
  }

  std::vector<Sample> compacted;
  compacted.reserve(trace.samples.size());
  for (const auto& sample : trace.samples) {
    if (!compacted.empty() && compacted.back().values == sample.values
        && sample.time != duration) {
      continue;
    }
    compacted.push_back(sample);
  }
  trace.samples = std::move(compacted);
}

std::vector<Sample> editedInputSamples(const Trace& trace)
{
  return trace.samples;
}

}  // namespace SILICON::waveform

namespace SILICON::waveform::fst {

using namespace SILICON::waveform;

void writeTrace(std::string_view fileName, const Trace& trace)
{
  writeTrace(fileName, trace, {.topScopeName = "Waveform"});
}

void writeTrace(std::string_view fileName, const Trace& trace,
                TraceWriter::Options options)
{
  if (trace.signalDefinitions.empty())
    throw std::runtime_error("no waveform signals are available");

  std::vector<TraceWriter::TraceSignal> traceSignals;
  traceSignals.reserve(trace.signalDefinitions.size());

  for (std::size_t i = 0; i < trace.signalDefinitions.size(); ++i) {
    traceSignals.push_back(
        {trace.signalDefinitions[i].name, signalWidth(trace, static_cast<int>(i))});
  }

  TraceWriter writer(fileName, traceSignals, std::move(options));

  for (const auto& [time, values] : trace.samples) {
    const auto strValues = values | std::views::transform([](const auto& el) {
                             return core::formatValue(el, BusValueFormat::Raw);
                           })
                           | std::ranges::to<std::vector<std::string>>();
    writer.emitSnapshot(time, strValues);
  }
  writer.flush();
}

}  // namespace SILICON::waveform::fst
