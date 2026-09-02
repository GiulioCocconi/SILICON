/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "verilog.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/circuit.hpp>
#include <core/serialization/yosys/netlist.hpp>

namespace SILICON::verilog {
namespace {
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
    return identifier.size() > 2 && identifier.front() == '_' && identifier.back() == '_'
           && std::ranges::all_of(
               identifier.substr(1, identifier.size() - 2), [](const char value) {
                 return std::isdigit(static_cast<unsigned char>(value)) != 0;
               });
  }

  template <typename Callback>
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

  [[nodiscard]] std::optional<char> associativeOperator(const std::string_view expression)
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
    std::unordered_map<std::string, Driver>      drivers;
    std::unordered_map<std::string, std::size_t> uses;
    std::unordered_set<std::string>              unsafe;
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (const auto assignment = parseContinuousAssignment(lines[line])) {
        if (declarations.contains(assignment->lhs)) {
          if (!drivers.emplace(assignment->lhs, Driver{line, std::move(assignment->rhs)})
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
      std::size_t copied         = 0;
      const auto  parentOperator = associativeOperator(expression);
      forEachIdentifier(expression, [&](const std::string_view identifier) {
        const auto begin = expression.find(identifier, copied);
        result.append(expression.substr(copied, begin - copied));
        const std::string name(identifier);
        if (inlineable.contains(name) && !active.contains(name)) {
          active.insert(name);
          const auto& child         = drivers.at(name).expression;
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
      removed[drivers.at(name).line] = true;
    }
    for (std::size_t line = 0; line < lines.size(); ++line) {
      if (removed[line])
        continue;
      const auto assignment = parseContinuousAssignment(lines[line]);
      if (!assignment)
        continue;
      std::unordered_set<std::string> active;
      const auto indent = lines[line].substr(0, lines[line].find_first_not_of(" \t"));
      lines[line]       = std::format("{}assign {} = {};", indent, assignment->lhs,
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

}  // namespace

std::string read(const std::string_view source, const yosys::ToolOptions& options)
{
  return yosys::readVerilog(source, options);
}

std::string read(const std::span<const SourceFile> sources,
                 const std::string_view entryPath, const yosys::ToolOptions& options)
{
  return yosys::readVerilog(sources, entryPath, options);
}

std::string postprocess(const std::string_view source)
{
  return useAnsiPortDeclarations(
      inlineGeneratedCombinationalNets(cleanProcessVerilog(source)));
}

std::string write(const std::string_view yosysJson, const yosys::ToolOptions& options)
{
  return postprocess(yosys::writeVerilog(yosysJson, options));
}

std::string write(const core::Circuit& circuit, const yosys::ToolOptions& options)
{
  return write(yosys::serialize(circuit), options);
}

}  // namespace SILICON::verilog
