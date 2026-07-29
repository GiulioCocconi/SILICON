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
#include <string>
#include <string_view>

namespace SILICON::core {

enum class NumberFormat { Signed, Unsigned, Hex, Oct, Bin };

[[nodiscard]] unsigned int maxValueForBusWidth(std::size_t width);
[[nodiscard]] std::string  formatFixedWidthHex(unsigned int value, std::size_t width);
[[nodiscard]] bool         parseBusValue(std::string_view text, unsigned int& value);
[[nodiscard]] std::string  formatRawBits(std::string_view rawBits, NumberFormat format);

}  // namespace SILICON::core
