/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <span>
#include <string>
#include <string_view>

#include <core/serialization/yosys/yosys_tool.hpp>

namespace SILICON::core {
class Circuit;
}

namespace SILICON::verilog {

using SourceFile = yosys::InputFile;

[[nodiscard]] std::string read(std::string_view          source,
                               const yosys::ToolOptions& options = {});
[[nodiscard]] std::string read(std::span<const SourceFile> sources,
                               std::string_view            entryPath,
                               const yosys::ToolOptions&   options = {});

/** Normalize raw Yosys write_verilog output into SILICON's readable form. */
[[nodiscard]] std::string postprocess(std::string_view source);

[[nodiscard]] std::string write(std::string_view          yosysJson,
                                const yosys::ToolOptions& options = {});
[[nodiscard]] std::string write(const core::Circuit&      circuit,
                                const yosys::ToolOptions& options = {});

}  // namespace SILICON::verilog
