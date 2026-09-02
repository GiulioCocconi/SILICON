/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <ui/logiFlow/code/codeSyntax.hpp>
#include <ui/logiFlow/code/indentationUtils.hpp>

#include <array>
#include <regex>
#include <string>

namespace SILICON::project {
namespace {

  using indentation::addIndentLevel;
  using indentation::codePortion;
  using indentation::endsWithWord;
  using indentation::leadingWhitespace;
  using indentation::regexSearch;
  using indentation::removeIndentLevel;
  using indentation::trimLeft;
  using indentation::trimRight;

  // Adapted from Vim's runtime/syntax/verilog.vim.
  constexpr auto StatementWords = std::to_array<std::string_view>({
      std::string_view{"always"},
      "and",
      "assign",
      "automatic",
      "buf",
      "bufif0",
      "bufif1",
      "cell",
      "cmos",
      "config",
      "deassign",
      "defparam",
      "design",
      "disable",
      "edge",
      "endconfig",
      "endfunction",
      "endgenerate",
      "endmodule",
      "endprimitive",
      "endspecify",
      "endtable",
      "endtask",
      "event",
      "force",
      "function",
      "generate",
      "genvar",
      "highz0",
      "highz1",
      "ifnone",
      "incdir",
      "include",
      "initial",
      "inout",
      "input",
      "instance",
      "integer",
      "large",
      "liblist",
      "library",
      "localparam",
      "macromodule",
      "medium",
      "module",
      "nand",
      "negedge",
      "nmos",
      "nor",
      "noshowcancelled",
      "not",
      "notif0",
      "notif1",
      "or",
      "output",
      "parameter",
      "pmos",
      "posedge",
      "primitive",
      "pull0",
      "pull1",
      "pulldown",
      "pullup",
      "pulsestyle_onevent",
      "pulsestyle_ondetect",
      "rcmos",
      "real",
      "realtime",
      "reg",
      "release",
      "rnmos",
      "rpmos",
      "rtran",
      "rtranif0",
      "rtranif1",
      "scalared",
      "showcancelled",
      "signed",
      "small",
      "specify",
      "specparam",
      "strong0",
      "strong1",
      "supply0",
      "supply1",
      "table",
      "task",
      "time",
      "tran",
      "tranif0",
      "tranif1",
      "tri",
      "tri0",
      "tri1",
      "triand",
      "trior",
      "trireg",
      "unsigned",
      "use",
      "vectored",
      "wait",
      "wand",
      "weak0",
      "weak1",
      "wire",
      "wor",
      "xnor",
      "xor",
  });

  constexpr auto LabelWords =
      std::to_array<std::string_view>({"begin", "end", "fork", "join"});
  constexpr auto ConditionalWords = std::to_array<std::string_view>(
      {"if", "else", "case", "casex", "casez", "default", "endcase"});
  constexpr auto RepeatWords =
      std::to_array<std::string_view>({"forever", "repeat", "while", "for"});

  constexpr std::array KeywordGroups{
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Statement, StatementWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Label, LabelWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Conditional, ConditionalWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Repeat, RepeatWords},
  };

  constexpr std::array TodoRules{CodeSyntaxContainedRule{
      CodeSyntaxStyle::Todo, R"((?<![A-Za-z0-9_?])(?:TODO|FIXME)(?![A-Za-z0-9_?]))"}};
  constexpr std::array EscapeRules{
      CodeSyntaxContainedRule{CodeSyntaxStyle::Escape, R"(\\(?:[nt"\\]|[0-7]{1,3}))"}};

  constexpr std::array MatchRules{
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Operator, R"([&|~><!)(*#%@+/=?:;}{,.^\-\[\]])", {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Constant,
                          R"((?<![A-Za-z0-9_?])[A-Z][A-Z0-9_]+(?![A-Za-z0-9_?]))",
                          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[bB]\s*[0-1_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[oO]\s*[0-7_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[dD]\s*[0-9_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[hH]\s*[0-9a-fA-F_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])[+-]?[0-9_]+(?:\.[0-9_]*)?(?:e[0-9_]*)?(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Global,
          R"(`(?:celldefine|default_nettype|define|else|elsif|endcelldefine|endif|ifdef|ifndef|include|line|nounconnected_drive|resetall|timescale|unconnected_drive|undef)(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Global, R"(\$[A-Za-z0-9_]+\b)", {}},
  };

  // More specific regions precede their generic comment counterparts so ties at
  // the same start position resolve like Vim's contained syntax groups.
  constexpr std::array RegionRules{
      CodeSyntaxRegionRule{CodeSyntaxStyle::Directive,
                           R"(//\s*synopsys dc_script_begin\b)",
                           R"(//\s*synopsys dc_script_end\b)",
                           {},
                           {},
                           true},
      CodeSyntaxRegionRule{CodeSyntaxStyle::Directive,
                           R"(//\s*\$s dc_script_begin\b)",
                           R"(//\s*\$s dc_script_end\b)",
                           {},
                           {},
                           true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(/\*\s*synopsys\b)", R"(\*/)", {}, {}, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(/\*\s*\$s\b)", R"(\*/)", {}, {}, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(//\s*synopsys\b)", R"($)", {}, {}, false},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(//\s*\$s\b)", R"($)", {}, {}, false},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(/\*)", R"(\*/)", {}, TodoRules, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(//)", R"($)", {}, TodoRules, false},
      CodeSyntaxRegionRule{CodeSyntaxStyle::String, R"(")", R"(")", R"(\\.)", EscapeRules,
                           false},
  };

  [[nodiscard]] bool startsWithBlockStatement(const std::string_view code)
  {
    static const std::regex expression(
        R"(^\s*(?:(?:end)\s+)?(?:if|else)\b|^\s*(?:for|case|casex|casez|always|initial|specify|fork)\b)",
        std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithDeclarationBlock(const std::string_view code)
  {
    static const std::regex expression(
        R"(^\s*(?:function|task|config|generate|primitive|table)\b)",
        std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithModule(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*module\b)", std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithEndModule(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*endmodule\b)", std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithPreprocessorBranch(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*`(?:ifdef|ifndef|elsif|else)\b)",
                                       std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithPreprocessorCloser(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*`(?:elsif|else|endif)\b)",
                                       std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithBlockCloser(const std::string_view code)
  {
    static const std::regex expression(
        R"(^\s*(?:join|end|endcase|endfunction|endtask|endspecify|endconfig|endgenerate|endprimitive|endtable)\b)",
        std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool startsWithStandaloneBegin(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*begin\b)", std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool containsBegin(const std::string_view code)
  {
    static const std::regex expression(R"(\bbegin\b)", std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool endsWithBegin(const std::string_view code)
  {
    static const std::regex expression(R"(\bbegin\b(?:\s*:\s*\w+)?\s*$)",
                                       std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool isTerminatedBlockStatement(const std::string_view code)
  {
    const auto trimmed = trimRight(code);
    return trimmed.ends_with(';') || endsWithWord(trimmed, "end");
  }

  [[nodiscard]] bool isOpenStatement(const std::string_view code)
  {
    const auto trimmed = trimRight(code);
    if (trimmed.empty())
      return false;
    if (endsWithWord(trimmed, "or"))
      return true;
    constexpr std::string_view OpenCharacters = "*(,{><+-/%^&|!=?:";
    return OpenCharacters.contains(trimmed.back());
  }

  [[nodiscard]] bool isCaseLabel(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*[^=!]+\s*:\s*$)", std::regex::optimize);
    return regexSearch(code, expression);
  }

  [[nodiscard]] bool closesMultilineStatement(const std::string_view code)
  {
    const auto trimmed = trimRight(code);
    if (!trimmed.ends_with(';'))
      return false;
    const auto beforeSemicolon = trimRight(trimmed.substr(0, trimmed.size() - 1));
    return !beforeSemicolon.empty();
  }

  [[nodiscard]] bool isOnlyClosingStatement(const std::string_view code)
  {
    static const std::regex expression(R"(^\s*\)*\s*;?\s*$)", std::regex::optimize);
    return std::regex_match(code.begin(), code.end(), expression);
  }

  [[nodiscard]] bool closesMultilineComment(const std::string_view line)
  {
    const auto trimmed = trimRight(line);
    if (!trimmed.ends_with("*/"))
      return false;
    const auto closing = trimmed.rfind("*/");
    const auto opening = trimmed.rfind("/*", closing);
    return opening == std::string_view::npos;
  }

  [[nodiscard]] std::string verilogIndentationFor(const CodeIndentationContext& context)
  {
    if (context.previousNonBlankLines.empty())
      return {};

    const auto              previousLine = context.previousNonBlankLines[0];
    const auto              olderLine    = context.previousNonBlankLines.size() > 1
                                               ? context.previousNonBlankLines[1]
                                               : std::string_view{};
    static const std::regex strippedPortions(
        R"(//.*|/\*.*?(?:\*/|$)|"(?:\\.|[^"\\])*(?:"|$))", std::regex::optimize);
    const auto previousCode = codePortion(previousLine, strippedPortions);
    const auto olderCode    = codePortion(olderLine, strippedPortions);
    const auto currentCode  = codePortion(context.currentLine, strippedPortions);
    auto       indentation  = leadingWhitespace(previousLine);

    // Keep this branch order aligned with Vim's GetVerilogIndent(): the first
    // matching previous-line rule wins.
    if (closesMultilineComment(previousLine)) {
      if (!indentation.empty() && indentation.back() == ' ')
        indentation.pop_back();
    } else if (startsWithBlockStatement(previousCode)) {
      if (!isTerminatedBlockStatement(previousCode))
        addIndentLevel(indentation);
    } else if (startsWithDeclarationBlock(previousCode)) {
      if (!endsWithWord(previousCode, "end"))
        addIndentLevel(indentation);
    } else if (startsWithModule(previousCode)) {
      const auto trimmed = trimRight(previousCode);
      if (!trimmed.empty() && (trimmed.back() == '(' || trimmed.back() == ','))
        addIndentLevel(indentation);
    } else if (endsWithBegin(previousCode)
               && (!isOpenStatement(olderCode) || isCaseLabel(olderCode))) {
      addIndentLevel(indentation);
    } else if (!containsBegin(previousCode) && startsWithBlockStatement(olderCode)
               && !isOpenStatement(olderCode) && !containsBegin(olderCode)) {
      removeIndentLevel(indentation);
    } else if (isOpenStatement(previousCode) && !isOpenStatement(olderCode)) {
      addIndentLevel(indentation);
    } else if (closesMultilineStatement(previousCode)
               && !isOnlyClosingStatement(previousCode) && isOpenStatement(olderCode)
               && !trimLeft(olderCode).empty()) {
      removeIndentLevel(indentation);
    } else if (startsWithPreprocessorBranch(previousCode)) {
      addIndentLevel(indentation);
    }

    // Current-line corrections mirror Vim's indentkeys-triggered reindent pass.
    if (startsWithBlockCloser(currentCode)) {
      removeIndentLevel(indentation);
    } else if (startsWithEndModule(currentCode)) {
      // Module indentation is deliberately disabled, matching Vim's default.
    } else if (startsWithStandaloneBegin(currentCode)) {
      const bool previousStartsDeclaration =
          startsWithDeclarationBlock(previousCode) || startsWithModule(previousCode)
          || regexSearch(previousCode,
                         std::regex(R"(^\s*specify\b)", std::regex::optimize));
      if (!previousStartsDeclaration && !isOnlyClosingStatement(previousCode)
          && (startsWithBlockStatement(previousCode)
              || trimRight(previousCode).ends_with(')')
              || isOpenStatement(previousCode))) {
        removeIndentLevel(indentation);
      }
    } else if (trimLeft(currentCode).starts_with(')')
               && (isOpenStatement(previousCode) || isOpenStatement(olderCode))) {
      removeIndentLevel(indentation);
    } else if (startsWithPreprocessorCloser(currentCode)) {
      removeIndentLevel(indentation);
    }

    return indentation;
  }

  constexpr auto IndentationTriggerPatterns = std::to_array<std::string_view>({
      std::string_view{
          R"((?<![A-Za-z0-9_?])(?:begin|end|join|endcase|endmodule|endfunction|endtask|endspecify|endconfig|endgenerate|endprimitive|endtable)\s*$)"},
      R"(^\s*\))",
      R"(^\s*`(?:else|elsif|endif)\b)",
  });

  constexpr CodeIndentation VerilogIndentation{
      .indentationFor  = verilogIndentationFor,
      .triggerPatterns = IndentationTriggerPatterns,
  };

}  // namespace

const CodeSyntax VERILOG_CODE_SYNTAX{
    .keywordGroups       = KeywordGroups,
    .matchRules          = MatchRules,
    .regionRules         = RegionRules,
    .extraWordCharacters = "?",
    .indentation         = &VerilogIndentation,
};

}  // namespace SILICON::project
