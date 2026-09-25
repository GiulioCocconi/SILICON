/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <core/component.hpp>

namespace SILICON::core {

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
  void setBinaryDocument(std::string slug, std::shared_ptr<const std::string> contents);

  /**
   * Replaces the cached contents without changing the document slug.
   *
   * Missing, empty, and externally invalidated contents are accepted and leave
   * the ROM unresolved.
   */
  void refreshBinaryContents(std::shared_ptr<const std::string> contents);

  /** Returns the immutable binary data currently cached by this ROM. */
  [[nodiscard]] std::shared_ptr<const std::string>
  binaryContentsSnapshot() const noexcept;
  [[nodiscard]] std::size_t resolvedWordCount() const noexcept { return wordCount; }

private:
  std::shared_ptr<const std::string> content;
  bool                               contentResolved        = false;
  std::size_t                        wordCount              = 0;
  bool                               initializingProperties = true;
  bool                               settingBinaryDocument  = false;

  void              configure(int dataWidth, std::shared_ptr<const std::string> contents,
                              bool rejectInvalid, bool reshape, bool preferAddressDepth = false);
  void              reshapeBuses(int dataWidth, std::size_t words);
  void              driveState(SILICON::simulation::Simulator& sim, State state);
  [[nodiscard]] int configuredDataWidth() const;
  [[nodiscard]] std::string configuredBinaryContents() const;
};

}  // namespace SILICON::core
