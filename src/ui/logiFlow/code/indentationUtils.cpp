/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <ui/logiFlow/code/indentationUtils.hpp>

#include <algorithm>
#include <cctype>

namespace SILICON::project::indentation {

std::string_view trimLeft(std::string_view value)
{
  const auto first = std::ranges::find_if_not(
      value, [](const unsigned char character) { return std::isspace(character); });
  value.remove_prefix(static_cast<std::size_t>(std::distance(value.begin(), first)));
  return value;
}

std::string_view trimRight(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}

std::string leadingWhitespace(const std::string_view line)
{
  const auto first = std::ranges::find_if_not(line, [](const unsigned char character) {
    return character == ' ' || character == '\t';
  });
  return std::string(line.begin(), first);
}

std::string codePortion(const std::string_view line, const std::regex& strippedPortions)
{
  std::string result(line);
  using Iterator = std::string_view::const_iterator;
  for (std::regex_iterator<Iterator> matches(line.begin(), line.end(), strippedPortions),
       end;
       matches != end; ++matches) {
    const auto start  = static_cast<std::size_t>(matches->position());
    const auto length = static_cast<std::size_t>(matches->length());
    std::fill_n(result.begin() + static_cast<std::ptrdiff_t>(start), length, ' ');
  }
  return result;
}

bool regexSearch(const std::string_view value, const std::regex& expression)
{
  return std::regex_search(value.begin(), value.end(), expression);
}

bool endsWithWord(const std::string_view value, const std::string_view word)
{
  const auto trimmed = trimRight(value);
  if (!trimmed.ends_with(word))
    return false;
  const auto start = trimmed.size() - word.size();
  return start == 0
         || !(std::isalnum(static_cast<unsigned char>(trimmed[start - 1]))
              || trimmed[start - 1] == '_');
}

void addIndentLevel(std::string& indentation)
{
  indentation.push_back('\t');
}

void removeIndentLevel(std::string& indentation)
{
  if (const auto tab = indentation.rfind('\t'); tab != std::string::npos)
    indentation.erase(tab, 1);
}

}  // namespace SILICON::project::indentation
