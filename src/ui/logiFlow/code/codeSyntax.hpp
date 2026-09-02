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

namespace SILICON::project {

enum class CodeSyntaxStyle {
  Statement,
  Label,
  Conditional,
  Repeat,
  Todo,
  Operator,
  Comment,
  Global,
  Constant,
  Number,
  String,
  Escape,
  Directive,
};

struct CodeSyntaxKeywordGroup {
  CodeSyntaxStyle                   style;
  std::span<const std::string_view> words;
};

struct CodeSyntaxContainedRule {
  CodeSyntaxStyle  style;
  std::string_view pattern;
};

struct CodeSyntaxMatchRule {
  CodeSyntaxStyle                          style;
  std::string_view                         pattern;
  std::span<const CodeSyntaxContainedRule> containedRules;
};

struct CodeSyntaxRegionRule {
  CodeSyntaxStyle                          style;
  std::string_view                         startPattern;
  std::string_view                         endPattern;
  std::string_view                         skipPattern;
  std::span<const CodeSyntaxContainedRule> containedRules;
  bool                                     multiline;
};

struct CodeIndentationContext {
  std::string_view                  currentLine;
  std::span<const std::string_view> previousNonBlankLines;
};

using CodeIndentationFunction = std::string (*)(const CodeIndentationContext&);

struct CodeIndentation {
  CodeIndentationFunction           indentationFor;
  std::span<const std::string_view> triggerPatterns;
};

/** Qt-independent description consumed by code-editor features. */
struct CodeSyntax {
  std::span<const CodeSyntaxKeywordGroup> keywordGroups;
  std::span<const CodeSyntaxMatchRule>    matchRules;
  std::span<const CodeSyntaxRegionRule>   regionRules;
  std::string_view                        extraWordCharacters;
  const CodeIndentation*                  indentation;
};

extern const CodeSyntax VERILOG_CODE_SYNTAX;

}  // namespace SILICON::project
