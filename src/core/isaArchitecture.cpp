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

#include "isaArchitecture.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <sisl/sisl.hpp>

namespace SILICON::project {

bool isArchitecturePathSlug(const std::string_view name)
{
  const auto letter = [](const char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
  };
  const auto digit = [](const char ch) { return ch >= '0' && ch <= '9'; };
  if (name.empty() || !(letter(name.front()) || name.front() == '_'))
    return false;
  return std::ranges::all_of(name.substr(1), [&](const char ch) {
    return letter(ch) || digit(ch) || ch == '_' || ch == '-';
  });
}

bool isValidArchitectureName(const std::string_view name)
{
  if (!isArchitecturePathSlug(name))
    return false;
  try {
    [[maybe_unused]] const auto isa =
        sisl::Isa::load_string(std::format("arch {} = {{}};", name));
  } catch (const sisl::Error&) {
    return false;
  }
  return true;
}

namespace {

  std::optional<std::pair<std::size_t, std::size_t>>
  declarationNameRange(const std::string_view source)
  {
    std::size_t pos        = 0;
    const auto  skipTrivia = [&]() -> bool {
      for (;;) {
        while (pos < source.size()
               && std::isspace(static_cast<unsigned char>(source[pos])))
          ++pos;
        if (source.substr(pos).starts_with("//")) {
          const auto end = source.find('\n', pos + 2);
          pos            = end == std::string_view::npos ? source.size() : end + 1;
        } else if (source.substr(pos).starts_with("/*")) {
          const auto end = source.find("*/", pos + 2);
          if (end == std::string_view::npos)
            return false;
          pos = end + 2;
        } else {
          return true;
        }
      }
    };
    if (!skipTrivia())
      return std::nullopt;
    if (!source.substr(pos).starts_with("arch"))
      return std::nullopt;
    pos += 4;
    if (pos == source.size()
        || !(std::isspace(static_cast<unsigned char>(source[pos]))
             || source.substr(pos).starts_with("//")
             || source.substr(pos).starts_with("/*")))
      return std::nullopt;
    if (!skipTrivia())
      return std::nullopt;
    const auto start = pos;
    while (pos < source.size()) {
      const char ch = source[pos];
      if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
        break;
      ++pos;
    }
    const auto name = source.substr(start, pos - start);
    return isValidArchitectureName(name) ? std::optional(std::pair{start, pos - start})
                                         : std::nullopt;
  }

}  // namespace

std::optional<std::string> declaredArchitectureName(const std::string_view source)
{
  const auto range = declarationNameRange(source);
  return range ? std::optional(std::string(source.substr(range->first, range->second)))
               : std::nullopt;
}

std::string renameArchitectureDeclaration(const std::string_view source,
                                          const std::string_view oldName,
                                          const std::string_view newName)
{
  std::string result(source);
  if (const auto range = declarationNameRange(source);
      range && source.substr(range->first, range->second) == oldName)
    result.replace(range->first, range->second, newName);
  return result;
}

void validateArchitectureDeclaration(const std::string_view directoryName,
                                     const std::string_view source)
{
  const auto declared = declaredArchitectureName(source);
  if (declared && *declared != directoryName)
    throw std::invalid_argument(
        std::format("architecture declaration '{}' does not match directory '{}'",
                    *declared, directoryName));
}

std::string architectureDirectory(const std::string_view name)
{
  return std::format("isa/{}/", name);
}

}  // namespace SILICON::project
