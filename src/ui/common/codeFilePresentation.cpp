/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "codeFilePresentation.hpp"

#include <array>
#include <ranges>
#include <stdexcept>

namespace SILICON::ui {
namespace {

constexpr std::array Presentations{
    CodeFilePresentation{.type                = project::CodeFileType::Verilog,
                         .displayName         = "Verilog",
                         .kdeSyntaxDefinition = "Verilog"}};

}  // namespace

std::span<const CodeFilePresentation> codeFilePresentations()
{
  return Presentations;
}

const CodeFilePresentation& codeFilePresentation(const project::CodeFileType type)
{
  const auto it = std::ranges::find(Presentations, type, &CodeFilePresentation::type);
  if (it == Presentations.end())
    throw std::invalid_argument("Unknown code-file presentation");
  return *it;
}

}  // namespace SILICON::ui
