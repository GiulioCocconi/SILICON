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

#include <regex>
#include <string>
#include <string_view>

namespace SILICON::ui::indentation {

[[nodiscard]] std::string_view trimLeft(std::string_view value);
[[nodiscard]] std::string_view trimRight(std::string_view value);
[[nodiscard]] std::string      leadingWhitespace(std::string_view line);

/** Masks each regex match with spaces before structural matching. */
[[nodiscard]] std::string codePortion(std::string_view  line,
                                      const std::regex& strippedPortions);

[[nodiscard]] bool regexSearch(std::string_view value, const std::regex& expression);
[[nodiscard]] bool endsWithWord(std::string_view value, std::string_view word);

void addIndentLevel(std::string& indentation, int indentWidth);
void removeIndentLevel(std::string& indentation, int indentWidth);

}  // namespace SILICON::ui::indentation
