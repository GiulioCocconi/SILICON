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

#include <optional>
#include <string>
#include <string_view>

namespace SILICON::project {

/** Checks that a ZIP path segment cannot escape its architecture directory. */
[[nodiscard]] bool isArchitecturePathSlug(std::string_view name);
/** Validates an architecture identifier with SISL itself. */
[[nodiscard]] bool isValidArchitectureName(std::string_view name);
/** Reads only the leading declaration so unfinished imports can still be edited. */
[[nodiscard]] std::optional<std::string>
declaredArchitectureName(std::string_view source);
/** Updates a matching leading declaration while preserving the rest of the source. */
[[nodiscard]] std::string renameArchitectureDeclaration(std::string_view source,
                                                        std::string_view oldName,
                                                        std::string_view newName);
/** Throws when a readable declaration disagrees with its containing directory. */
void                      validateArchitectureDeclaration(std::string_view directoryName,
                                                          std::string_view source);
[[nodiscard]] std::string architectureDirectory(std::string_view name);

}  // namespace SILICON::project
