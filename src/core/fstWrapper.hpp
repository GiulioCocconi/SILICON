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

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fstapi.h>

namespace SILICON::waveform::fst {

// --- Shared types ----------------------------------------------------------------------

/**
 * @brief Custom deleter used with std::unique_ptr for fstReaderContext.
 *
 * The libfst API is C-based and exposes manual lifetime management through
 * fstReaderOpen()/fstReaderClose(). Wrapping the raw pointer in a unique_ptr
 * with a custom deleter gives strict RAII semantics:
 *
 *   - construction => resource acquired
 *   - destruction  => resource released automatically
 *
 * This completely eliminates accidental leaks and makes move semantics trivial.
 */
struct ReaderDeleter {
  void operator()(fstReaderContext* ctx) const
  {
    if (ctx)
      fstReaderClose(ctx);
  }
};

/**
 * @brief Custom deleter used with std::unique_ptr for fstWriterContext.
 *
 * Same rationale as ReaderDeleter above.
 */
struct WriterDeleter {
  void operator()(fstWriterContext* ctx) const
  {
    if (ctx)
      fstWriterClose(ctx);
  }
};

// =======================================================================================
// Reader
// =======================================================================================

/**
 * @class Reader
 * @brief High-level zero-overhead wrapper around libfst reader APIs.
 *
 * The existence of an Reader instance guarantees that the FST file
 * is successfully opened and the underlying reader context is valid.
 */
class Reader final {
public:
  /**
   * @brief Decoded enum table extracted from libfst.
   *
   * FST stores enum mappings as string-encoded metadata. The utility parser
   * allocates temporary structures internally, so this wrapper copies the data
   * into STL-owned containers before releasing the libfst allocation.
   */
  struct EnumTable {
    std::string name;

    /**
     * Mapping format:
     *
     *   literal -> encoded value
     *
     * Example:
     *
     *   "IDLE" -> "00"
     *   "BUSY" -> "01"
     */
    std::vector<std::pair<std::string, std::string>> mapping;
  };

  /**
   * @brief Represents a single waveform variable/signal.
   */
  struct FstVarNode {
    std::string name;

    /**
     * Handle used by libfst to identify a signal during value iteration.
     *
     * This is effectively the signal ID used for waveform updates.
     */
    fstHandle handle;

    /**
     * Bit width of the variable.
     */
    uint32_t length;

    /**
     * Input/output/inout/etc.
     */
    fstVarDir direction;

    /**
     * wire/reg/integer/real/etc.
     */
    fstVarType type;
  };

  /**
   * @brief Recursive hierarchy node representing scopes/modules/packages/etc.
   *
   * This reconstructs the hierarchical structure emitted by the FST hierarchy
   * stream into a navigable tree.
   */
  struct FstScopeNode {
    std::string name;
    std::string component;

    /**
     * Scope classification.
     */
    fstScopeType type;

    /**
     * Variables/signals directly contained in this scope.
     */
    std::vector<FstVarNode> vars;

    /**
     * Nested child scopes.
     */
    std::vector<FstScopeNode> children;
  };

  /**
   * @brief Opens an FST file for reading.
   *
   * Throws immediately on failure.
   *
   * This constructor establishes the class invariant that a successfully
   * constructed object always owns a valid reader context.
   */
  explicit Reader(std::string_view fileName);

  ~Reader() = default;

  /**
   * Copying is forbidden because the underlying libfst context is a unique
   * ownership resource.
   */
  Reader(const Reader&)            = delete;
  Reader& operator=(const Reader&) = delete;

  /**
   * Moving is safe because ownership transfers cleanly through unique_ptr.
   */
  Reader(Reader&&) noexcept            = default;
  Reader& operator=(Reader&&) noexcept = default;

  [[nodiscard]] const std::string& getFileName() const { return fn; }

  // --- Metadata Accessors --------------------------------------------------------------

  [[nodiscard]] uint64_t getStartTime() const
  {
    assert(context);
    return fstReaderGetStartTime(context.get());
  }

  [[nodiscard]] uint64_t getEndTime() const
  {
    assert(context);
    return fstReaderGetEndTime(context.get());
  }

  [[nodiscard]] uint64_t getVarCount() const
  {
    assert(context);
    return fstReaderGetVarCount(context.get());
  }

  [[nodiscard]] uint64_t getScopeCount() const
  {
    assert(context);
    return fstReaderGetScopeCount(context.get());
  }

  /**
   * @brief Returns the exponent used by the timescale.
   *
   * Example:
   *   -9 => nanoseconds
   *  -12 => picoseconds
   */
  [[nodiscard]] int8_t getTimescale() const
  {
    assert(context);
    return fstReaderGetTimescale(context.get());
  }

  [[nodiscard]] std::string getVersion() const;
  [[nodiscard]] std::string getDate() const;

  // --- Hierarchy Traversal -------------------------------------------------------------

  /**
   * @brief Generic hierarchy iterator.
   *
   * This uses a templated callable rather than std::function in order to avoid:
   *
   *   - heap allocation
   *   - virtual dispatch
   *   - type erasure overhead
   *
   * The callback is fully inlinable by the compiler.
   *
   * Example:
   *
   *   reader.iterateHierarchy([](const fstHier* h) {
   *       ...
   *   });
   */
  template <typename Func> void iterateHierarchy(Func&& callback)
  {
    assert(context);

    // Rewind hierarchy stream before iteration. libfst internally maintains iteration
    // state.
    fstReaderIterateHierRewind(context.get());

    struct fstHier* hier;

    /* The hierarchy is emitted as a flat event stream:
     *   SCOPE
     *     VAR
     *     VAR
     *     SCOPE
     *       VAR
     *     UPSCOPE
     *   UPSCOPE */

    while ((hier = fstReaderIterateHier(context.get())) != nullptr) {
      callback(hier);
    }
  }

  /**
   * @brief Reconstructs hierarchy into a recursive tree structure.
   *
   * Internally this performs a single linear pass over the hierarchy event
   * stream using a stack-based AST construction algorithm.
   */
  [[nodiscard]] FstScopeNode buildHierarchyTree();

  // --- Trace Data Reading --------------------------------------------------------------

  /**
   * @brief Restricts waveform iteration to a bounded time window.
   */
  void setLimitTimeRange(uint64_t start_time, uint64_t end_time)
  {
    assert(context);
    fstReaderSetLimitTimeRange(context.get(), start_time, end_time);
  }

  /**
   * @brief Removes any previously configured time restrictions.
   */
  void setUnlimitedTimeRange()
  {
    assert(context);
    fstReaderSetUnlimitedTimeRange(context.get());
  }

  /**
   * @brief Enables processing for all signals.
   */
  void setFacProcessMaskAll()
  {
    assert(context);
    fstReaderSetFacProcessMaskAll(context.get());
  }

  /**
   * @brief Disables processing for all signals.
   */
  void clearFacProcessMaskAll()
  {
    assert(context);
    fstReaderClrFacProcessMaskAll(context.get());
  }

  /**
   * @brief Enables a specific signal for processing.
   */
  void setFacProcessMask(fstHandle facidx)
  {
    assert(context);
    fstReaderSetFacProcessMask(context.get(), facidx);
  }

  /**
   * @brief Disables a specific signal for processing.
   */
  void clearFacProcessMask(fstHandle facidx)
  {
    assert(context);
    fstReaderClrFacProcessMask(context.get(), facidx);
  }

  /**
   * @brief Iterates waveform blocks and forwards value changes to a callback.
   *
   * This is the hottest execution path in waveform processing, so the design is
   * aggressively optimized:
   *
   *   - no std::function
   *   - no heap allocation
   *   - no virtual dispatch
   *   - no dynamic polymorphism
   *
   * libfst expects C function pointers, so stateless lambdas are used as trampoline
   * adapters.
   */
  template <typename Func> void readIterateBlocks(Func&& callback)
  {
    assert(context);

    // Trampoline for fixed-size values. libfst invokes this for regular scalar/vector
    // changes.
    auto tramp = [](void* user_data, uint64_t time, fstHandle facidx,
                    const unsigned char* value) {
      (*static_cast<std::remove_reference_t<Func>*>(user_data))(
          time, facidx, std::string_view(reinterpret_cast<const char*>(value)), 0);
    };

    // Trampoline for variable-length values. Used for dynamically sized payloads.
    auto trampVarlen = [](void* user_data, uint64_t time, fstHandle facidx,
                          const unsigned char* value, uint32_t len) {
      (*static_cast<std::remove_reference_t<Func>*>(user_data))(
          time, facidx, std::string_view(reinterpret_cast<const char*>(value), len), len);
    };

    // Forward callback state through libfst.
    fstReaderIterBlocks2(context.get(), tramp, trampVarlen, &callback, nullptr);
  }

  /**
   * @brief Parses a textual enum encoding into structured data.
   *
   * libfst internally allocates the intermediate structure. This wrapper copies the data
   * into STL-owned memory before releasing the libfst allocation.
   */
  static EnumTable parseEnumTable(std::string_view enumString);

private:
  /**
   * Cached filename for diagnostics/debugging.
   */
  std::string fn;

  /**
   * Sole owner of the underlying libfst reader context.
   */
  std::unique_ptr<fstReaderContext, ReaderDeleter> context;
};

// =======================================================================================
// DataWriter
// =======================================================================================

/**
 * @class DataWriter
 *
 * @brief Stateful writer for waveform value emission.
 *
 * This class intentionally cannot exist until hierarchy construction is fully
 * completed.
 *
 * That sequencing guarantee is enforced at the type level through the
 * HierarchyBuilder -> DataWriter transition.
 */
class DataWriter final {
public:
  DataWriter(const DataWriter&)            = delete;
  DataWriter& operator=(const DataWriter&) = delete;

  DataWriter(DataWriter&&) noexcept            = default;
  DataWriter& operator=(DataWriter&&) noexcept = default;

  ~DataWriter() = default;

  /**
   * @brief Advances simulation time.
   * @param time Timestamp for all subsequent events until the  next time change event.
   */
  inline void emitTimeChange(const uint64_t time)
  {
    assert(context);
    fstWriterEmitTimeChange(context.get(), time);
  }

  // --- String/Binary Encoded Value Emission --------------------------------------------

  inline void emitValueChange(fstHandle handle, const std::string& val)
  {
    assert(context);
    fstWriterEmitValueChange(context.get(), handle, val.c_str());
  }

  /**
   * Used for dynamically sized payloads.
   *
   * std::string_view avoids unnecessary copying.
   */
  inline void emitVariableLengthValueChange(fstHandle handle, std::string_view val)
  {
    assert(context);

    fstWriterEmitVariableLengthValueChange(context.get(), handle, val.data(),
                                           val.length());
  }

  // --- Native Integer-Oriented Emission APIs -------------------------------------------

  /**
   * These APIs avoid ASCII conversion overhead for numeric vectors.
   *
   * They are substantially more efficient than string-based emission for large
   * traces.
   */
  inline void emitValue32(fstHandle handle, uint32_t bits, uint32_t val)
  {
    assert(context);
    fstWriterEmitValueChange32(context.get(), handle, bits, val);
  }

  inline void emitValue64(fstHandle handle, uint32_t bits, uint64_t val)
  {
    assert(context);
    fstWriterEmitValueChange64(context.get(), handle, bits, val);
  }

  inline void emitValueVec32(fstHandle handle, uint32_t bits, const uint32_t* val)
  {
    assert(context);
    fstWriterEmitValueChangeVec32(context.get(), handle, bits, val);
  }

  inline void emitValueVec64(fstHandle handle, uint32_t bits, const uint64_t* val)
  {
    assert(context);
    fstWriterEmitValueChangeVec64(context.get(), handle, bits, val);
  }

  inline void flush()
  {
    assert(context);
    fstWriterFlushContext(context.get());
  }

private:
  /**
   * Only HierarchyBuilder may construct a writer.
   *
   * This enforces the intended typestate transition.
   */
  friend class HierarchyBuilder;

  explicit DataWriter(std::unique_ptr<fstWriterContext, WriterDeleter> ctx)
    : context(std::move(ctx))
  {
  }

  std::unique_ptr<fstWriterContext, WriterDeleter> context;
};

// =======================================================================================
// HierarchyBuilder
// =======================================================================================

/**
 * @class HierarchyBuilder
 *
 * @brief Typestate phase for static topology construction.
 *
 * Responsibilities:
 *
 *   - configure file metadata
 *   - create scopes
 *   - create variables
 *   - configure enums
 *
 * Once hierarchy definition is complete, ownership transitions into DataWriter through
 * finish().
 */
class HierarchyBuilder final {
public:
  explicit HierarchyBuilder(std::string_view fileName, int use_compressed_hier = 1);

  ~HierarchyBuilder() = default;

  HierarchyBuilder(const HierarchyBuilder&)            = delete;
  HierarchyBuilder& operator=(const HierarchyBuilder&) = delete;

  HierarchyBuilder(HierarchyBuilder&&) noexcept            = default;
  HierarchyBuilder& operator=(HierarchyBuilder&&) noexcept = default;

  // --- Global Writer Configuration -----------------------------------------------------

  void setTimeScale(int exponent)
  {
    assert(context);
    fstWriterSetTimescale(context.get(), exponent);
  }

  void setTimeZero(int64_t tim)
  {
    assert(context);
    fstWriterSetTimezero(context.get(), tim);
  }

  void setPackType(fstWriterPackType typ)
  {
    assert(context);
    fstWriterSetPackType(context.get(), typ);
  }

  void setFileType(fstFileType typ)
  {
    assert(context);
    fstWriterSetFileType(context.get(), typ);
  }

  void setDate(std::string_view date)
  {
    assert(context);
    const std::string dateStorage(date);
    fstWriterSetDate(context.get(), dateStorage.c_str());
  }

  void setVersion(std::string_view version)
  {
    assert(context);
    const std::string versionStorage(version);
    fstWriterSetVersion(context.get(), versionStorage.c_str());
  }

  // --- Hierarchy Construction ----------------------------------------------------------

  /**
   * @brief Pushes a new scope onto the writer hierarchy stack.
   */
  void setScope(fstScopeType scope_type, std::string_view scope_name,
                std::string_view scope_comp = "");

  /**
   * @brief Pops one hierarchy level.
   * Must match a preceding setScope().
   */
  void upScope()
  {
    assert(context);
    fstWriterSetUpscope(context.get());
  }

  /**
   * @brief Creates a waveform variable.
   *
   * aliasHandle enables signal aliasing where multiple names reference the
   * same underlying waveform storage.
   */
  fstHandle createVar(fstVarType var_type, fstVarDir var_dir, uint32_t len,
                      std::string_view name, fstHandle aliasHandle = 0);

  // --- Enum Support --------------------------------------------------------------------

  /**
   * @brief Creates a symbolic enum mapping table.
   *
   * Example:
   *
   *   "IDLE" -> "00"
   *   "RUN"  -> "01"
   */
  fstEnumHandle createEnumTable(
      std::string_view name, unsigned int min_valbits,
      const std::vector<std::pair<const std::string, const std::string>>& values);

  /**
   * @brief Associates a previously created enum table with subsequent variables.
   */
  void emitEnumTableRef(fstEnumHandle handle)
  {
    assert(context);
    fstWriterEmitEnumTableRef(context.get(), handle);
  }

  /**
   * @brief Final typestate transition.
   *
   * Consumes the hierarchy builder and returns a waveform data writer.
   *
   * The && qualifier prevents calling finish() on lvalues accidentally.
   * Users must explicitly acknowledge consumption via std::move().
   */
  DataWriter finish() &&;

private:
  std::string fn;

  /**
   * Sole owner of libfst writer state.
   */
  std::unique_ptr<fstWriterContext, WriterDeleter> context;
};

}  // namespace SILICON::waveform::fst
