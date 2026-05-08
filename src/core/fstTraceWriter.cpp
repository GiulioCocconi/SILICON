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

#include <utils/ranges_wrapper.hpp>

namespace {

uint32_t checkedFstWidth(const std::size_t width)
{
  if (width > std::numeric_limits<uint32_t>::max())
    throw std::invalid_argument("FST signal width exceeds libfst limit");

  return static_cast<uint32_t>(width);
}

}  // namespace

FstTraceWriter::FstTraceWriter(const std::string& fileName,
                               const std::vector<TraceSignal>& signals)
  : FstTraceWriter(fileName, signals, Options{})
{
}

FstTraceWriter::FstTraceWriter(const std::string& fileName,
                               const std::vector<TraceSignal>& traceSignals,
                               Options options)
  : writer([&] {
      FstHierarchyBuilder builder(fileName);
      builder.setTimeScale(options.timescale);
      builder.setVersion(options.version);
      builder.setPackType(FST_WR_PT_LZ4);
      builder.setScope(FST_ST_VCD_MODULE,
                       options.topScopeName.empty() ? "SILICON" : options.topScopeName);

      registeredSignals.reserve(traceSignals.size());
      handlesBySignalName.reserve(traceSignals.size());

      for (const auto& [index, signal] : traceSignals | silicon::views::enumerate) {
        if (signal.width == 0)
          continue;

        const auto handle = builder.createVar(FST_VT_VCD_WIRE, FST_VD_IMPLICIT,
                                              checkedFstWidth(signal.width),
                                              signal.name);
        registeredSignals.push_back(RegisteredSignal{
            signal.name, static_cast<std::size_t>(index), signal.width, handle});
        handlesBySignalName.try_emplace(signal.name, handle);
      }

      builder.upScope();
      return std::move(builder).finish();
    }())
{
}

void FstTraceWriter::emitSnapshot(const uint64_t time,
                                  const std::span<const std::string> values)
{
  writer.emitTimeChange(time);

  for (const auto& signal : registeredSignals) {
    const std::string_view value =
        signal.valueIndex < values.size() ? std::string_view(values[signal.valueIndex])
                                          : std::string_view{};
    writer.emitValueChange(signal.handle, normalizeValue(value, signal.width));
  }
}

void FstTraceWriter::flush()
{
  writer.flush();
}

std::optional<fstHandle> FstTraceWriter::handleForSignal(std::string_view name) const
{
  const auto it = handlesBySignalName.find(std::string(name));
  if (it == handlesBySignalName.end())
    return std::nullopt;

  return it->second;
}

std::string FstTraceWriter::normalizeValue(std::string_view value,
                                           const std::size_t width)
{
  std::string normalized(value);
  if (normalized.empty())
    normalized = "x";

  for (char& ch : normalized) {
    if (ch != '0' && ch != '1' && ch != 'x' && ch != 'z')
      ch = 'x';
  }

  if (normalized.size() < width)
    normalized.insert(normalized.begin(), width - normalized.size(), 'x');
  else if (normalized.size() > width)
    normalized.erase(0, normalized.size() - width);

  return normalized;
}
