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

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/fstTraceWriter.hpp>
#include <core/wire.hpp>

namespace SILICON::waveform {

struct Signal {
  std::string name;
  std::size_t width = 1;
};

struct Sample {
  uint64_t                 time = 0;
  std::vector<core::BusValue> values;
};

struct Trace {
  std::vector<Signal> signalDefinitions;
  std::vector<Sample> samples;
  int                 inputCount = 0;
};

void resetTrace(Trace& trace, std::vector<Signal> signalDefinitions, int inputCount);

void appendSnapshot(Trace& trace, uint64_t time, std::vector<core::BusValue> values);

void appendSnapshots(Trace& trace, std::span<const Sample> samples);

void clearSamples(Trace& trace);

[[nodiscard]] std::size_t signalWidth(const Trace& trace, int signalIndex);

void rebuildEditableTrace(Trace& trace, uint64_t duration);

void applyEditInterval(Trace& trace, uint64_t duration, int signalIndex,
                       uint64_t startTime, uint64_t endTime,
                       const core::BusValue& rawValue);

[[nodiscard]] std::vector<Sample> editedInputSamples(const Trace& trace);

}  // namespace SILICON::waveform

namespace SILICON::waveform::fst {

void writeTrace(std::string_view fileName, const waveform::Trace& trace);

void writeTrace(std::string_view fileName, const waveform::Trace& trace,
                TraceWriter::Options options);

}  // namespace SILICON::waveform::fst
