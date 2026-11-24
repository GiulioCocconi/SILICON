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

  /** Decode bytes, trimming byte padding or zero-filling a partial final word. */
  [[nodiscard]] static MemoryImage fromBytes(std::span<const std::uint8_t> bytes,
                                             std::size_t wordWidth, std::size_t depth);
  /** Encode raw bytes, zero-padding the final byte. */
  [[nodiscard]] std::vector<std::uint8_t> bytes() const;
  [[nodiscard]] BusValue word(std::size_t address) const;

  [[nodiscard]] const Bits& bits() const noexcept { return data; }
  [[nodiscard]] std::size_t wordWidth() const noexcept { return width; }
  [[nodiscard]] std::size_t depth() const noexcept { return words; }

private:
  Bits        data;
  std::size_t width;
  std::size_t words;
};

/** Common geometry and immutable binary-document backing for RAM and ROM. */
class Memory : public Component {
public:
  /** Select a document atomically, rejecting contents incompatible with explicit depth.
   */
  void setBinaryDocument(std::string                                      slug,
                         std::shared_ptr<const std::vector<std::uint8_t>> contents);
  /** Hydrate or update a document without discarding explicit geometry. */
  void refreshBinaryContents(std::shared_ptr<const std::vector<std::uint8_t>> contents);
  /** Install exact geometry and initialization, selecting explicit depth. */
  void setMemoryImage(std::shared_ptr<const MemoryImage> image);

  /** Resolved depth; zero if automatic depth has no nonempty document. */
  [[nodiscard]] std::size_t memoryDepth() const noexcept;
  /**
   * Zero infers ceil(byte_count * 8 / word_width), zero-filling a partial final word.
   * Positive depths distinguish logical data from byte padding. Explicit images
   * may end within their final word; bits beyond the logical image are ignored.
   */
  void setMemoryDepth(std::size_t depth);

  [[nodiscard]] std::shared_ptr<const MemoryImage> memoryImageSnapshot() const noexcept
  {
    return image;
  }
  [[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>>
  binaryContentsSnapshot() const noexcept { return content; }

protected:
  explicit Memory(int depth);
  [[nodiscard]] int            configuredDataWidth() const;
  [[nodiscard]] unsigned short addressWidth() const;
  virtual void                 reshapeBuses() = 0;

  std::shared_ptr<const std::vector<std::uint8_t>> content;
  std::shared_ptr<const MemoryImage> image;

private:
  void configure(int width, int depth, std::string slug,
                 std::shared_ptr<const std::vector<std::uint8_t>> contents,
                 bool                                             rejectInvalid);
  void commit(int width, int depth, std::string slug,
              std::shared_ptr<const std::vector<std::uint8_t>> contents,
              std::shared_ptr<const MemoryImage>               nextImage);
};

/** Independent asynchronous read ports and one masked, positive-edge write port. */
class RAM final : public Memory {
public:
  static constexpr std::string_view Type = "RAM";

  enum class Inputs : unsigned int {
    ReadAddress = 0,
    WriteAddress,
    WriteData,
    WriteEnable,
    Clock,
  };
  enum class Outputs : unsigned int { Data = 0 };

  RAM();

  [[nodiscard]] std::string_view typeName() const override { return Type; }
  [[nodiscard]] ComponentMetadata metadata() const override
  {
    return {"RAM", "Asynchronous read and masked synchronous write memory.",
            ComponentCategory::Memory};
  }

  void simulate(SILICON::simulation::Simulator& sim,
                const SILICON::simulation::Context& context) override;
  [[nodiscard]] bool usesStagedSequentialOutputs() const override { return true; }
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

  [[nodiscard]] std::size_t readPortCount() const;
  // Keep the original five input indices stable; extra read addresses follow them.
  [[nodiscard]] static std::size_t readAddressIndex(std::size_t port)
  {
    return port == 0 ? 0 : 4 + port;
  }

private:
  std::vector<BusValue> words;
  // Only modified words are copied, for cancellation before output publication.
  std::vector<std::pair<std::size_t, BusValue>> previousWords;

  void               reshapeBuses() override;
  void initializeWords();
  void write(const BusValue& address, const BusValue& data, const BusValue& mask,
             State clockEnable);
};

/** Asynchronous read-only memory backed by a project binary document. */
class ROM final : public Memory {
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

private:
  void reshapeBuses() override;
  void driveState(SILICON::simulation::Simulator& sim, State state);
};

}  // namespace SILICON::core
