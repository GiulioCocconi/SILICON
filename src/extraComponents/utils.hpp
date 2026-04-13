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

class WireSplitter : public Component {
public:
  static constexpr std::string_view Type = "WireSplitter";
  std::string_view typeName() const override { return Type; }

  WireSplitter() = default;
  WireSplitter(Bus input, const std::vector<Bus>& outputs);

  void simulate(class Simulator& sim) override;
};

class WireMerger : public Component {
public:
  static constexpr std::string_view Type = "WireMerger";
  std::string_view typeName() const override { return Type; }

  WireMerger() = default;
  WireMerger(const std::vector<Bus>& inputs, Bus output);

  void simulate(class Simulator& sim) override;
};
