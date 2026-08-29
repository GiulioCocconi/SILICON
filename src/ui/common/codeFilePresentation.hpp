/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <span>
#include <string_view>

#include <core/codeFile.hpp>

namespace SILICON::ui {

/** Editor-only metadata for a project source-file type. */
struct CodeFilePresentation {
  SILICON::project::CodeFileType type;
  std::string_view               displayName;
  std::string_view               kdeSyntaxDefinition;
};

[[nodiscard]] std::span<const CodeFilePresentation> codeFilePresentations();
[[nodiscard]] const CodeFilePresentation&
codeFilePresentation(SILICON::project::CodeFileType type);

}  // namespace SILICON::ui
