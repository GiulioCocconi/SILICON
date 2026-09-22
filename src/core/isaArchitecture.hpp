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
#include <vector>

#include <sisl/sisl.hpp>

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

/** Outcome of parsing a SISL architecture source into a usable instruction set. */
struct ArchitectureBuildResult {
  /** Populated only when the whole source was parsed successfully. */
  std::optional<sisl::Isa> isa;
  /** Diagnostics reported by SISL, preserved verbatim with their source locations. */
  std::vector<sisl::Diagnostic> diagnostics;
  /** Domain error text for failures SISL does not describe, e.g. a name mismatch. */
  std::optional<std::string> error;

  [[nodiscard]] bool success() const noexcept { return isa.has_value(); }
  explicit           operator bool() const noexcept { return success(); }
};

/**
 * Validates that the leading declaration matches @p expectedName and then builds
 * the architecture with SISL. This is the single definition of a successful
 * architecture build shared by every consumer.
 *
 * A std::nullopt @p expectedName skips the declaration check, which is only
 * correct for sources whose architecture name is not known yet. A declaration
 * mismatch is reported through ArchitectureBuildResult::error without parsing,
 * so the source is fully parsed at most once.
 */
[[nodiscard]] ArchitectureBuildResult
buildArchitecture(std::optional<std::string> expectedName, std::string_view source);

}  // namespace SILICON::project
