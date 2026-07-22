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

#include <string_view>

namespace silicon::yosys::cells {

inline constexpr std::string_view Dff       = "SILICON_DFF";
inline constexpr std::string_view Dffe      = "SILICON_DFFE";
inline constexpr std::string_view Dffsr     = "SILICON_DFFSR";
inline constexpr std::string_view Dffsre    = "SILICON_DFFSRE";
inline constexpr std::string_view Jkff      = "SILICON_JKFF";
inline constexpr std::string_view HalfAdder = "SILICON_HALF_ADDER";
inline constexpr std::string_view FullAdder = "SILICON_FULL_ADDER";
inline constexpr std::string_view Adder     = "SILICON_ADDER";
inline constexpr std::string_view Pipo      = "SILICON_PIPO";
inline constexpr std::string_view Piso      = "SILICON_PISO";
inline constexpr std::string_view Sipo      = "SILICON_SIPO";
inline constexpr std::string_view Siso      = "SILICON_SISO";

}  // namespace silicon::yosys::cells
