/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "codeFile.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <stdexcept>

namespace SILICON::project {
namespace {

  constexpr std::array Registry{CodeFileTypeInfo{.type        = CodeFileType::Verilog,
                                                 .displayName = "Verilog",
                                                 .extension   = ".v",
                                                 .kdeSyntaxDefinition = "Verilog"}};

}  // namespace

std::span<const CodeFileTypeInfo> codeFileTypeRegistry()
{
  return Registry;
}

const CodeFileTypeInfo& codeFileTypeInfo(const CodeFileType type)
{
  const auto it = std::ranges::find(Registry, type, &CodeFileTypeInfo::type);
  if (it == Registry.end())
    throw std::invalid_argument("Unknown code file type");
  return *it;
}

std::optional<CodeFileType> codeFileTypeForPath(const std::string_view path)
{
  const auto it = std::ranges::find_if(
      Registry, [path](const CodeFileTypeInfo& info) {
        return isValidCodeFilePath(path, info.type);
      });
  return it == Registry.end() ? std::nullopt : std::optional(it->type);
}

bool isValidCodeFilePath(const std::string_view path, const CodeFileType type)
{
  constexpr std::string_view prefix = "code/";
  if (!path.starts_with(prefix) || path.front() == '/' || path.back() == '/'
      || path.contains('\\') || path.contains(".."))
    return false;

  const auto fileName = path.substr(prefix.size());
  if (fileName.empty() || fileName.contains('/'))
    return false;

  const auto extension = codeFileTypeInfo(type).extension;
  if (fileName.size() <= extension.size() || !fileName.ends_with(extension))
    return false;

  return std::ranges::none_of(path, [](const unsigned char character) {
    return character < 0x20 || character == 0x7f;
  });
}

std::string codeFilePath(const std::string_view baseName, const CodeFileType type)
{
  return std::format("code/{}{}", baseName, codeFileTypeInfo(type).extension);
}

}  // namespace SILICON::project
