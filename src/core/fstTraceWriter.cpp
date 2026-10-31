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

#include "fstTraceWriter.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include <utils/ranges_wrapper.hpp>

namespace SILICON::waveform::fst {

using namespace SILICON::core;

namespace {

  uint32_t checkedFstWidth(const std::size_t width)
  {
    if (width > std::numeric_limits<uint32_t>::max())
      throw std::invalid_argument("FST signal width exceeds libfst limit");

    return static_cast<uint32_t>(width);
  }

}  // namespace

TraceWriter::TraceWriter(std::string_view                fileName,
                         const std::vector<TraceSignal>& signals)
  : TraceWriter(fileName, signals, Options{})
{
}

TraceWriter::TraceWriter(std::string_view                fileName,
                         const std::vector<TraceSignal>& traceSignals, Options options)
  : writer([&] {
      HierarchyBuilder builder(fileName);
      builder.setTimeScale(options.timescale);
      builder.setVersion(options.version);
      builder.setPackType(FST_WR_PT_LZ4);
      builder.setScope(FST_ST_VCD_MODULE,
                       options.topScopeName.empty() ? "SILICON" : options.topScopeName);

      registeredSignals.reserve(traceSignals.size());
      handlesBySignalName.reserve(traceSignals.size());

      for (const auto& [index, signal] : traceSignals | SILICON::views::enumerate) {
        if (signal.width == 0)
          continue;

        const auto handle = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT,
                                              checkedFstWidth(signal.width), signal.name);
        registeredSignals.push_back(
            RegisteredSignal{signal.name, static_cast<std::size_t>(index), signal.width,
                             handle, signal.errorMask});
        handlesBySignalName.try_emplace(signal.name, handle);
      }

      builder.upScope();
      return std::move(builder).finish();
    }())
{
}

void TraceWriter::emitSnapshot(const uint64_t                     time,
                               const std::span<const std::string> values)
{
  writer.emitTimeChange(time);
  auto errorMasks = nlohmann::json::object();

  for (const auto& signal : registeredSignals) {
    const std::string_view value = signal.valueIndex < values.size()
                                       ? std::string_view(values[signal.valueIndex])
                                       : std::string_view{};
    auto                   normalized = normalizeValue(value, signal.width);
    std::string            errors(signal.width, '0');
    for (std::size_t i = 0; i < normalized.size(); ++i) {
      if (normalized[i] == 'E') {
        normalized[i] = 'x';
        errors[i]     = '1';
      }
    }
    writer.emitValueChange(signal.handle, normalized);
    if (signal.errorMask)
      errorMasks[signal.name + "__error"] = errors;
  }

  if (!errorMasks.empty())
    writer.emitComment(
        nlohmann::json{{"silicon_error_masks", {{"time", time}, {"values", errorMasks}}}}
            .dump());
}

void TraceWriter::flush()
{
  writer.flush();
}

std::optional<fstHandle> TraceWriter::handleForSignal(std::string_view name) const
{
  const auto it = handlesBySignalName.find(name);
  if (it == handlesBySignalName.end())
    return std::nullopt;

  return it->second;
}

std::string TraceWriter::normalizeValue(std::string_view value, const std::size_t width)
{
  std::string normalized(value);
  if (normalized.empty())
    normalized = "x";

  for (char& ch : normalized) {
    if (ch == 'X')
      ch = 'x';
    else if (ch == 'Z')
      ch = 'z';
    else if (ch != '0' && ch != '1' && ch != 'x' && ch != 'z' && ch != 'E')
      ch = 'x';
  }

  if (normalized.size() < width)
    normalized.insert(normalized.begin(), width - normalized.size(), 'x');
  else if (normalized.size() > width)
    normalized.erase(0, normalized.size() - width);

  return normalized;
}

}  // namespace SILICON::waveform::fst
