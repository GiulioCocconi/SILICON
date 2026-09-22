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

namespace SILICON::ui {
namespace {

  // Keep these words aligned with the reserved keywords in SISL's parser.
  constexpr auto DeclarationWords =
      std::to_array<std::string_view>({"arch", "enum", "instr-format", "instr", "alias"});
  constexpr auto PropertyWords =
      std::to_array<std::string_view>({"endianness", "encoding", "assembly", "width"});
  constexpr auto TypeWords  = std::to_array<std::string_view>({"bits", "uint", "sint"});
  constexpr auto ValueWords = std::to_array<std::string_view>({"Little", "Big"});

  constexpr std::array KeywordGroups{
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Statement, DeclarationWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Label, PropertyWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Global, TypeWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Constant, ValueWords},
  };

  constexpr std::array TodoRules{CodeSyntaxContainedRule{
      CodeSyntaxStyle::Todo, R"((?<![A-Za-z0-9_-])(?:TODO|FIXME)(?![A-Za-z0-9_-]))"}};
  constexpr std::array EscapeRules{
      CodeSyntaxContainedRule{CodeSyntaxStyle::Escape, R"(\\(?:["\\nrt]))"}};

  constexpr std::array MatchRules{
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_-])(?:0[bB][01]+|0[oO][0-7]+|0[xX][0-9a-fA-F]+|[0-9]+)(?![A-Za-z0-9_-]))",
          {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Operator, R"([=;:{}\[\]<>(),])", {}},
  };

  constexpr std::array RegionRules{
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(/\*)", R"(\*/)", {}, TodoRules, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(//)", R"($)", {}, TodoRules, false},
      CodeSyntaxRegionRule{CodeSyntaxStyle::String, R"(")", R"(")", R"(\\.)", EscapeRules,
                           false},
  };

  [[nodiscard]] std::string sislIndentationFor(const CodeIndentationContext& context)
  {
    if (context.previousNonBlankLines.empty())
      return {};

    static const std::regex strippedPortions(
        R"(//.*|/\*.*?(?:\*/|$)|"(?:\\.|[^"\\])*(?:"|$))", std::regex::optimize);
    const auto previousLine = context.previousNonBlankLines.front();
    const auto previousCode = indentation::codePortion(previousLine, strippedPortions);
    const auto currentCode =
        indentation::codePortion(context.currentLine, strippedPortions);
    auto result = indentation::leadingWhitespace(previousLine);

    const auto previous = indentation::trimRight(previousCode);
    if (!previous.empty() && std::string_view("{([").contains(previous.back()))
      indentation::addIndentLevel(result, context.indentWidth);

    const auto current = indentation::trimLeft(currentCode);
    if (!current.empty() && std::string_view("})]").contains(current.front()))
      indentation::removeIndentLevel(result, context.indentWidth);

    return result;
  }

  constexpr auto IndentationTriggerPatterns =
      std::to_array<std::string_view>({R"(^\s*})", R"(^\s*\])", R"(^\s*\))"});

  constexpr CodeIndentation SislIndentation{
      .indentationFor       = sislIndentationFor,
      .triggerPatterns      = IndentationTriggerPatterns,
      .indentWidth          = 2,
      .expandPairsOnNewline = true,
  };

}  // namespace

const CodeSyntax SISL_CODE_SYNTAX{
    .keywordGroups       = KeywordGroups,
    .matchRules          = MatchRules,
    .regionRules         = RegionRules,
    .extraWordCharacters = "-",
    .indentation         = &SislIndentation,
};

}  // namespace SILICON::ui
