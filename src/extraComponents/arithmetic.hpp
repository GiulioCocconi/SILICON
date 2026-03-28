/*
  Copyright (C) 2025 Giulio Cocconi

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
#include <core/gates.hpp>
#include <core/wire.hpp>
#include <iostream>
#include <memory>

class HalfAdder : public Component {
public:
  HalfAdder() = default;
  HalfAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr sum, Wire_ptr cout);
};

class FullAdder : public Component {
public:
  FullAdder() = default;
  FullAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr cin, Wire_ptr sum, Wire_ptr cout);
};

class AdderNBits : public Component {
public:
  AdderNBits() = default;
  AdderNBits(std::array<Bus, 2> inputs, Bus sum, Wire_ptr cout);
};
