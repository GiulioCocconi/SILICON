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

#include "codeFilePresentation.hpp"

#include <array>
#include <ranges>
#include <stdexcept>

namespace SILICON::ui {
namespace {

  constexpr std::array PRESENTATIONS{
      CodeFilePresentation{.type                = project::DocumentType::Verilog,
                           .displayName         = "Verilog",
                           .syntax              = &VERILOG_CODE_SYNTAX,
                           .architectureTabName = nullptr},
      CodeFilePresentation{.type                = project::DocumentType::Sisl,
                           .displayName         = "SISL",
                           .syntax              = &SISL_CODE_SYNTAX,
                           .architectureTabName = "Instruction format"}};

}  // namespace

std::span<const CodeFilePresentation> codeFilePresentations()
{
  return PRESENTATIONS;
}

const CodeFilePresentation& codeFilePresentation(const project::DocumentType type)
{
  const auto it = std::ranges::find(PRESENTATIONS, type, &CodeFilePresentation::type);
  if (it == PRESENTATIONS.end())
    throw std::invalid_argument("Unknown code-file presentation");
  return *it;
}

}  // namespace SILICON::ui
