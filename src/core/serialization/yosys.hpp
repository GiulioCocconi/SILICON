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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>

#include <nlohmann/json.hpp>

#include <core/wire.hpp>

namespace SILICON::core {
class Circuit;
class Component;
}  // namespace SILICON::core

namespace SILICON::yosys {

using Json = nlohmann::ordered_json;

/**
 * @brief Module-instantiation dependency graph for a Yosys JSON design.
 *
 * Vertices are declared module names. Edges point from an instantiating module to the
 * declared modules it instantiates; primitive and external cell types are not included.
 * The graph is derived from a Yosys `write_json` document and is never serialized.
 *
 * The graph uses `boost::vecS` for its vertex container, which means vertex descriptors
 * are integer indices. As a consequence, removing a vertex reindexes every subsequent
 * vertex, so the `nameToVertex` index must be rebuilt after a removal. Name lookup
 * remains average O(1) thanks to the `std::unordered_map` index.
 */
class ModuleDependencyGraph {
public:
  /// @brief List of module names.
  using ModuleNameList = std::vector<std::string>;

  /// @brief Per-vertex payload holding the declared module name.
  struct VertexProperty {
    std::string name;
  };

  /// @brief Boost graph type: set out-edges, vector vertices, directed.
  using Graph =
      boost::adjacency_list<boost::setS, boost::vecS, boost::directedS, VertexProperty>;
  /// @brief Boost vertex descriptor (an integer index under `vecS`).
  using Vertex = boost::graph_traits<Graph>::vertex_descriptor;

  /// @brief O(1) index mapping a module name to its graph vertex.
  using NameToVertex = std::unordered_map<std::string, Vertex>;

  /**
   * @brief Ensures a module vertex exists, creating it if necessary.
   *
   * @param moduleName Declared module name to register.
   */
  void addModule(std::string_view moduleName);

  /**
   * @brief Records that @p from instantiates @p to.
   *
   * Both modules are ensured as vertices first. Duplicate edges are ignored, so
   * dependencies remain unique.
   *
   * @param from Instantiating module name.
   * @param to   Instantiated declared module name.
   */
  void addDependency(std::string_view from, std::string_view to);

  /**
   * @brief Returns the sorted, unique modules @p moduleName instantiates.
   *
   * @param moduleName Declared module name to query.
   * @return The dependency names, sorted lexicographically; empty if the module is
   *         unknown or has no dependencies.
   */
  [[nodiscard]] ModuleNameList dependenciesOf(std::string_view moduleName) const;

  /**
   * @brief Returns the unique dependency closure of @p roots in dependency-first order.
   *
   * Explicit roots are included in the result. Shared dependencies occur once, and
   * otherwise-independent names are ordered deterministically.
   *
   * @throw std::invalid_argument If a requested root is unknown.
   * @throw std::runtime_error If the selected hierarchy is recursive.
   */
  [[nodiscard]] ModuleNameList dependencyOrder(const ModuleNameList& roots) const;

  /**
   * @brief Returns every declared module name.
   *
   * @return The module names, sorted lexicographically; empty if the graph is empty.
   */
  [[nodiscard]] ModuleNameList modules() const;

  /**
   * @brief Checks whether a module vertex is present in the graph.
   *
   * @param moduleName Declared module name to look up.
   * @return `true` if the module is registered.
   */
  [[nodiscard]] bool containsModule(std::string_view moduleName) const;

  /// @brief Read-only access to the underlying Boost graph.
  [[nodiscard]] const Graph& boost() const { return graph; }

private:
  /// @brief The dependency graph itself.
  Graph graph;
  /// @brief Name-to-vertex index kept in sync with @ref graph.
  NameToVertex nameToVertex;

  [[nodiscard]] std::optional<Vertex> findVertex(std::string_view moduleName) const;

  /**
   * @brief Ensures a module vertex exists in this graph (O(1)).
   *
   * @param moduleName Declared module name.
   * @return The descriptor of the existing or newly created vertex.
   */
  Vertex ensureVertex(std::string_view moduleName);

  /**
   * @brief Rebuilds @ref nameToVertex from the current @ref graph.
   *
   * Required after `remove_vertex` because `vecS` reindexes descriptors.
   */
  void rebuildNameIndex();
};

/**
 * @brief Build a module dependency graph from a Yosys write_json document.
 *
 * Edges point from an instantiating module to the declared modules it instantiates.
 * Malformed documents throw `std::runtime_error` with an `Invalid Yosys JSON` prefix.
 */
[[nodiscard]] ModuleDependencyGraph moduleDependencyGraph(std::string_view json);

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

/** @brief One named Verilog source made available to the preprocessor. */
struct VerilogSourceFile {
  /** @brief Safe relative path used for include resolution and diagnostics. */
  std::string_view path;
  /** @brief Source contents written to the temporary Yosys workspace. */
  std::string_view contents;
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
 * @brief Lower one Verilog-2005 source string into raw Yosys write_json.
 *
 * This is the only operation that consumes Verilog, and it performs no design
 * elaboration: `read_verilog` is parsed and `proc` converts processes to
 * `$dff`/`$mux` cells so the JSON backend can emit them, then the result is
 * written to JSON. The result retains the declared module hierarchy and unlowered
 * cells. Every downstream transformation (`elaborateHierarchy`,
 * `deserialize`, `exportVerilog`) takes Yosys JSON and re-enters Yosys through
 * `read_json` rather than reading Verilog again. Captured Yosys output is written
 * through the `yosys` logger; failures refer callers to those logs instead of
 * embedding an arbitrarily large tool transcript.
 */
[[nodiscard]] std::string readVerilog(std::string_view   source,
                                      const ToolOptions& options = {});

/**
 * @brief Lower one entry file from a set of named Verilog sources into Yosys JSON.
 *
 * Every source is materialized in the same temporary workspace, preserving its
 * relative path, so Yosys can resolve transitive `include` directives. Only @p entryPath
 * is passed to `read_verilog`; files that are not included remain outside the design.
 *
 * @throws std::invalid_argument If paths are unsafe or duplicated, or if @p entryPath
 * does not name one of @p sources.
 */
[[nodiscard]] std::string readVerilog(std::span<const VerilogSourceFile> sources,
                                      std::string_view                   entryPath,
                                      const ToolOptions&                 options = {});

/**
 * @brief Elaborate a Yosys JSON design for import.
 *
 * Starts with `read_json` and lowers the design while preserving its module
 * hierarchy: procedural blocks are converted, the SILICON technology library is
 * mapped, and full/half adders are extracted, but module instances are kept
 * hierarchical so each module becomes a subcircuit. Use this to prepare a
 * multi-module design for dependency discovery or hierarchical import.
 */
[[nodiscard]] std::string elaborateHierarchy(std::string_view   json,
                                             const ToolOptions& options = {});

/**
 * @brief Convert Yosys write_json into readable structural Verilog using Yosys.
 *
 * This is the inverse of `readVerilog` and operates entirely on the Yosys JSON
 * transport format. The Yosys result is normalized to ANSI-style module port
 * declarations. Redundant `wire` declarations for those ports are removed, while
 * genuine internal and shared nets remain explicit. SILICON technology cells are
 * lowered to standard behavioral Verilog, so the result has no dependency on
 * SILICON's Yosys cell library. Two-input NAND and NOR gates use fused Yosys cells so
 * their generated expressions do not expose a meaningless operation-to-inverter net.
 * When the SILICON Yosys plugin is available, wide muxes are raised to combinational
 * processes so the backend emits readable case statements.
 */
[[nodiscard]] std::string exportVerilog(std::string_view     json,
                                        const ToolOptions& options = {});

/**
 * @brief Convert a Silicon circuit to readable Verilog using external Yosys.
 *
 * Serializes the circuit to Yosys JSON and then lowers it with `exportVerilog(json)`.
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

  /**
   * @brief Encode a component input, applying its declared unconnected-input default.
   * @param component Component that owns the input
   * @param index Input bus index
   * @param expectedWidth Required bus width
   */
  [[nodiscard]] Json inputBits(const core::Component& component, std::size_t index,
                               std::size_t expectedWidth) const;

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
 * `top` attribute is selected. Input must already use the canonical cells supported
 * by the direct importer. Cells naming another non-blackbox module in the same design
 * become project subcircuit placeholders. Synthesis patterns such as equality banks are
 * normalized by `readVerilog` when the SILICON Yosys plugin is available. Unsupported
 * or ambiguous designs fail atomically.
 */
[[nodiscard]] core::Circuit
deserialize(std::string_view                json,
            std::optional<std::string_view> moduleName = std::nullopt);

}  // namespace SILICON::yosys
