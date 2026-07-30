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

#include "num_formatting.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <sstream>

namespace SILICON::core {

namespace {

  std::string trimmed(std::string_view text)
  {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
      text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
      text.remove_suffix(1);
    return std::string(text);
  }

  bool startsWithIgnoreCase(std::string_view text, std::string_view prefix)
  {
    if (text.size() < prefix.size())
      return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(text[i]))
          != std::tolower(static_cast<unsigned char>(prefix[i])))
        return false;
    }
    return true;
  }

  bool isKnownBitString(std::string_view rawBits)
  {
    return !rawBits.empty() && std::ranges::all_of(rawBits, [](char ch) {
      return ch == '0' || ch == '1';
    });
  }

  std::string uppercaseRaw(std::string_view rawBits)
  {
    std::string value(rawBits);
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
      return static_cast<char>(std::toupper(ch));
    });
    return value.empty() ? "X" : value;
  }

  void decimalMultiplyByTwo(std::string& value)
  {
    int carry = 0;
    for (auto it = value.rbegin(); it != value.rend(); ++it) {
      const int digit = (*it - '0') * 2 + carry;
      *it             = static_cast<char>('0' + digit % 10);
      carry           = digit / 10;
    }
    if (carry != 0)
      value.insert(value.begin(), static_cast<char>('0' + carry));
  }

  void decimalAddOne(std::string& value)
  {
    int carry = 1;
    for (auto it = value.rbegin(); it != value.rend() && carry != 0; ++it) {
      const int digit = (*it - '0') + carry;
      *it             = static_cast<char>('0' + digit % 10);
      carry           = digit / 10;
    }
    if (carry != 0)
      value.insert(value.begin(), '1');
  }

  std::string parseKnownBits(std::string_view rawBits)
  {
    std::string value = "0";
    for (const char bit : rawBits) {
      decimalMultiplyByTwo(value);
      if (bit == '1')
        decimalAddOne(value);
    }
    return value;
  }

  std::string parseTwosComplementMagnitude(std::string_view rawBits)
  {
    std::string magnitude(rawBits);
    for (char& bit : magnitude)
      bit = bit == '1' ? '0' : '1';

    for (auto it = magnitude.rbegin(); it != magnitude.rend(); ++it) {
      if (*it == '0') {
        *it = '1';
        break;
      }
      *it = '0';
    }

    return parseKnownBits(magnitude);
  }

  std::string groupedBase(std::string_view rawBits, int groupSize,
                          std::string_view digits, std::string_view prefix)
  {
    if (!isKnownBitString(rawBits))
      return uppercaseRaw(rawBits);

    const std::size_t pad =
        (groupSize - rawBits.size() % static_cast<std::size_t>(groupSize))
        % static_cast<std::size_t>(groupSize);
    std::string padded(pad, '0');
    padded.append(rawBits);

    std::string result(prefix);
    bool        emittedNonZero = false;
    for (std::size_t i = 0; i < padded.size(); i += static_cast<std::size_t>(groupSize)) {
      int value = 0;
      for (int offset = 0; offset < groupSize; ++offset) {
        value <<= 1;
        if (padded[i + static_cast<std::size_t>(offset)] == '1')
          ++value;
      }
      result.push_back(digits[static_cast<std::size_t>(value)]);
      emittedNonZero = emittedNonZero || value != 0;
    }

    if (!emittedNonZero)
      return std::string(prefix) + "0";
    return result;
  }

}  // namespace

unsigned int maxValueForBusWidth(const std::size_t width)
{
  if (width >= std::numeric_limits<unsigned int>::digits)
    return std::numeric_limits<unsigned int>::max();
  return (1U << width) - 1U;
}

std::string formatFixedWidthHex(const unsigned int value, const std::size_t width)
{
  const int          hexDigits = std::max(1, static_cast<int>((width + 3) / 4));
  std::ostringstream stream;
  stream << "0X" << std::uppercase << std::hex;
  stream.width(hexDigits);
  stream.fill('0');
  stream << value;
  return stream.str();
}

bool parseBusValue(std::string_view text, unsigned int& value)
{
  const std::string normalized = trimmed(text);
  if (normalized.empty())
    return false;

  int              base   = 10;
  std::string_view digits = normalized;
  if (startsWithIgnoreCase(digits, "0x")) {
    base = 16;
    digits.remove_prefix(2);
  } else if (startsWithIgnoreCase(digits, "0b")) {
    base = 2;
    digits.remove_prefix(2);
  }

  if (digits.empty())
    return false;

  unsigned int parsed = 0;
  const auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
  if (ec != std::errc{} || ptr != digits.data() + digits.size())
    return false;

  value = parsed;
  return true;
}

std::string formatRawBits(std::string_view rawBits, const NumberFormat format)
{
  if (!isKnownBitString(rawBits))
    return uppercaseRaw(rawBits);

  switch (format) {
    case NumberFormat::Hex: return groupedBase(rawBits, 4, "0123456789ABCDEF", "0X");
    case NumberFormat::Oct: return groupedBase(rawBits, 3, "01234567", "0O");
    case NumberFormat::Bin: return std::string("0B") + std::string(rawBits);
    case NumberFormat::Unsigned: return parseKnownBits(rawBits);
    case NumberFormat::Signed:
      if (rawBits.front() == '1')
        return "-" + parseTwosComplementMagnitude(rawBits);
      return parseKnownBits(rawBits);
  }

  return uppercaseRaw(rawBits);
}

}  // namespace SILICON::core
