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
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <core/circuit.hpp>

#ifdef __EMSCRIPTEN__

namespace silicon::yosys {

ScriptResult runScript(const std::string_view script, const ToolOptions& options)
{
  (void)script;
  (void)options;
  throw std::runtime_error(
      "Yosys platform-availability phase failed: external execution is unavailable "
      "when compiled for Emscripten");
}

Circuit importVerilog(const std::string_view source, const std::string_view topModule,
                      const ToolOptions& options)
{
  (void)source;
  (void)topModule;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

std::string exportVerilog(const Circuit& circuit, const ToolOptions& options)
{
  (void)circuit;
  (void)runScript({}, options);
  throw std::logic_error("Unreachable after unavailable Yosys execution");
}

}  // namespace silicon::yosys

#else

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

namespace silicon::yosys {
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
    std::filesystem::path blackBoxes;
    std::filesystem::path simulationModels;
    std::filesystem::path technologyMap;
  };

  [[nodiscard]] std::optional<TechnologyLibrary>
  libraryAt(const std::filesystem::path& directory)
  {
    TechnologyLibrary result{directory / "silicon_cells_bb.v",
                             directory / "silicon_cells_sim.v",
                             directory / "silicon_techmap.v"};
    std::error_code   error;
    for (const auto* path :
         {&result.blackBoxes, &result.simulationModels, &result.technologyMap}) {
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
          "directory '{}' does not contain silicon_cells_bb.v, silicon_cells_sim.v, "
          "and silicon_techmap.v",
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

  [[nodiscard]] std::string capturedDiagnostics(const ScriptResult& result)
  {
    return std::format("\nstdout:\n{}\nstderr:\n{}",
                       result.standardOutput.empty() ? "<empty>" : result.standardOutput,
                       result.standardError.empty() ? "<empty>" : result.standardError);
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
          "missing or invalid",
          options.executable->string()));
    }
    executable = *options.executable;
  } else {
    executable = process::search_path("yosys").string();
    if (executable.empty())
      throw std::runtime_error(
          "Yosys executable-discovery phase failed: 'yosys' was not found on PATH");
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
    throw std::runtime_error(
        std::format("Yosys process-creation phase failed for '{}': {}\nstdout:\n<empty>\n"
                    "stderr:\n<empty>",
                    executable.string(), error.what()));
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
    throw std::runtime_error(std::format(
        "Yosys output-capture phase failed: {}\nstdout:\n<unavailable>\nstderr:\n"
        "<unavailable>",
        error.what()));
  }

  ScriptResult result;
  try {
    result = {standardOutput.get(), standardError.get()};
  } catch (const std::exception& error) {
    throw std::runtime_error(std::format(
        "Yosys output-capture phase failed after exit status {}: {}\nstdout:\n"
        "<unavailable>\nstderr:\n<unavailable>",
        child->exit_code(), error.what()));
  }

  const int exitStatus = child->exit_code();
  if (exitStatus != 0) {
    throw std::runtime_error(
        std::format("Yosys script-execution phase failed with exit status {}{}",
                    exitStatus, capturedDiagnostics(result)));
  }
  return result;
}

Circuit importVerilog(const std::string_view source, const std::string_view topModule,
                      const ToolOptions& options)
{
  if (!isVerilogIdentifier(topModule)) {
    throw std::invalid_argument(std::format(
        "Invalid Verilog top-module identifier '{}': expected a simple Verilog-2005 "
        "identifier",
        topModule));
  }

  TemporaryWorkspace workspace;
  const auto         library    = technologyLibrary(options);
  const auto         sourcePath = workspace.path() / "source.v";
  const auto         jsonPath   = workspace.path() / "design.json";
  writeFile(sourcePath, source, "Verilog-import input-writing phase");

  (void)runScript(std::format("read_verilog -lib {}\n"
                              "read_verilog {}\n"
                              "hierarchy -check -top {}\n"
                              "proc\n"
                              "flatten\n"
                              "delete t:$scopeinfo\n"
                              "opt\n"
                              "simplemap t:$and t:$or t:$xor t:$not\n"
                              "extract_fa\n"
                              "techmap -map {}\n"
                              "opt_clean\n"
                              "write_json {}\n",
                              quotePath(library.blackBoxes), quotePath(sourcePath),
                              topModule, quotePath(library.technologyMap),
                              quotePath(jsonPath)),
                  options);
  return deserialize(readFile(jsonPath, "Verilog-import output-reading phase"),
                     topModule);
}

std::string exportVerilog(const Circuit& circuit, const ToolOptions& options)
{
  TemporaryWorkspace workspace;
  const auto         library     = technologyLibrary(options);
  const auto         jsonPath    = workspace.path() / "design.json";
  const auto         verilogPath = workspace.path() / "design.v";
  writeFile(jsonPath, serialize(circuit), "Verilog-export input-writing phase");

  (void)runScript(std::format("read_verilog -lib {}\n"
                              "read_json {}\n"
                              "hierarchy -check -auto-top\n"
                              "write_verilog -noattr {}\n",
                              quotePath(library.blackBoxes), quotePath(jsonPath),
                              quotePath(verilogPath)),
                  options);
  return readFile(verilogPath, "Verilog-export output-reading phase");
}

}  // namespace silicon::yosys

#endif
