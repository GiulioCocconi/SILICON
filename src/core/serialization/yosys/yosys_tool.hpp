/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace SILICON::yosys {

struct ToolOptions {
  std::optional<std::filesystem::path> executable;
  std::optional<std::filesystem::path> technologyLibraryDirectory;
};

struct ScriptResult {
  std::string standardOutput;
  std::string standardError;
};

/** A named in-memory input materialized in a temporary Yosys workspace. */
struct InputFile {
  std::string_view path;
  std::string_view contents;
};

[[nodiscard]] ScriptResult runScript(std::string_view   script,
                                     const ToolOptions& options = {});

/** Run the Yosys Verilog frontend and return unelaborated write_json output. */
[[nodiscard]] std::string readVerilog(std::string_view   source,
                                      const ToolOptions& options = {});
[[nodiscard]] std::string readVerilog(std::span<const InputFile> sources,
                                      std::string_view           entryPath,
                                      const ToolOptions&         options = {});

/** Apply SILICON's language-independent Yosys JSON import lowering. */
[[nodiscard]] std::string elaborateHierarchy(std::string_view   json,
                                             const ToolOptions& options = {});

/** Run the Yosys Verilog backend and return its raw source output. */
[[nodiscard]] std::string writeVerilog(std::string_view   json,
                                       const ToolOptions& options = {});

}  // namespace SILICON::yosys
