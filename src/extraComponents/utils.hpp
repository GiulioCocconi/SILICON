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

#include <core/component.hpp>
#include <core/wire.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SILICON::extra {
using namespace SILICON::core;

class WireSplitter : public Component {
private:
  void initializeProperties();
  std::vector<std::pair<unsigned int, unsigned int>> slices;

public:
  static constexpr std::string_view Type = "WireSplitter";
  std::string_view                  typeName() const override { return Type; }
  ComponentMetadata                 metadata() const override
  {
    return {"Wire Splitter", "Splits a bus into bits or bus slices.",
            ComponentCategory::Utils};
  }

  WireSplitter();
  WireSplitter(Bus input, const std::vector<Bus>& outputs);

  int setSize(int newSize);

  /** Comma-separated offset:width pairs; empty selects every individual bit.
   * Slices may overlap, so byte, half-word and scalar consumers can share a splitter.
   */
  std::string setSlices(const std::string& specification);
  [[nodiscard]] std::vector<std::pair<unsigned int, unsigned int>> outputSlices() const;

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

class WireMerger : public Component {
private:
  void initializeProperties();
  std::vector<unsigned int> widths;

public:
  static constexpr std::string_view Type = "WireMerger";
  std::string_view                  typeName() const override { return Type; }
  ComponentMetadata                 metadata() const override
  {
    return {"Wire Merger", "Concatenates bits or buses, least-significant input first.",
            ComponentCategory::Utils};
  }

  WireMerger();
  WireMerger(const std::vector<Bus>& inputs, Bus output);

  int setSize(int newSize);

  /** Comma-separated input widths summing to size; empty selects individual bits. */
  std::string setWidths(const std::string& specification);

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

}  // namespace SILICON::extra
