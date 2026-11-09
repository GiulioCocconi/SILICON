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

#include <vector>

#include <boost/dynamic_bitset.hpp>
#include <core/component.hpp>

namespace SILICON::core {

/** Packed, least-significant-bit-first words with exact logical geometry. */
class MemoryImage {
public:
  using Bits = boost::dynamic_bitset<std::uint8_t>;

  MemoryImage(Bits bits, std::size_t wordWidth, std::size_t depth);

  /** Decode bytes, trimming byte padding or zero-filling a partial final word. */
  [[nodiscard]] static MemoryImage fromBytes(std::span<const std::uint8_t> bytes,
                                             std::size_t wordWidth, std::size_t depth);
  /** Encode raw bytes, zero-padding the final byte. */
  [[nodiscard]] std::vector<std::uint8_t> bytes() const;

  [[nodiscard]] const Bits& bits() const noexcept { return data; }
  [[nodiscard]] std::size_t wordWidth() const noexcept { return width; }
  [[nodiscard]] std::size_t depth() const noexcept { return words; }

private:
  Bits        data;
  std::size_t width;
  std::size_t words;
};

/** Asynchronous read-only memory backed by a project binary document. */
class ROM final : public Component {
public:
  static constexpr std::string_view Type = "ROM";

  enum class Inputs : unsigned int {
    Address = 0,
    CS      = 1,
    OE      = 2,
  };

  enum class Outputs : unsigned int {
    Data = 0,
  };

  ROM();
  ROM(Bus address, Wire_ptr chipSelect, Wire_ptr outputEnable, Bus data);

  [[nodiscard]] std::string_view  typeName() const override { return Type; }
  [[nodiscard]] ComponentMetadata metadata() const override
  {
    return {"ROM", "Reads packed words from a project binary document.",
            ComponentCategory::Memory};
  }

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

  /**
   * Sets the document slug and its immutable contents as one operation.
   *
   * A non-empty snapshot whose packed word count is not a power of two is
   * rejected without changing the current document, snapshot, or bus sizing.
   */
  void setBinaryDocument(std::string                                      slug,
                         std::shared_ptr<const std::vector<std::uint8_t>> contents);

  /**
   * Replaces the cached contents without changing the document slug.
   *
   * Missing, empty, and externally invalidated contents are accepted and leave
   * the ROM unresolved.
   */
  void refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents);

  /** Cache a canonical ROM image with an explicit depth, including a single word. */
  void refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents,
                             std::size_t                                      words);

  /** Returns the immutable binary data currently cached by this ROM. */
  [[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>>
                            binaryContentsSnapshot() const noexcept;
  [[nodiscard]] std::size_t resolvedWordCount() const noexcept;

  /** Install an exact image, such as a Yosys memory initializer. */
  void setMemoryImage(std::shared_ptr<const MemoryImage> image);
  [[nodiscard]] std::shared_ptr<const MemoryImage> memoryImageSnapshot() const noexcept
  {
    return image;
  }

  /** Persisted depth; zero means infer geometry for legacy or newly selected documents.
   */
  [[nodiscard]] std::size_t memoryDepth() const noexcept;
  void                      setMemoryDepth(std::size_t depth);

private:
  std::shared_ptr<const std::vector<std::uint8_t>> content;
  std::shared_ptr<const MemoryImage>               image;
  std::size_t                                      imageDepth             = 0;
  bool                                             settingMemoryImage     = false;
  bool                               settingBinaryDocument  = false;

  void configure(int dataWidth, std::shared_ptr<const std::vector<std::uint8_t>> contents,
                 bool rejectInvalid, bool preferAddressDepth = false,
                 std::size_t explicitWords = 0);
  void              reshapeBuses(int dataWidth, std::size_t words);
  void              driveState(SILICON::simulation::Simulator& sim, State state);
  [[nodiscard]] int configuredDataWidth() const;
  [[nodiscard]] std::string configuredBinaryContents() const;
};

}  // namespace SILICON::core
