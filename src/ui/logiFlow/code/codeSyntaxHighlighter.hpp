/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <array>
#include <vector>

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <ui/logiFlow/code/codeSyntax.hpp>

class QPalette;
class QTextDocument;

namespace SILICON::ui {

/** Applies a project CodeSyntax description to a QTextDocument. */
class CodeSyntaxHighlighter final : public QSyntaxHighlighter {
public:
  explicit CodeSyntaxHighlighter(QTextDocument* document);

  void setSyntax(const SILICON::project::CodeSyntax* syntax);
  void setPalette(const QPalette& palette);

protected:
  void highlightBlock(const QString& text) override;

private:
  struct CompiledKeywordGroup {
    SILICON::project::CodeSyntaxStyle style;
    QRegularExpression                expression;
  };

  struct CompiledContainedRule {
    SILICON::project::CodeSyntaxStyle style;
    QRegularExpression                expression;
  };

  struct CompiledMatchRule {
    SILICON::project::CodeSyntaxStyle  style;
    QRegularExpression                 expression;
    std::vector<CompiledContainedRule> containedRules;
  };

  struct CompiledRegionRule {
    SILICON::project::CodeSyntaxStyle  style;
    QRegularExpression                 startExpression;
    QRegularExpression                 endExpression;
    QRegularExpression                 skipExpression;
    std::vector<CompiledContainedRule> containedRules;
    bool                               multiline;
  };

  struct RegionEnd {
    qsizetype start;
    qsizetype end;
  };

  static constexpr std::size_t StyleCount = 13;

  [[nodiscard]] const QTextCharFormat&
                                 format(SILICON::project::CodeSyntaxStyle style) const;
  [[nodiscard]] static RegionEnd findRegionEnd(const QString& text, qsizetype from,
                                               const CompiledRegionRule& region);
  void applyContainedRules(const QString& text, qsizetype start, qsizetype end,
                           const std::vector<CompiledContainedRule>& rules);

  const SILICON::project::CodeSyntax*     syntax = nullptr;
  std::vector<CompiledKeywordGroup>       keywordGroups;
  std::vector<CompiledMatchRule>          matchRules;
  std::vector<CompiledRegionRule>         regionRules;
  std::array<QTextCharFormat, StyleCount> formats;
};

}  // namespace SILICON::ui
