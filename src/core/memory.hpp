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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <vector>

#include <boost/dynamic_bitset.hpp>
#include <core/component.hpp>

namespace SILICON::core {

/** Packed, least-significant-bit-first words with exact logical geometry. */
class MemoryImage {
public:
  using Bits = boost::dynamic_bitset<std::uint8_t>;

  MemoryImage(Bits bits, std::size_t wordWidth, std::size_t depth);
  MemoryImage(Bits bits, std::size_t wordWidth, std::size_t depth,
              std::size_t knownPrefixBits);

  /** Decode packed LSB-first bytes. Unused high byte-padding bits must be zero. */
  [[nodiscard]] static MemoryImage fromBytes(std::span<const std::uint8_t> bytes,
                                             std::size_t wordWidth, std::size_t depth,
                                             bool unknownPadding = false);
  /** Encode raw bytes, zero-padding the final byte. */
  [[nodiscard]] std::vector<std::uint8_t> bytes() const;
  [[nodiscard]] BusValue                  word(std::size_t address) const;

  [[nodiscard]] std::size_t knownBitCount() const noexcept { return knownBits; }
  [[nodiscard]] const Bits& bits() const noexcept { return data; }
  [[nodiscard]] std::size_t wordWidth() const noexcept { return width; }
  [[nodiscard]] std::size_t depth() const noexcept { return words; }

private:
  Bits        data;
  std::size_t width;
  std::size_t words;
  std::size_t knownBits;
};

/** Asynchronous read ports with an optional masked, positive-edge write port.
 * Unknown read addresses merge all possible words bit by bit. */
class Memory final : public Component {
public:
  [[nodiscard]] std::vector<PortDescription> portDescriptions() const override;

  static constexpr std::string_view Uninitialized  = "Uninitialized";
  static constexpr std::string_view Zero           = "Zero";
  static constexpr std::string_view BinaryDocument = "Binary document";
  static constexpr std::string_view Unknown        = "Unknown";
  // Bound packed images and the per-word runtime representation before allocation.
  static constexpr std::size_t MaxCapacityBits = 64 * 1024 * 1024;
  static constexpr std::size_t MaxDepth        = 1024 * 1024;
  static constexpr std::size_t MaxPortBits     = 1024 * 1024;

  /** Atomically set geometry, rejecting image truncation. Zero depthOverride
   * selects 2^addressWidth. */
  void setGeometry(int dataWidth, int addressWidth, int depthOverride = 0);
  /** Select binary initialization, growing geometry if needed (never shrinking). */
  void setBinaryDocument(std::string                                      slug,
                         std::shared_ptr<const std::vector<std::uint8_t>> contents);
  /** Refresh a selected document without changing geometry. Invalid images remain
   * unresolved and are reported by initializationError(), execution and export. */
  void refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents);
  /** Explicitly install exact geometry and immutable initialization (e.g. imports). */
  void setMemoryImage(std::shared_ptr<const MemoryImage> image);

  void setProperties(const PropertyMap& values) override;
  [[nodiscard]] std::vector<PropertyDefinition> propertyDefinitions() const override;

  [[nodiscard]] std::optional<PropertyValue>
  getProperty(std::string_view key) const override;

  [[nodiscard]] std::size_t    memoryDepth() const noexcept;
  [[nodiscard]] unsigned short addressWidth() const;
  [[nodiscard]] std::size_t    capacityBits() const noexcept;
  [[nodiscard]] std::string    initializationError() const;
  void                         validateInitialization() const;

  [[nodiscard]] std::shared_ptr<const MemoryImage> memoryImageSnapshot() const noexcept
  {
    return image;
  }
  [[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>>
  binaryContentsSnapshot() const noexcept
  {
    return content;
  }

  static constexpr std::string_view Type = "Memory";

  enum class WriteInputs : unsigned int {
    Address = 0,
    Data,
    Enable,
    Clock,
  };
  enum class Outputs : unsigned int { Data = 0 };

  Memory();

  [[nodiscard]] std::vector<DocumentationConfiguration>
  documentationConfigurations() const override;

  [[nodiscard]] std::string_view  typeName() const override { return Type; }
  [[nodiscard]] ComponentMetadata metadata() const override
  {
    return {
        .displayName = "Memory",
        .description =
            "Asynchronous read memory with an optional masked synchronous write port.",
        .category                = ComponentCategory::Memory,
        .configurationProperties = {"type", "addressWidth", "dataWidth", "readPorts",
                                    "initialization", "padding"}};
  }

  using Component::simulate;
  void               simulate(SILICON::simulation::Simulator&     sim,
                              const SILICON::simulation::Context& context) override;
  [[nodiscard]] bool usesStagedSequentialOutputs() const override { return isWritable(); }
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

  [[nodiscard]] bool        isWritable() const;
  [[nodiscard]] std::size_t readPortCount() const;
  // Read address inputs use their port number; the optional write inputs follow.
  [[nodiscard]] std::size_t writeInputIndex(WriteInputs input) const
  {
    return readPortCount() + busIndex(input);
  }

private:
  [[nodiscard]] int configuredDataWidth() const;
  void              reshapeBuses();

  std::shared_ptr<const std::vector<std::uint8_t>> content;
  std::shared_ptr<const MemoryImage>               image;

  bool                                          shapedWritable = false;
  std::vector<BusValue>                         words;
  std::vector<std::pair<std::size_t, BusValue>> previousWords;

  void initializeWords();
  [[nodiscard]] BusValue readWord(std::size_t address) const;
  [[nodiscard]] BusValue read(const BusValue& address) const;
  void write(const BusValue& address, const BusValue& data, const BusValue& mask,
             State clockEnable);
  void commit(PropertyMap next, std::shared_ptr<const std::vector<std::uint8_t>> contents,
              bool rejectInvalid);
};

}  // namespace SILICON::core
