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
#include <string_view>
#include <vector>

namespace SILICON::extra {
using namespace SILICON::core;

class WireSplitter : public Component {
private:
  void initializeProperties();

public:
  static constexpr std::string_view Type = "WireSplitter";
  std::string_view                  typeName() const override { return Type; }
  ComponentMetadata                 metadata() const override
  {
    return {"Wire Splitter", "Splits a bus into individual output bits.",
            ComponentCategory::Utils};
  }

  WireSplitter();
  WireSplitter(Bus input, const std::vector<Bus>& outputs);

  int setSize(int newSize);

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

class WireMerger : public Component {
private:
  void initializeProperties();

public:
  static constexpr std::string_view Type = "WireMerger";
  std::string_view                  typeName() const override { return Type; }
  ComponentMetadata                 metadata() const override
  {
    return {"Wire Merger", "Merges individual input bits into a bus.",
            ComponentCategory::Utils};
  }

  WireMerger();
  WireMerger(const std::vector<Bus>& inputs, Bus output);

  int setSize(int newSize);

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

}  // namespace SILICON::extra
