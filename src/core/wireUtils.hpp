/*
  Copyright (C) 2026 Giulio Cocconi

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
#include <vector>

#include <core/wire.hpp>

namespace silicon::wire {

/**
 * @brief Checks whether an unsigned value exceeds a bus width.
 * @param value Value to fit in the bus.
 * @param width Number of bits available in the bus.
 * @return True when value cannot be represented by width bits.
 */
[[nodiscard]] bool busValueOverflowsWidth(unsigned int value, std::size_t width);

/**
 * @brief Checks whether assigning a binary value would change any driven bus wire.
 * @param bus Bus to compare against the requested value.
 * @param value Binary value encoded least-significant bit first.
 * @return True when at least one existing wire would change state.
 */
[[nodiscard]] bool busWillChangeToValue(const Bus& bus, unsigned int value);

/**
 * @brief Tests whether a state is a concrete binary logic value.
 * @param state State to test.
 * @return True for LOW or HIGH, false for UNKNOWN or ERROR.
 */
[[nodiscard]] bool isKnownBinary(State state);

/**
 * @brief Tests whether an actual state could represent an expected binary state.
 * @param actual Observed state.
 * @param expected Expected concrete state.
 * @return True when actual equals expected or actual is not a known binary state.
 */
[[nodiscard]] bool mayBe(State actual, State expected);

/**
 * @brief Normalizes non-binary states for stored binary elements.
 * @param state State to normalize.
 * @return LOW/HIGH unchanged, UNKNOWN for every other state.
 */
[[nodiscard]] State normalizeBinaryOrUnknown(State state);

/**
 * @brief Reads an optional control input, defaulting to inactive when unconnected.
 * @param inputs Component input buses.
 * @param index Input bus index.
 * @param inactiveState State returned when the control pin is unconnected.
 * @return Current wire state, or inactiveState when the pin is unconnected.
 */
[[nodiscard]] State optionalControlStateOrInactive(const std::vector<Bus>& inputs,
                                                   unsigned int index,
                                                   State inactiveState);

}  // namespace silicon::wire
