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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
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
  const std::array sources{VerilogSourceFile{.path = "source.v", .contents = source}};
  return readVerilog(sources, "source.v", options);
}

std::string readVerilog(const std::span<const VerilogSourceFile> sources,
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

  [[nodiscard]] bool isSafeRelativeSourcePath(const std::string_view path)
  {
    if (path.empty() || path.front() == '/' || path.back() == '/'
        || path.contains('\\') || path.contains("//"))
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

  struct ContinuousAssignment {
    std::string lhs;
    std::string rhs;
  };

  [[nodiscard]] std::optional<ContinuousAssignment>
  parseContinuousAssignment(const std::string_view line)
  {
    const auto value = trim(line);
    if (!value.starts_with("assign ") || !value.ends_with(';'))
      return std::nullopt;
    const auto equal = value.find(" = ", std::string_view("assign ").size());
    if (equal == std::string_view::npos)
      return std::nullopt;
    const auto lhs = trim(value.substr(std::string_view("assign ").size(),
                                      equal - std::string_view("assign ").size()));
    if (!isVerilogIdentifier(lhs))
      return std::nullopt;
    return ContinuousAssignment{
        std::string(lhs),
        std::string(trim(value.substr(equal + 3, value.size() - equal - 4)))};
  }

  [[nodiscard]] bool isGeneratedIdentifier(const std::string_view identifier)
  {
    return identifier.size() > 2 && identifier.front() == '_'
           && identifier.back() == '_'
           && std::ranges::all_of(identifier.substr(1, identifier.size() - 2),
                                  [](const char value) {
                                    return std::isdigit(
                                               static_cast<unsigned char>(value))
                                           != 0;
                                  });
  }

  template<typename Callback>
  void forEachIdentifier(const std::string_view value, Callback&& callback)
  {
    const auto isIdentifierCharacter = [](const char character) {
      const auto value = static_cast<unsigned char>(character);
      return std::isalnum(value) != 0 || character == '_' || character == '$';
    };
    for (std::size_t begin = 0; begin < value.size();) {
      if (!std::isalpha(static_cast<unsigned char>(value[begin]))
          && value[begin] != '_') {
        ++begin;
        continue;
      }
      auto end = begin + 1;
      while (end < value.size() && isIdentifierCharacter(value[end]))
        ++end;
      callback(value.substr(begin, end - begin));
      begin = end;
    }
  }

  [[nodiscard]] std::optional<char>
  associativeOperator(const std::string_view expression)
  {
    unsigned            nesting = 0;
    std::optional<char> result;
    for (std::size_t index = 0; index < expression.size(); ++index) {
      const char character = expression[index];
      if (character == '(' || character == '[' || character == '{') {
        ++nesting;
        continue;
      }
      if (character == ')' || character == ']' || character == '}') {
        if (nesting != 0)
          --nesting;
        continue;
      }
      if (nesting != 0 || (character != '&' && character != '|' && character != '^'))
        continue;
      if (index == 0)
        return std::nullopt;
      const auto previous = expression.find_last_not_of(" \t", index - 1);
      const auto next     = expression.find_first_not_of(" \t", index + 1);
      if (previous == std::string_view::npos || next == std::string_view::npos
          || expression[previous] == character || expression[next] == character)
        return std::nullopt;
      if (result && *result != character)
        return std::nullopt;
      result = character;
    }
    return result;
  }

  /**
   * Fold single-use scalar temporaries introduced by the Verilog backend back into
   * their consuming continuous assignment. Restricting this to `_NN_` wires with one
   * use avoids duplicating logic or substituting expressions into procedural code.
   */
  [[nodiscard]] std::string
  inlineGeneratedCombinationalNets(const std::string_view source)
  {
    auto lines = splitLines(source);

    std::unordered_map<std::string, std::size_t> declarations;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      const auto name = parseNetDeclarationName(lines[line], "wire");
      if (name && isGeneratedIdentifier(*name)
          && trim(lines[line]) == std::format("wire {};", *name))
        declarations.emplace(*name, line);
    }

    struct Driver {
      std::size_t line;
      std::string expression;
    };
    std::unordered_map<std::string, Driver> drivers;
    std::unordered_map<std::string, std::size_t> uses;
    std::unordered_set<std::string> unsafe;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (const auto assignment = parseContinuousAssignment(lines[line])) {
        if (declarations.contains(assignment->lhs)) {
          if (!drivers
                   .emplace(assignment->lhs,
                            Driver{line, std::move(assignment->rhs)})
                   .second)
            unsafe.insert(assignment->lhs);
        }
        forEachIdentifier(assignment->rhs, [&](const std::string_view identifier) {
          if (declarations.contains(std::string(identifier)))
            ++uses[std::string(identifier)];
        });
        continue;
      }

      bool declarationLine = false;
      for (const auto& [name, declaration] : declarations) {
        if (declaration == line) {
          declarationLine = true;
          break;
        }
      }
      if (declarationLine)
        continue;
      forEachIdentifier(lines[line], [&](const std::string_view identifier) {
        if (declarations.contains(std::string(identifier)))
          unsafe.insert(std::string(identifier));
      });
    }

    std::unordered_set<std::string> inlineable;
    for (const auto& [name, declaration] : declarations) {
      (void)declaration;
      if (drivers.contains(name) && uses[name] == 1 && !unsafe.contains(name))
        inlineable.insert(name);
    }

    const auto expand = [&](this const auto& self, const std::string_view expression,
                            std::unordered_set<std::string>& active) -> std::string {
      std::string result;
      std::size_t copied = 0;
      const auto  parentOperator = associativeOperator(expression);
      forEachIdentifier(expression, [&](const std::string_view identifier) {
        const auto begin = expression.find(identifier, copied);
        result.append(expression.substr(copied, begin - copied));
        const std::string name(identifier);
        if (inlineable.contains(name) && !active.contains(name)) {
          active.insert(name);
          const auto& child = drivers.at(name).expression;
          const auto  expandedChild = self(child, active);
          if (parentOperator && associativeOperator(child) == parentOperator)
            result += expandedChild;
          else
            result += '(' + expandedChild + ')';
          active.erase(name);
        } else {
          result += identifier;
        }
        copied = begin + identifier.size();
      });
      result.append(expression.substr(copied));
      return result;
    };

    std::vector<bool> removed(lines.size(), false);
    for (const auto& name : inlineable) {
      removed[declarations.at(name)] = true;
      removed[drivers.at(name).line]  = true;
    }
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (removed[line])
        continue;
      const auto assignment = parseContinuousAssignment(lines[line]);
      if (!assignment)
        continue;
      std::unordered_set<std::string> active;
      const auto indent = lines[line].substr(0, lines[line].find_first_not_of(" \t"));
      lines[line] = std::format("{}assign {} = {};", indent, assignment->lhs,
                                expand(assignment->rhs, active));
    }

    std::string result;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (!removed[line])
        result += lines[line] + '\n';
    }
    return result;
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

      const auto directionRank = [&declarations](const std::string& port) {
        const auto& declaration = declarations.at(port);
        if (declaration.starts_with("input "))
          return 0;
        if (declaration.starts_with("inout "))
          return 1;
        return 2;
      };
      std::ranges::sort(ports, [&](const auto& lhs, const auto& rhs) {
        const auto lhsRank = directionRank(lhs);
        const auto rhsRank = directionRank(rhs);
        return lhsRank != rhsRank ? lhsRank < rhsRank : lhs < rhs;
      });

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
  const std::array sources{VerilogSourceFile{.path = "source.v", .contents = source}};
  return readVerilog(sources, "source.v", options);
}

std::string readVerilog(const std::span<const VerilogSourceFile> sources,
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
    const auto sourcePath = workspace.path() / std::filesystem::path(source.path);
    std::error_code error;
    std::filesystem::create_directories(sourcePath.parent_path(), error);
    if (error)
      throw std::runtime_error(std::format(
          "Yosys Verilog-read input-writing phase failed: could not create '{}': {}",
          sourcePath.parent_path().string(), error.message()));
    writeFile(sourcePath, source.contents, "Verilog-read input-writing phase");
  }

  const auto sourcePath = workspace.path() / std::filesystem::path(entryPath);
  const auto         jsonPath   = workspace.path() / "design.json";

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
                              "rename -enumerate\n"
                              "write_verilog -noattr -norename -decimal {}\n",
                              quotePath(library.cells), quotePath(jsonPath),
                              quotePath(library.cells), muxExport,
                              quotePath(verilogPath)),
                   options);
  return useAnsiPortDeclarations(inlineGeneratedCombinationalNets(cleanProcessVerilog(
      readFile(verilogPath, "Verilog-export output-reading phase"))));
}

#endif

std::string exportVerilog(const Circuit& circuit, const ToolOptions& options)
{
  return exportVerilog(serialize(circuit), options);
}

}  // namespace SILICON::yosys
