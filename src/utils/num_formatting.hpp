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

#include <core/wire.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace SILICON::core {

enum class BusValueFormat { Raw, Signed, Unsigned, Hex, Oct, Bin, Unknown };

/** @brief Format an LSB-first BusValue for display. */
[[nodiscard]] std::string formatValue(const BusValue& value, BusValueFormat format,
                                      std::size_t fixedWidth = 0);

/**
 * @brief Parse user-facing decimal, hexadecimal, octal, binary, or four-state text.
 *
 * Unprefixed binary/four-state text is interpreted as an MSB-first raw value; other
 * unprefixed digits are decimal. Invalid text returns BusValueFormat::Unknown.
 */
[[nodiscard]] std::pair<BusValue, BusValueFormat> valueFromStr(std::string_view text);

[[nodiscard]] BusValue maxValueForBusWidth(std::size_t width);

/** @brief Builds a fixed-width BusValue from an unsigned integer (LSB-first). */
[[nodiscard]] BusValue busValueFromInteger(std::uint64_t value, std::size_t width);

/** @brief Strictly decodes an MSB-first raw string containing 0, 1, X, or E. */
[[nodiscard]] BusValue busValueFromBits(std::string_view bits);

}  // namespace SILICON::core
