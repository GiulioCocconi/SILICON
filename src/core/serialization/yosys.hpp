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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/wire.hpp>

namespace SILICON::core {
class Circuit;
}

namespace SILICON::yosys {

using Json = nlohmann::ordered_json;

/** @brief Options controlling invocation of the external Yosys executable. */
struct ToolOptions {
  /** @brief Executable to invoke directly; when absent, `yosys` is found on PATH. */
  std::optional<std::filesystem::path> executable;
  /** @brief Override the directory containing the packaged SILICON Yosys library. */
  std::optional<std::filesystem::path> technologyLibraryDirectory;
};

/** @brief Output captured from one external Yosys invocation. */
struct ScriptResult {
  std::string standardOutput;
  std::string standardError;
};

/**
 * @brief Execute an arbitrary Yosys script and capture its output streams separately.
 *
 * The executable is invoked directly, without an intermediary command shell. This
 * operation is unavailable in Emscripten builds and throws on discovery, launch, or
 * script-execution failure.
 */
[[nodiscard]] ScriptResult runScript(std::string_view   script,
                                     const ToolOptions& options = {});

/**
 * @brief Lower one Verilog-2005 source string into a flattened Silicon circuit.
 *
 * @p topModule must be a simple Verilog identifier and selects the source module that
 * defines the subcircuit interface. SystemVerilog mode is not currently enabled.
 * Captured Yosys output is written through the `yosys` logger; failures refer callers
 * to those logs instead of embedding an arbitrarily large tool transcript.
 */
[[nodiscard]] core::Circuit importVerilog(std::string_view   source,
                                          std::string_view   topModule,
                                          const ToolOptions& options = {});

/**
 * @brief Convert a Silicon circuit to readable Verilog using external Yosys.
 *
 * The Yosys result is normalized to ANSI-style module port declarations. Redundant
 * `wire` declarations for those ports are removed, while genuine internal and shared
 * nets remain explicit. Two-input NAND and NOR gates use fused Yosys cells so their
 * generated expressions do not expose a meaningless operation-to-inverter net.
 * When the SILICON Yosys plugin is available, wide muxes are raised to combinational
 * processes so the backend emits readable case statements.
 */
[[nodiscard]] std::string exportVerilog(const core::Circuit& circuit,
                                        const ToolOptions&   options = {});

/**
 * @brief Mutable module-building interface passed to component serializers.
 *
 * Components use this context to translate their logical behavior into native
 * Yosys cells. Signal numbering, port netnames, and recursive module construction
 * remain owned by the circuit-level serializer.
 */
class SerializationContext {
public:
  struct Impl;

  /** @brief Convert a Silicon bus to a Yosys least-significant-bit-first vector. */
  [[nodiscard]] Json bits(const core::Bus& bus, std::string_view nullValue = "x") const;

  /** @brief Allocate module-local temporary signal bits. */
  [[nodiscard]] Json allocateBits(std::size_t width);

  /** @brief Concatenate bit vectors in their existing LSB-first order. */
  [[nodiscard]] static Json concatenate(const std::vector<Json>& vectors);

  /** @brief Encode an unsigned Yosys parameter as a fixed-width binary string. */
  [[nodiscard]] static std::string parameter(std::uint64_t value, std::size_t width = 32);

  /** @brief Emit one native Yosys cell for the current Silicon component. */
  void addCell(std::string_view suffix, std::string_view type, Json parameters,
               Json portDirections, Json connections);

  /** @brief Register a named top-level input or output port. */
  void addPort(std::string name, std::string_view direction, const core::Bus& bus);

  /** @brief Emit a hierarchical instance and recursively serialize its definition. */
  void addSubcircuitInstance(std::string_view slug, const std::vector<core::Bus>& inputs,
                             const std::vector<core::Bus>& outputs);

  /** @internal Constructed only by the circuit serializer. */
  explicit SerializationContext(Impl& impl) : impl(impl) {}

private:
  Impl& impl;

  friend std::string serialize(const core::Circuit& circuit);
};

/** @brief Serialize a complete Silicon circuit as Yosys write_json-compatible JSON. */
[[nodiscard]] std::string serialize(const core::Circuit& circuit);

/**
 * @brief Deserialize one supported Yosys write_json module into a Silicon circuit.
 *
 * When @p moduleName is absent, the sole module or unique module carrying the Yosys
 * `top` attribute is selected. Unsupported or ambiguous designs fail atomically.
 */
[[nodiscard]] core::Circuit
deserialize(std::string_view                json,
            std::optional<std::string_view> moduleName = std::nullopt);

}  // namespace SILICON::yosys
