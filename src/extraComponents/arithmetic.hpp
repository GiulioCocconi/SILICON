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

#include <array>
#include <memory>
#include <string_view>

#include <core/component.hpp>
#include <core/wire.hpp>

class HalfAdder : public Component {
public:
  static constexpr std::string_view Type = "HalfAdder";
  std::string_view                  typeName() const override { return Type; }

  HalfAdder();
  HalfAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr sum, Wire_ptr cout);

  void simulate(class Simulator& sim) override;
};

class FullAdder : public Component {
public:
  static constexpr std::string_view Type = "FullAdder";
  std::string_view                  typeName() const override { return Type; }

  FullAdder();
  FullAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr cin, Wire_ptr sum, Wire_ptr cout);

  void simulate(Simulator& sim) override;
};

class AdderNBits : public Component {
public:
  static constexpr std::string_view Type = "AdderNBits";
  std::string_view                  typeName() const override { return Type; }

  AdderNBits();
  AdderNBits(std::array<Bus, 2> inputs, Bus sum, Wire_ptr cout);

  void simulate(Simulator& sim) override;

  int setSize(int width);
};
