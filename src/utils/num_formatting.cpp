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
#include <format>
#include <map>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <core/wireUtils.hpp>
#include <utils/ranges_wrapper.hpp>

namespace SILICON::core {
namespace {

  const std::map<BusValueFormat, std::string> formatPrefix{
      {BusValueFormat::Signed, "-"},
      {BusValueFormat::Hex, "0x"},
      {BusValueFormat::Oct, "0o"},
      {BusValueFormat::Bin, "0b"},
  };

  const std::map<BusValueFormat, std::string> formatAlphabet{
      {BusValueFormat::Raw,
       std::format("{}{}{}{}", static_cast<char>(std::to_underlying(State::LOW)),
                   static_cast<char>(std::to_underlying(State::HIGH)),
                   static_cast<char>(std::to_underlying(State::UNKNOWN)),
                   static_cast<char>(std::to_underlying(State::ERROR)))},
      {BusValueFormat::Signed, "0123456789"},
      {BusValueFormat::Unsigned, "0123456789"},
      {BusValueFormat::Oct, "01234567"},
      {BusValueFormat::Hex, "0123456789ABCDEF"},
      {BusValueFormat::Bin, "01"},
  };

  bool satisfiesAlphabet(const std::string_view digits, const BusValueFormat format)
  {
    const auto alphabet = formatAlphabet.at(format);

    return !digits.empty() && std::ranges::all_of(digits, [alphabet](const char digit) {
      return alphabet.contains(toupper(digit));
    });
  };

  char upper(const char value)
  {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  }

  std::string_view trim(std::string_view text)
  {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
      text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
      text.remove_suffix(1);
    return text;
  }

  bool startsWithIgnoreCase(const std::string_view value, const std::string_view prefix)
  {
    return value.size() >= prefix.size()
           && std::ranges::equal(
               value.substr(0, prefix.size()), prefix,
               [](const char lhs, const char rhs) { return upper(lhs) == upper(rhs); });
  }

  [[nodiscard]] std::pair<BusValueFormat, std::string_view>
  getFormat(std::string_view value)
  {
    value = trim(value);
    if (value.empty())
      return {BusValueFormat::Unknown, {}};

    // Explicit prefixes take precedence over the raw alphabet (notably for 0xE).
    for (const auto& [format, prefix] : formatPrefix) {
      if (!startsWithIgnoreCase(value, prefix))
        continue;

      const auto digits = value.substr(prefix.size());
      if (satisfiesAlphabet(digits, format))
        return {format, digits};
    }

    for (const auto& [format, _] : formatAlphabet) {
      if (formatPrefix.contains(format))
        continue;

      auto digits = value;
      if (satisfiesAlphabet(digits, format))
        return {format, digits};
    }
    return {BusValueFormat::Unknown, {}};
  }

  // Arbitrary-precision Horner's method for converting unbounded binary strings.
  void decimalMultiplyByTwoAndAdd(std::string& value, const int add)
  {
    int carry = add;
    for (char& digit : value | std::views::reverse) {
      const int expanded = (digit - '0') * 2 + carry;
      digit              = static_cast<char>('0' + expanded % 10);
      carry              = expanded / 10;
    }
    if (carry != 0)
      value.insert(value.begin(), static_cast<char>('0' + carry));
  }

  std::string parseKnownBits(const std::string_view rawBits)
  {
    std::string value = "0";
    for (const char bit : rawBits)
      decimalMultiplyByTwoAndAdd(value, bit - '0');
    return value;
  }

  std::string twosComplementMagnitude(const std::string_view rawBits)
  {
    std::string magnitude =
        rawBits
        | std::views::transform([](const char bit) { return bit == '1' ? '0' : '1'; })
        | std::ranges::to<std::string>();

    for (char& bit : magnitude | std::views::reverse) {
      if (bit == '0') {
        bit = '1';
        break;
      }
      bit = '0';
    }
    return parseKnownBits(magnitude);
  }

  std::string groupedBase(const std::string_view rawBits, const int groupSize,
                          const std::string_view digits)
  {
    if (std::ranges::all_of(rawBits, [](const char bit) { return bit == '0'; }))
      return "0";

    const std::size_t padding =
        (static_cast<std::size_t>(groupSize) - rawBits.size() % groupSize)
        % static_cast<std::size_t>(groupSize);
    std::string padded(padding, '0');
    padded += rawBits;

    std::string result;
    for (const auto chunk : padded | SILICON::views::chunk(groupSize)) {
      const int value =
          std::ranges::fold_left(chunk, 0, [](const int acc, const char bit) {
            return (acc << 1) | (bit - '0');
          });
      result.push_back(digits[static_cast<std::size_t>(value)]);
    }

    const auto firstNonZero = result.find_first_not_of('0');
    return firstNonZero == std::string::npos ? "0" : result.substr(firstNonZero);
  }

}  // namespace

BusValue maxValueForBusWidth(const std::size_t width)
{
  return BusValue(width, State::HIGH);
}

BusValue busValueFromInteger(std::uint64_t value, const std::size_t width)
{
  BusValue result;
  result.reserve(width);
  for (std::size_t bit = 0; bit < width; ++bit) {
    result.push_back((value & 1U) != 0 ? State::HIGH : State::LOW);
    value >>= 1U;
  }
  return result;
}

BusValue busValueFromBits(const std::string_view bits)
{
  if (bits.empty())
    return {};

  const auto [format, digits] = getFormat(bits);
  // Only valid formats are those which satisfy the raw alphabet
  if (format != BusValueFormat::Raw && !satisfiesAlphabet(digits, BusValueFormat::Raw))
    throw std::invalid_argument("Raw bus values may contain only 0, 1, X, or E");

  return digits | std::views::reverse | std::views::transform([](const char digit) {
           return static_cast<State>(upper(digit));
         })
         | std::ranges::to<BusValue>();
}

std::string formatValue(const BusValue& value, const BusValueFormat format,
                        const std::size_t fixedWidth)
{
  if (value.empty())
    return {};

  const bool mustBeRaw = std::ranges::any_of(value, [](const State state) {
    return state == State::UNKNOWN || state == State::ERROR;
  });

  const auto rawStr = value | std::views::reverse
                      | std::views::transform([](const State state) {
                          return static_cast<char>(std::to_underlying(state));
                        })
                      | std::ranges::to<std::string>();

  if (format == BusValueFormat::Raw || mustBeRaw)
    return rawStr;

  std::string res;
  switch (format) {
    case BusValueFormat::Hex:
      res = groupedBase(rawStr, 4, formatAlphabet.at(format));
      break;
    case BusValueFormat::Oct:
      res = groupedBase(rawStr, 3, formatAlphabet.at(format));
      break;
    case BusValueFormat::Bin: res = rawStr; break;
    case BusValueFormat::Unsigned: res = parseKnownBits(rawStr); break;
    case BusValueFormat::Signed:
      res = value.back() == State::HIGH
                ? formatPrefix.at(format) + twosComplementMagnitude(rawStr)
                : parseKnownBits(rawStr);
      break;
    case BusValueFormat::Raw:
    case BusValueFormat::Unknown: throw std::invalid_argument("Invalid output format");
  }

  const bool needsPrefix =
      format != BusValueFormat::Signed && formatPrefix.contains(format);
  const std::size_t padding = fixedWidth > res.size() ? fixedWidth - res.size() : 0;
  return std::format("{}{}{}", needsPrefix ? formatPrefix.at(format) : "",
                     std::string(padding, '0'), res);
}

ParsedBusValue valueFromStr(const std::string_view value)
{
  const auto [format, digits] = getFormat(value);
  BusValue result;

  switch (format) {
    case BusValueFormat::Raw:
      result = digits | std::views::reverse | std::views::transform([](const char digit) {
                 return static_cast<State>(upper(digit));
               })
               | std::ranges::to<BusValue>();
      break;
    case BusValueFormat::Bin:
      result = digits | std::views::reverse | std::views::transform([](const char digit) {
                 return digit == '1' ? State::HIGH : State::LOW;
               })
               | std::ranges::to<BusValue>();
      break;
    case BusValueFormat::Hex:
      for (const char digit : digits | std::views::reverse) {
        const char normalized = upper(digit);
        const int  parsed = normalized >= 'A' ? normalized - 'A' + 10 : normalized - '0';
        for (int bit = 0; bit < 4; ++bit)
          result.push_back((parsed >> bit) & 1 ? State::HIGH : State::LOW);
      }
      break;
    case BusValueFormat::Oct:
      for (const char digit : digits | std::views::reverse) {
        const int parsed = digit - '0';
        for (int bit = 0; bit < 3; ++bit)
          result.push_back((parsed >> bit) & 1 ? State::HIGH : State::LOW);
      }
      break;
    case BusValueFormat::Unsigned:
    case BusValueFormat::Signed: {
      std::string decimal(digits);
      bool        nonZero = true;
      while (nonZero) {
        int remainder = 0;
        nonZero       = false;

        for (char& digit : decimal) {
          const int expanded = digit - '0' + remainder * 10;
          digit              = static_cast<char>(expanded / 2 + '0');
          remainder          = expanded % 2;
          if (digit != '0')
            nonZero = true;
        }
        result.push_back(remainder != 0 ? State::HIGH : State::LOW);
      }

      if (format == BusValueFormat::Signed) {
        result.push_back(State::LOW);
        if (trim(value).front() == '-')
          result = twosComplement(result);
      }
      break;
    }
    case BusValueFormat::Unknown: return {{}, BusValueFormat::Unknown};
  }

  return {std::move(result), format};
}

std::optional<BusValue> resizeParsedValue(const ParsedBusValue& parsed,
                                          const std::size_t     width)
{
  if (parsed.format == BusValueFormat::Unknown || parsed.value.empty())
    return std::nullopt;

  if (parsed.format == BusValueFormat::Signed) {
    if (!SILICON::wireUtils::fitsSigned(parsed.value, width))
      return std::nullopt;
    return SILICON::wireUtils::normalizeBusValue(parsed.value, width,
                                                 parsed.value.back());
  }

  if (!SILICON::wireUtils::fitsUnsigned(parsed.value, width))
    return std::nullopt;

  const State extension = parsed.format == BusValueFormat::Raw && parsed.value.size() == 1
                                  && parsed.value.front() == State::UNKNOWN
                              ? State::UNKNOWN
                              : State::LOW;
  return SILICON::wireUtils::normalizeBusValue(parsed.value, width, extension);
}

}  // namespace SILICON::core
