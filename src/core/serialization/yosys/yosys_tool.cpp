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

#include "yosys_tool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <logging/logger.hpp>

#ifndef __EMSCRIPTEN__
  #include <boost/asio/io_context.hpp>
  #include <boost/process/v1/args.hpp>
  #include <boost/process/v1/async.hpp>
  #include <boost/process/v1/child.hpp>
  #include <boost/process/v1/io.hpp>
  #include <boost/process/v1/search_path.hpp>

  #if defined(_WIN32)
    #include <windows.h>
  #elif defined(__APPLE__)
    #include <mach-o/dyld.h>
  #endif
#endif

namespace SILICON::yosys {

namespace {

  const SILICON::logging::Logger yosysLog("yosys");

}  // namespace

#ifdef __EMSCRIPTEN__

ScriptResult runScript(const std::string_view script, const ToolOptions& options)
{
  (void)script;
  (void)options;
  yosysLog.error("External execution is unavailable in Emscripten builds");
  throw std::runtime_error(
      "Yosys platform-availability phase failed: external execution is unavailable "
      "when compiled for Emscripten. See the 'yosys' logs for details.");
}

std::string readVerilog(const std::string_view source, const ToolOptions& options)
{
  const std::array sources{InputFile{.path = "source.v", .contents = source}};
  return readVerilog(sources, "source.v", options);
}

std::string readVerilog(const std::span<const InputFile> sources,
                        const std::string_view entryPath, const ToolOptions& options)
{
  (void)sources;
  (void)entryPath;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

std::string elaborateHierarchy(const std::string_view json, const ToolOptions& options)
{
  (void)json;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

std::string writeVerilog(std::string_view json, const ToolOptions& options)
{
  (void)json;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

#else

namespace {

  class TemporaryWorkspace {
  public:
    TemporaryWorkspace()
    {
      static std::atomic_uint64_t sequence      = 0;
      const auto                  temporaryRoot = std::filesystem::temp_directory_path();
      const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

      for (unsigned attempt = 0; attempt < 128; ++attempt) {
        directory =
            temporaryRoot
            / std::format("silicon_yosys_{}_{}_{}", timestamp,
                          sequence.fetch_add(1, std::memory_order_relaxed), attempt);
        std::error_code error;
        if (std::filesystem::create_directory(directory, error))
          return;
        if (error && error != std::errc::file_exists)
          throw std::runtime_error(
              std::format("Yosys temporary-workspace creation failed for '{}': {}",
                          directory.string(), error.message()));
      }
      throw std::runtime_error(
          "Yosys temporary-workspace creation failed: no unique directory was available");
    }

    TemporaryWorkspace(const TemporaryWorkspace&)            = delete;
    TemporaryWorkspace& operator=(const TemporaryWorkspace&) = delete;

    ~TemporaryWorkspace()
    {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return directory; }

  private:
    std::filesystem::path directory;
  };

  void writeFile(const std::filesystem::path& path, const std::string_view contents,
                 const std::string_view phase)
  {
    std::ofstream output(path, std::ios::binary);
    if (!output)
      throw std::runtime_error(
          std::format("Yosys {} failed: could not open '{}'", phase, path.string()));
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output)
      throw std::runtime_error(
          std::format("Yosys {} failed: could not write '{}'", phase, path.string()));
  }

  [[nodiscard]] bool isSafeRelativeSourcePath(const std::string_view path)
  {
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.contains('\\')
        || path.contains("//"))
      return false;

    const std::filesystem::path filesystemPath(path);
    if (filesystemPath.has_root_path())
      return false;
    for (const auto& component : filesystemPath)
      if (component == "." || component == "..")
        return false;

    return std::ranges::none_of(path, [](const unsigned char character) {
      return character < 0x20 || character == 0x7f;
    });
  }

  [[nodiscard]] std::string readFile(const std::filesystem::path& path,
                                     const std::string_view       phase)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error(std::format(
          "Yosys {} failed: could not open generated file '{}'", phase, path.string()));
    std::string contents{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
    if (input.bad())
      throw std::runtime_error(std::format(
          "Yosys {} failed: could not read generated file '{}'", phase, path.string()));
    return contents;
  }

  [[nodiscard]] std::string quotePath(const std::filesystem::path& path)
  {
    std::string result("\"");
    for (const char character : path.string()) {
      if (character == '\\' || character == '"')
        result += '\\';
      result += character;
    }
    return result + '"';
  }

  struct TechnologyLibrary {
    std::filesystem::path cells;
    std::filesystem::path technologyMap;
  };

  [[nodiscard]] std::optional<TechnologyLibrary>
  libraryAt(const std::filesystem::path& directory)
  {
    TechnologyLibrary result{directory / "silicon_cells.v",
                             directory / "silicon_techmap.v"};
    std::error_code   error;
    for (const auto* path : {&result.cells, &result.technologyMap}) {
      if (!std::filesystem::is_regular_file(*path, error) || error)
        return std::nullopt;
    }
    return result;
  }

  [[nodiscard]] std::optional<std::filesystem::path> runningExecutable()
  {
  #if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD  size =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size())
      return std::nullopt;
    path.resize(size);
    return std::filesystem::path(std::move(path));
  #elif defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) != 0)
      return std::nullopt;
    std::error_code error;
    auto            canonical =
        std::filesystem::weakly_canonical(std::filesystem::path(path.data()), error);
    return error ? std::optional(std::filesystem::path(path.data()))
                 : std::optional(std::move(canonical));
  #else
    std::error_code error;
    auto            path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::nullopt : std::optional(std::move(path));
  #endif
  }

  [[nodiscard]] TechnologyLibrary technologyLibrary(const ToolOptions& options)
  {
    if (options.technologyLibraryDirectory) {
      if (const auto result = libraryAt(*options.technologyLibraryDirectory))
        return *result;
      throw std::runtime_error(std::format(
          "Yosys resource-discovery phase failed: configured technology-library "
          "directory '{}' does not contain silicon_cells.v and silicon_techmap.v",
          options.technologyLibraryDirectory->string()));
    }

    std::vector<std::filesystem::path> candidates{
        std::filesystem::path(SILICON_YOSYS_BUILD_RESOURCE_DIR),
        std::filesystem::path(SILICON_YOSYS_INSTALL_RESOURCE_DIR),
    };
    if (const auto executable = runningExecutable()) {
      const auto directory = executable->parent_path();
      candidates.push_back(
          (directory / ".." / SILICON_YOSYS_INSTALL_RELATIVE_RESOURCE_DIR)
              .lexically_normal());
  #if defined(__APPLE__)
      candidates.push_back((directory / ".." / "Resources" / "yosys").lexically_normal());
  #endif
    }
    for (const auto& candidate : candidates)
      if (const auto result = libraryAt(candidate))
        return *result;

    throw std::runtime_error(std::format(
        "Yosys resource-discovery phase failed: the packaged SILICON technology "
        "library was not found at '{}' or '{}'",
        candidates[0].string(), candidates[1].string()));
  }

  [[nodiscard]] std::optional<std::filesystem::path> verilogPlugin()
  {
    std::vector<std::filesystem::path> candidates;
  #if defined(SILICON_YOSYS_BUILD_PLUGIN) && defined(SILICON_YOSYS_INSTALL_PLUGIN) \
      && defined(SILICON_YOSYS_INSTALL_RELATIVE_PLUGIN)
    candidates = {std::filesystem::path(SILICON_YOSYS_BUILD_PLUGIN),
                  std::filesystem::path(SILICON_YOSYS_INSTALL_PLUGIN)};
    if (const auto executable = runningExecutable()) {
      candidates.push_back(
          (executable->parent_path() / ".." / SILICON_YOSYS_INSTALL_RELATIVE_PLUGIN)
              .lexically_normal());
    }
  #endif
    for (const auto& candidate : candidates) {
      std::error_code error;
      if (std::filesystem::is_regular_file(candidate, error) && !error)
        return candidate;
    }
    return std::nullopt;
  }

  void logCapturedOutput(const ScriptResult& result, const bool failed)
  {
    if (!result.standardOutput.empty())
      yosysLog.info(std::format("stdout:\n{}", result.standardOutput));
    if (!result.standardError.empty()) {
      const auto message = std::format("stderr:\n{}", result.standardError);
      if (failed)
        yosysLog.error(message);
      else
        yosysLog.warning(message);
    }
  }

}  // namespace

ScriptResult runScript(const std::string_view script, const ToolOptions& options)
{
  namespace process = boost::process::v1;

  std::filesystem::path executable;
  if (options.executable) {
    std::error_code error;
    const auto      status = std::filesystem::status(*options.executable, error);

    if (error || !std::filesystem::exists(status)
        || !std::filesystem::is_regular_file(status)) {
      throw std::runtime_error(std::format(
          "Yosys executable-validation phase failed: configured executable '{}' is "
          "missing or invalid. See the 'yosys' logs for details.",
          options.executable->string()));
    }
    executable = *options.executable;
  } else {
    executable = process::search_path("yosys").string();
    if (executable.empty()) {
      yosysLog.error("Executable 'yosys' was not found on PATH");
      throw std::runtime_error(
          "Yosys executable-discovery phase failed: 'yosys' was not found on PATH. "
          "See the 'yosys' logs for details.");
    }
  }

  boost::asio::io_context         ioContext;
  std::future<std::string>        standardOutput;
  std::future<std::string>        standardError;
  std::unique_ptr<process::child> child;
  try {
    child = std::make_unique<process::child>(
        executable.string(), process::args({"-Q", "-p", std::string(script)}),
        process::std_out > standardOutput, process::std_err > standardError, ioContext);
  } catch (const std::exception& error) {
    yosysLog.error(std::format("Process creation failed for '{}': {}",
                               executable.string(), error.what()));
    throw std::runtime_error(
        "Yosys process-creation phase failed. See the 'yosys' logs for details.");
  }

  try {
    ioContext.run();
    child->wait();
  } catch (const std::exception& error) {
    try {
      if (child->running())
        child->terminate();
      child->wait();
    } catch (const std::exception&) {
    }
    yosysLog.error(std::format("Output capture failed: {}", error.what()));
    throw std::runtime_error(
        "Yosys output-capture phase failed. See the 'yosys' logs for details.");
  }

  ScriptResult result;
  try {
    result = {standardOutput.get(), standardError.get()};
  } catch (const std::exception& error) {
    yosysLog.error(std::format("Output capture failed after exit status {}: {}",
                               child->exit_code(), error.what()));
    throw std::runtime_error(
        "Yosys output-capture phase failed. See the 'yosys' logs for details.");
  }

  const int exitStatus = child->exit_code();
  logCapturedOutput(result, exitStatus != 0);
  if (exitStatus != 0) {
    yosysLog.error(std::format("Command failed with exit status {}", exitStatus));
    throw std::runtime_error(std::format(
        "Yosys script-execution phase failed with exit status {}. See the 'yosys' "
        "logs for command output.",
        exitStatus));
  }
  return result;
}

std::string readVerilog(const std::string_view source, const ToolOptions& options)
{
  const std::array sources{InputFile{.path = "source.v", .contents = source}};
  return readVerilog(sources, "source.v", options);
}

std::string readVerilog(const std::span<const InputFile> sources,
                        const std::string_view entryPath, const ToolOptions& options)
{
  if (!isSafeRelativeSourcePath(entryPath))
    throw std::invalid_argument("Verilog entry path must be a safe relative path");

  std::unordered_set<std::string> paths;
  for (const auto& source : sources) {
    if (!isSafeRelativeSourcePath(source.path))
      throw std::invalid_argument(std::format(
          "Verilog source path '{}' must be a safe relative path", source.path));
    if (!paths.emplace(source.path).second)
      throw std::invalid_argument(
          std::format("Duplicate Verilog source path: {}", source.path));
  }
  if (!paths.contains(std::string(entryPath)))
    throw std::invalid_argument(
        std::format("Verilog entry source is missing: {}", entryPath));

  TemporaryWorkspace workspace;
  for (const auto& source : sources) {
    const auto      sourcePath = workspace.path() / std::filesystem::path(source.path);
    std::error_code error;
    std::filesystem::create_directories(sourcePath.parent_path(), error);
    if (error)
      throw std::runtime_error(std::format(
          "Yosys Verilog-read input-writing phase failed: could not create '{}': {}",
          sourcePath.parent_path().string(), error.message()));
    writeFile(sourcePath, source.contents, "Verilog-read input-writing phase");
  }

  const auto sourcePath = workspace.path() / std::filesystem::path(entryPath);
  const auto jsonPath   = workspace.path() / "design.json";

  (void)runScript(std::format("read_verilog {}\n"
                              // `proc` converts processes ($dff/$mux cells) so the JSON
                              // backend can emit them; it is required before write_json
                              // even though readVerilog performs no further elaboration.
                              "proc\n"
                              "write_json {}\n",
                              quotePath(sourcePath), quotePath(jsonPath)),
                  options);
  return readFile(jsonPath, "Verilog-read output-reading phase");
}

std::string elaborateHierarchy(const std::string_view json, const ToolOptions& options)
{
  TemporaryWorkspace workspace;
  const auto         inputPath  = workspace.path() / "design.json";
  const auto         outputPath = workspace.path() / "elaborated.json";
  writeFile(inputPath, json, "Yosys-elaboration input-writing phase");

  const auto library = technologyLibrary(options);
  const auto plugin  = verilogPlugin();
  const auto pluginLoad =
      plugin ? std::format("plugin -i {}\n", quotePath(*plugin)) : std::string();
  const auto muxImport =
      plugin ? std::string("silicon_pmux_bmux\nsilicon_eq_decoder\n") : std::string();

  (void)runScript(std::format("read_verilog -lib -D SILICON_BLACKBOX {}\n"
                              "read_json {}\n"
                              "{}"
                              "hierarchy -check\n"
                              "proc\n"
                              "muxpack\n"
                              "{}"
                              "pmuxtree\n"
                              "delete t:$scopeinfo\n"
                              "opt\n"
                              // Preserve vector bitwise operations as one native Silicon
                              // gate. Only scalar forms participate in full/half-adder
                              // extraction; otherwise unrelated ALU result lanes such as
                              // A&B and A^B are incorrectly expanded into many adder
                              // primitives merely because they share operands.
                              "simplemap t:$and r:Y_WIDTH=1 %i "
                              "t:$or r:Y_WIDTH=1 %i "
                              "t:$xor r:Y_WIDTH=1 %i "
                              "t:$not r:Y_WIDTH=1 %i "
                              "t:$logic_not t:$logic_and t:$logic_or "
                              "t:$reduce_and t:$reduce_or t:$reduce_xor\n"
                              "extract_fa\n"
                              "techmap -map {}\n"
                              "opt_clean\n"
                              "write_json {}\n",
                              quotePath(library.cells), quotePath(inputPath), pluginLoad,
                              muxImport, quotePath(library.technologyMap),
                              quotePath(outputPath)),
                  options);
  return readFile(outputPath, "Yosys-elaboration output-reading phase");
}

std::string writeVerilog(std::string_view json, const ToolOptions& options)
{
  TemporaryWorkspace workspace;
  const auto         library     = technologyLibrary(options);
  const auto         jsonPath    = workspace.path() / "design.json";
  const auto         verilogPath = workspace.path() / "design.v";
  writeFile(jsonPath, json, "Verilog-export input-writing phase");

  const auto plugin = verilogPlugin();
  const auto muxExport =
      plugin ? std::format("plugin -i {}\nsilicon_bmux_case\n", quotePath(*plugin))
             : std::string();

  (void)runScript(std::format("read_verilog -lib -D SILICON_BLACKBOX {}\n"
                              "read_json {}\n"
                              "hierarchy -check -auto-top\n"
                              "techmap -autoproc -D SILICON_EXPORT_MAP -map {}\n"
                              "opt_expr t:$pos\n"
                              "opt_clean -purge\n"
                              "rename -unescape\n"
                              "opt\n"
                              "{}"
                              "rename -enumerate\n"
                              "write_verilog -noattr -norename -decimal {}\n",
                              quotePath(library.cells), quotePath(jsonPath),
                              quotePath(library.cells), muxExport,
                              quotePath(verilogPath)),
                  options);
  return readFile(verilogPath, "Verilog-export output-reading phase");
}

#endif

}  // namespace SILICON::yosys
