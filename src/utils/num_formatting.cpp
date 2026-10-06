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

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ios>
#include <iterator>
#include <map>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/wireUtils.hpp>

namespace SILICON::core {
namespace {

  using BigInt = boost::multiprecision::cpp_int;

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

  bool satisfiesAlphabet(const std::string_view DIGITS, const BusValueFormat format)
  {
    const auto alphabet = formatAlphabet.at(format);

    return !DIGITS.empty() && std::ranges::all_of(DIGITS, [alphabet](const char digit) {
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

      const auto DIGITS = value.substr(prefix.size());
      if (satisfiesAlphabet(DIGITS, format))
        return {format, DIGITS};
    }

    for (const auto& [format, _] : formatAlphabet) {
      if (formatPrefix.contains(format))
        continue;

      auto DIGITS = value;
      if (satisfiesAlphabet(DIGITS, format))
        return {format, DIGITS};
    }
    return {BusValueFormat::Unknown, {}};
  }

  BigInt parseMagnitude(const std::string_view DIGITS, const BusValueFormat format)
  {
    if (format == BusValueFormat::Bin) {
      std::vector<unsigned char> bits;
      bits.reserve(DIGITS.size());
      std::ranges::transform(DIGITS, std::back_inserter(bits),
                             [](const char bit) { return bit == '1'; });

      BigInt result;
      boost::multiprecision::import_bits(result, bits.begin(), bits.end(), 1, true);
      return result;
    }

    auto firstNonZero = DIGITS.find_first_not_of('0');
    if (firstNonZero == std::string_view::npos)
      return 0;

    std::string encoded(DIGITS.substr(firstNonZero));
    switch (format) {
      case BusValueFormat::Hex: encoded.insert(0, "0x"); break;
      case BusValueFormat::Oct: encoded.insert(0, "0"); break;
      case BusValueFormat::Signed:
      case BusValueFormat::Unsigned: break;
      default: throw std::invalid_argument("Invalid integer input format");
    }

    return BigInt(encoded);
  }

  BigInt unsignedValue(const BusValue& value)
  {
    std::vector<unsigned char> bits;
    bits.reserve(value.size());
    std::ranges::transform(value, std::back_inserter(bits),
                           [](const State state) { return state == State::HIGH; });

    BigInt result;
    boost::multiprecision::import_bits(result, bits.begin(), bits.end(), 1, false);
    return result;
  }

  BusValue busValueFromMagnitude(const BigInt& value, const std::size_t width)
  {
    BusValue                   result(width, State::LOW);
    std::vector<unsigned char> bits;
    boost::multiprecision::export_bits(value, std::back_inserter(bits), 1, false);

    std::ranges::transform(bits, result.begin(), [](const unsigned char bit) {
      return bit == 0 ? State::LOW : State::HIGH;
    });
    return result;
  }

  std::size_t magnitudeWidth(const BigInt& value)
  {
    return value == 0 ? 1
                      : static_cast<std::size_t>(boost::multiprecision::msb(value)) + 1;
  }

  std::string formatBigInt(const BigInt& value, const BusValueFormat format)
  {
    switch (format) {
      case BusValueFormat::Signed:
      case BusValueFormat::Unsigned: return value.str();
      case BusValueFormat::Hex:
        return value.str(0, std::ios_base::hex | std::ios_base::uppercase);
      case BusValueFormat::Oct: return value.str(0, std::ios_base::oct);
      default: throw std::invalid_argument("Invalid integer output format");
    }
  }

}  // namespace

std::string formatInteger(std::uint64_t value, const BusValueFormat format,
                          const std::size_t bitWidth)
{
  if (bitWidth == 0 || bitWidth > 64)
    throw std::invalid_argument("Integer bit width must be between 1 and 64");

  const BigInt modulus = BigInt{1} << bitWidth;
  BigInt       number  = BigInt{value} & (modulus - 1);

  switch (format) {
    case BusValueFormat::Signed: {
      if ((number & (BigInt{1} << (bitWidth - 1))) != 0)
        number -= modulus;
      return formatBigInt(number, format);
    }
    case BusValueFormat::Unsigned: return formatBigInt(number, format);
    case BusValueFormat::Bin: {
      const auto bits = busValueFromMagnitude(number, bitWidth);
      return bits | std::views::reverse | std::views::transform([](const State state) {
               return static_cast<char>(std::to_underlying(state));
             })
             | std::ranges::to<std::string>();
    }
    case BusValueFormat::Oct:
    case BusValueFormat::Hex: return formatBigInt(number, format);
    case BusValueFormat::Raw:
    case BusValueFormat::Unknown:
      throw std::invalid_argument("Invalid integer output format");
  }
  throw std::invalid_argument("Invalid integer output format");
}

std::optional<std::uint64_t> parseInteger(std::string_view     text,
                                          const BusValueFormat format,
                                          const std::size_t    bitWidth)
{
  if (bitWidth == 0 || bitWidth > 64)
    return std::nullopt;
  text = trim(text);
  if (text.empty())
    return std::nullopt;

  const BigInt modulus = BigInt{1} << bitWidth;
  if (format == BusValueFormat::Signed) {
    bool negative = false;
    if (text.front() == '+' || text.front() == '-') {
      negative = text.front() == '-';
      text.remove_prefix(1);
    }
    if (!satisfiesAlphabet(text, format))
      return std::nullopt;

    BigInt value = parseMagnitude(text, format);
    if (negative)
      value = -value;

    const BigInt minimum = -(BigInt{1} << (bitWidth - 1));
    const BigInt maximum = (BigInt{1} << (bitWidth - 1)) - 1;
    if (value < minimum || value > maximum)
      return std::nullopt;
    if (value < 0)
      value += modulus;
    return value.convert_to<std::uint64_t>();
  }

  if (format == BusValueFormat::Unsigned && text.front() == '+')
    text.remove_prefix(1);

  if (format != BusValueFormat::Unsigned && format != BusValueFormat::Bin
      && format != BusValueFormat::Oct && format != BusValueFormat::Hex)
    return std::nullopt;

  if (format == BusValueFormat::Bin && startsWithIgnoreCase(text, "0b"))
    text.remove_prefix(2);
  else if (format == BusValueFormat::Oct && startsWithIgnoreCase(text, "0o"))
    text.remove_prefix(2);
  else if (format == BusValueFormat::Hex && startsWithIgnoreCase(text, "0x"))
    text.remove_prefix(2);

  if (!satisfiesAlphabet(text, format))
    return std::nullopt;
  const BigInt value = parseMagnitude(text, format);
  if (value >= modulus)
    return std::nullopt;
  return value.convert_to<std::uint64_t>();
}

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

  const auto [format, DIGITS] = getFormat(bits);
  // Only valid formats are those which satisfy the raw alphabet
  if (format != BusValueFormat::Raw && !satisfiesAlphabet(DIGITS, BusValueFormat::Raw))
    throw std::invalid_argument("Raw bus values may contain only 0, 1, X, or E");

  return DIGITS | std::views::reverse | std::views::transform([](const char digit) {
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

  const BigInt unsignedNumber = unsignedValue(value);
  std::string  res;
  switch (format) {
    case BusValueFormat::Hex:
    case BusValueFormat::Oct: res = formatBigInt(unsignedNumber, format); break;
    case BusValueFormat::Bin: res = rawStr; break;
    case BusValueFormat::Unsigned: res = formatBigInt(unsignedNumber, format); break;
    case BusValueFormat::Signed: {
      const BigInt signedNumber = value.back() == State::HIGH
                                      ? unsignedNumber - (BigInt{1} << value.size())
                                      : unsignedNumber;
      res                       = formatBigInt(signedNumber, format);
      break;
    }
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
  const auto [format, DIGITS] = getFormat(value);
  BusValue result;

  switch (format) {
    case BusValueFormat::Raw:
      result = DIGITS | std::views::reverse | std::views::transform([](const char digit) {
                 return static_cast<State>(upper(digit));
               })
               | std::ranges::to<BusValue>();
      break;
    case BusValueFormat::Bin:
      result = busValueFromMagnitude(parseMagnitude(DIGITS, format), DIGITS.size());
      break;
    case BusValueFormat::Hex:
      result = busValueFromMagnitude(parseMagnitude(DIGITS, format), DIGITS.size() * 4);
      break;
    case BusValueFormat::Oct:
      result = busValueFromMagnitude(parseMagnitude(DIGITS, format), DIGITS.size() * 3);
      break;
    case BusValueFormat::Unsigned:
    case BusValueFormat::Signed: {
      const BigInt magnitude = parseMagnitude(DIGITS, format);
      result = busValueFromMagnitude(magnitude, magnitudeWidth(magnitude));

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
