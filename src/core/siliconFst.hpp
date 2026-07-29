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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/circuit.hpp>
#include <core/fstTraceWriter.hpp>

namespace SILICON::waveform::fst {

using namespace SILICON::core;

/**
 * @class CircuitWriter
 * @brief SILICON-aware adapter over the generic FST writer wrapper.
 *
 * The low-level FST wrapper exposes libfst concepts such as scopes, variables, and
 * handles. This class lifts that API to SILICON concepts by registering circuit buses as
 * vector signals and emitting snapshots from the buses' current simulation states.
 */
class CircuitWriter final {
public:
  /**
   * @brief Bus registered under a stable trace signal name.
   */
  using NamedBus = std::pair<std::string, Bus>;

  /**
   * @brief FST file metadata and hierarchy options shared with TraceWriter.
   */
  using Options = TraceWriter::Options;

  CircuitWriter(std::string_view fileName, const Circuit& circuit);
  CircuitWriter(std::string_view fileName, const Circuit& circuit, Options options);
  CircuitWriter(std::string_view fileName, const std::vector<NamedBus>& buses);
  CircuitWriter(std::string_view fileName, const std::vector<NamedBus>& buses,
                   Options options);

  CircuitWriter(const CircuitWriter&)            = delete;
  CircuitWriter& operator=(const CircuitWriter&) = delete;

  CircuitWriter(CircuitWriter&&) noexcept            = default;
  CircuitWriter& operator=(CircuitWriter&&) noexcept = default;

  /**
   * @brief Emits all registered bus states at the given simulation timestamp.
   */
  void emitSnapshot(uint64_t time);

  /**
   * @brief Flushes pending FST data so external tools can read recent snapshots.
   */
  void flush();

  /**
   * @brief Returns the FST handle associated with a registered bus name, if present.
   */
  [[nodiscard]] std::optional<fstHandle> handleForBus(std::string_view name) const;

  /**
   * @brief Number of circuit buses registered in the FST hierarchy.
   */
  [[nodiscard]] std::size_t busCount() const { return buses.size(); }

  /**
   * @brief Maps SILICON states to VCD/FST scalar symbols.
   */
  [[nodiscard]] static char stateToFstValue(State state);

private:
  std::vector<NamedBus> buses;
  TraceWriter        writer;
};

}  // namespace SILICON::waveform::fst
