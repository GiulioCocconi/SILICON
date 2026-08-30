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

#include "yosys.hpp"

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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/circuit.hpp>
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

using namespace SILICON::core;

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
  (void)source;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

std::string elaborateHierarchy(const std::string_view json, const ToolOptions& options)
{
  (void)json;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

std::string exportVerilog(std::string_view json, const ToolOptions& options)
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

  [[nodiscard]] bool isVerilogIdentifier(const std::string_view identifier)
  {
    if (identifier.empty())
      return false;
    const auto isLetter = [](const char character) {
      return (character >= 'a' && character <= 'z')
             || (character >= 'A' && character <= 'Z') || character == '_';
    };
    const auto isDigit = [](const char character) {
      return character >= '0' && character <= '9';
    };
    if (!isLetter(identifier.front()))
      return false;
    for (const char character : identifier.substr(1))
      if (!isLetter(character) && !isDigit(character) && character != '$')
        return false;
    return true;
  }

  [[nodiscard]] std::string_view trim(const std::string_view value)
  {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
      return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
  }

  struct PortDeclaration {
    std::string name;
    std::string ansi;
  };

  [[nodiscard]] std::optional<PortDeclaration>
  parsePortDeclaration(const std::string_view line)
  {
    const auto value = trim(line);
    for (const std::string_view direction : {"input", "output", "inout"}) {
      const auto prefix = std::format("{} ", direction);
      if (!value.starts_with(prefix) || !value.ends_with(';'))
        continue;

      const auto body =
          trim(value.substr(prefix.size(), value.size() - prefix.size() - 1));
      const auto separator = body.find_last_of(" \t");
      const auto name =
          separator == std::string_view::npos ? body : trim(body.substr(separator + 1));
      if (!isVerilogIdentifier(name))
        return std::nullopt;
      return PortDeclaration{std::string(name), std::format("{} {}", direction, body)};
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<std::string>
  parseNetDeclarationName(const std::string_view line, const std::string_view kind)
  {
    const auto prefix = std::format("{} ", kind);
    const auto value  = trim(line);
    if (!value.starts_with(prefix) || !value.ends_with(';'))
      return std::nullopt;

    const auto body = trim(value.substr(prefix.size(), value.size() - prefix.size() - 1));
    const auto separator = body.find_last_of(" \t");
    const auto name =
        separator == std::string_view::npos ? body : trim(body.substr(separator + 1));
    if (!isVerilogIdentifier(name))
      return std::nullopt;
    return std::string(name);
  }

  [[nodiscard]] std::vector<std::string> splitLines(const std::string_view source)
  {
    std::vector<std::string> lines;
    for (std::size_t begin = 0; begin < source.size();) {
      const auto end = source.find('\n', begin);
      if (end == std::string_view::npos) {
        lines.emplace_back(source.substr(begin));
        break;
      }
      lines.emplace_back(source.substr(begin, end - begin));
      begin = end + 1;
    }
    return lines;
  }

  [[nodiscard]] std::string cleanProcessVerilog(const std::string_view source)
  {
    auto                            lines = splitLines(source);
    std::unordered_set<std::string> backendGuards;
    std::vector<bool>               removed(lines.size(), false);

    for (std::size_t line = 0; line < lines.size(); ++line) {
      const auto value = trim(lines[line]);
      if (!value.starts_with("reg \\$auto$verilog_backend.cc:")
          || value.find(":dump_module$") == std::string_view::npos)
        continue;

      const auto begin = value.find('\\');
      const auto end   = value.find_first_of(" \t", begin);
      if (begin != std::string_view::npos && end != std::string_view::npos) {
        backendGuards.emplace(value.substr(begin, end - begin));
        removed[line] = true;
      }
    }

    for (std::size_t line = 0; line < lines.size(); ++line) {
      for (const auto& guard : backendGuards) {
        if (lines[line].find("if (" + guard + " ) begin end") != std::string::npos) {
          removed[line] = true;
          break;
        }
      }

      const auto casePosition = lines[line].find("casez (");
      if (casePosition != std::string::npos)
        lines[line].replace(casePosition, std::string_view("casez").size(), "case");
    }

    std::string result;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (removed[line])
        continue;
      result += lines[line];
      result += '\n';
    }
    return result;
  }

  /**
   * Convert the predictable non-ANSI declarations emitted by write_verilog into one
   * ANSI module header. A module is left untouched unless every listed port has one
   * unambiguous direction declaration, preventing a partial rewrite of unfamiliar
   * Yosys output. Only matching port wires are removed; internal nets are preserved.
   */
  [[nodiscard]] std::string useAnsiPortDeclarations(const std::string_view source)
  {
    auto              lines = splitLines(source);
    std::vector<bool> removed(lines.size(), false);
    for (std::size_t moduleLine = 0; moduleLine < lines.size(); ++moduleLine) {
      const auto header = trim(lines[moduleLine]);
      if (!header.starts_with("module ") || !header.ends_with(");"))
        continue;

      const auto open = header.find('(');
      if (open == std::string_view::npos)
        continue;
      const auto portList = header.substr(open + 1, header.size() - open - 3);

      std::vector<std::string> ports;
      for (std::size_t begin = 0; begin <= portList.size();) {
        const auto end  = portList.find(',', begin);
        const auto port = trim(portList.substr(begin, end == std::string_view::npos
                                                          ? portList.size() - begin
                                                          : end - begin));
        if (!isVerilogIdentifier(port)) {
          ports.clear();
          break;
        }
        ports.emplace_back(port);
        if (end == std::string_view::npos)
          break;
        begin = end + 1;
      }
      if (ports.empty())
        continue;

      std::size_t endModule = moduleLine + 1;
      while (endModule < lines.size() && trim(lines[endModule]) != "endmodule")
        ++endModule;
      if (endModule == lines.size())
        continue;

      std::unordered_map<std::string, std::string> declarations;
      std::unordered_set<std::size_t>              removableLines;
      for (std::size_t line = moduleLine + 1; line < endModule; ++line) {
        if (const auto declaration = parsePortDeclaration(lines[line])) {
          declarations[declaration->name] = declaration->ansi;
          removableLines.insert(line);
        }
      }
      if (declarations.size() != ports.size())
        continue;
      if (std::ranges::any_of(ports, [&declarations](const auto& port) {
            return !declarations.contains(port);
          }))
        continue;

      for (std::size_t line = moduleLine + 1; line < endModule; ++line) {
        if (const auto wire = parseNetDeclarationName(lines[line], "wire");
            wire && declarations.contains(*wire))
          removableLines.insert(line);
        if (const auto reg = parseNetDeclarationName(lines[line], "reg");
            reg && declarations.contains(*reg)
            && declarations.at(*reg).starts_with("output ")) {
          declarations.at(*reg).insert(std::string_view("output ").size(), "reg ");
          removableLines.insert(line);
        }
      }

      std::string ansiHeader =
          lines[moduleLine].substr(0, lines[moduleLine].find('(') + 1);
      for (std::size_t port = 0; port < ports.size(); ++port) {
        if (port != 0)
          ansiHeader += ", ";
        ansiHeader += declarations.at(ports[port]);
      }
      lines[moduleLine] = ansiHeader + ");";
      for (const auto line : removableLines)
        removed[line] = true;
      moduleLine = endModule;
    }

    std::string result;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (removed[line])
        continue;
      result += lines[line];
      result += '\n';
    }
    return result;
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
    const auto status = std::filesystem::status(*options.executable, error);

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
  TemporaryWorkspace workspace;
  const auto         sourcePath = workspace.path() / "source.v";
  const auto         jsonPath   = workspace.path() / "design.json";
  writeFile(sourcePath, source, "Verilog-read input-writing phase");

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

std::string exportVerilog(std::string_view json, const ToolOptions& options)
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
                              "write_verilog -noattr -norename -decimal {}\n",
                              quotePath(library.cells), quotePath(jsonPath),
                              quotePath(library.cells), muxExport,
                              quotePath(verilogPath)),
                   options);
  return useAnsiPortDeclarations(
      cleanProcessVerilog(readFile(verilogPath, "Verilog-export output-reading phase")));
}

#endif

std::string exportVerilog(const Circuit& circuit, const ToolOptions& options)
{
  return exportVerilog(serialize(circuit), options);
}

}  // namespace SILICON::yosys
