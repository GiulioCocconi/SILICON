/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace SILICON::project {

enum class CodeFileType { Verilog };

struct CodeFileTypeInfo {
  CodeFileType     type;
  std::string_view extension;
};

[[nodiscard]] std::span<const CodeFileTypeInfo> codeFileTypeRegistry();
[[nodiscard]] const CodeFileTypeInfo&           codeFileTypeInfo(CodeFileType type);
[[nodiscard]] std::optional<CodeFileType> codeFileTypeForPath(std::string_view path);
[[nodiscard]] bool        isValidCodeFilePath(std::string_view path, CodeFileType type);
[[nodiscard]] std::string codeFilePath(std::string_view baseName, CodeFileType type);

}  // namespace SILICON::project
