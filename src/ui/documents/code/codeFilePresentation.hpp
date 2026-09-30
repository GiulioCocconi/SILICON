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

#include <span>
#include <string_view>

#include <core/projectDocument.hpp>
#include <ui/documents/code/codeSyntax.hpp>

namespace SILICON::ui {

/** Editor-only metadata for a project source-file type. */
struct CodeFilePresentation {
  SILICON::project::DocumentType type;
  std::string_view               displayName;
  const CodeSyntax*              syntax;
  const char*                    architectureTabName;
};

[[nodiscard]] std::span<const CodeFilePresentation> codeFilePresentations();
[[nodiscard]] const CodeFilePresentation&
codeFilePresentation(SILICON::project::DocumentType type);

}  // namespace SILICON::ui
