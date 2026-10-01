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

namespace SILICON::yosys::cells {

inline constexpr std::string_view ROM        = "SILICON_ROM";
inline constexpr std::string_view LOGIC      = "SILICON_LOGIC";
inline constexpr std::string_view COMPARE    = "SILICON_COMPARE";
inline constexpr std::string_view SHIFT      = "SILICON_SHIFT";
inline constexpr std::string_view DFF        = "SILICON_DFF";
inline constexpr std::string_view DFFE       = "SILICON_DFFE";
inline constexpr std::string_view DLATCH     = "SILICON_DLATCH";
inline constexpr std::string_view DFFSR      = "SILICON_DFFSR";
inline constexpr std::string_view DFFSRE     = "SILICON_DFFSRE";
inline constexpr std::string_view JKFF       = "SILICON_JKFF";
inline constexpr std::string_view HALF_ADDER = "SILICON_HALF_ADDER";
inline constexpr std::string_view FULL_ADDER = "SILICON_FULL_ADDER";
inline constexpr std::string_view ADDER      = "SILICON_ADDER";
inline constexpr std::string_view PIPO       = "SILICON_PIPO";
inline constexpr std::string_view PISO       = "SILICON_PISO";
inline constexpr std::string_view SIPO       = "SILICON_SIPO";
inline constexpr std::string_view SISO       = "SILICON_SISO";

}  // namespace SILICON::yosys::cells

namespace SILICON::yosys::attributes {

/** Verilog memory attribute naming the project binary document used by a ROM. */
inline constexpr std::string_view BINARY_DOCUMENT = "silicon_mem_slug";

}  // namespace SILICON::yosys::attributes
