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

#include <ui/documents/code/codeSyntax.hpp>
#include <ui/documents/code/indentationUtils.hpp>

#include <array>
#include <regex>
#include <string>

namespace SILICON::ui {
namespace {

  // Keep these words aligned with the reserved keywords in SISL's parser.
  constexpr auto DECLARATION_WORDS =
      std::to_array<std::string_view>({"arch", "enum", "instr-format", "instr", "alias"});
  constexpr auto PROPERTY_WORDS =
      std::to_array<std::string_view>({"endianness", "encoding", "assembly", "width"});
  constexpr auto TYPE_WORDS  = std::to_array<std::string_view>({"bits", "uint", "sint"});
  constexpr auto VALUE_WORDS = std::to_array<std::string_view>({"Little", "Big"});

  constexpr std::array KEYWORD_GROUPS{
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Statement, DECLARATION_WORDS},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Label, PROPERTY_WORDS},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Global, TYPE_WORDS},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Constant, VALUE_WORDS},
  };

  constexpr std::array TODO_RULES{CodeSyntaxContainedRule{
      CodeSyntaxStyle::Todo, R"((?<![A-Za-z0-9_-])(?:TODO|FIXME)(?![A-Za-z0-9_-]))"}};
  constexpr std::array ESCAPE_RULES{
      CodeSyntaxContainedRule{CodeSyntaxStyle::Escape, R"(\\(?:["\\nrt]))"}};

  constexpr std::array MATCH_RULES{
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_-])(?:0[bB][01]+|0[oO][0-7]+|0[xX][0-9a-fA-F]+|[0-9]+)(?![A-Za-z0-9_-]))",
          {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Operator, R"([=;:{}\[\]<>(),])", {}},
  };

  constexpr std::array REGION_RULES{
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(/\*)", R"(\*/)", {}, TODO_RULES, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(//)", R"($)", {}, TODO_RULES, false},
      CodeSyntaxRegionRule{CodeSyntaxStyle::String, R"(")", R"(")", R"(\\.)", ESCAPE_RULES,
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

  constexpr auto INDENTATION_TRIGGER_PATTERNS =
      std::to_array<std::string_view>({R"(^\s*})", R"(^\s*\])", R"(^\s*\))"});

  constexpr CodeIndentation SISL_INDENTATION{
      .indentationFor       = sislIndentationFor,
      .triggerPatterns      = INDENTATION_TRIGGER_PATTERNS,
      .indentWidth          = 2,
      .expandPairsOnNewline = true,
  };

}  // namespace

const CodeSyntax SISL_CODE_SYNTAX{
    .keywordGroups       = KEYWORD_GROUPS,
    .matchRules          = MATCH_RULES,
    .regionRules         = REGION_RULES,
    .extraWordCharacters = "-",
    .indentation         = &SISL_INDENTATION,
};

}  // namespace SILICON::ui
