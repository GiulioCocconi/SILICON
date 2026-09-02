/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <ui/logiFlow/code/codeSyntaxHighlighter.hpp>

#include <algorithm>
#include <stdexcept>

#include <QPalette>
#include <QStringList>
#include <QTextDocument>

#include <ui/common/theme.hpp>

namespace SILICON::ui {
namespace {

  using SILICON::project::CodeSyntaxStyle;

  [[nodiscard]] QRegularExpression compileExpression(const std::string_view pattern)
  {
    QRegularExpression expression(
        QString::fromUtf8(pattern.data(), static_cast<qsizetype>(pattern.size())));
    if (!expression.isValid())
      throw std::invalid_argument(QString("Invalid code-syntax expression '%1': %2")
                                      .arg(expression.pattern(), expression.errorString())
                                      .toStdString());
    return expression;
  }

  [[nodiscard]] QTextCharFormat colored(const QColor& color)
  {
    QTextCharFormat format;
    format.setForeground(color);
    return format;
  }

}  // namespace

CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* document)
  : QSyntaxHighlighter(document)
{
}

void CodeSyntaxHighlighter::setSyntax(const SILICON::project::CodeSyntax* newSyntax)
{
  syntax = newSyntax;
  keywordGroups.clear();
  matchRules.clear();
  regionRules.clear();

  if (syntax) {
    for (const auto& group : syntax->keywordGroups) {
      QStringList escapedWords;
      escapedWords.reserve(static_cast<qsizetype>(group.words.size()));
      for (const auto word : group.words) {
        escapedWords.append(QRegularExpression::escape(
            QString::fromUtf8(word.data(), static_cast<qsizetype>(word.size()))));
      }
      const QString extra = QRegularExpression::escape(
          QString::fromUtf8(syntax->extraWordCharacters.data(),
                            static_cast<qsizetype>(syntax->extraWordCharacters.size())));
      keywordGroups.push_back({.style      = group.style,
                               .expression = compileExpression(
                                   QString("(?<![A-Za-z0-9_%1])(?:%2)(?![A-Za-z0-9_%1])")
                                       .arg(extra, escapedWords.join('|'))
                                       .toStdString())});
    }

    for (const auto& rule : syntax->matchRules) {
      CompiledMatchRule compiled{.style          = rule.style,
                                 .expression     = compileExpression(rule.pattern),
                                 .containedRules = {}};
      for (const auto& contained : rule.containedRules) {
        compiled.containedRules.push_back(
            {.style      = contained.style,
             .expression = compileExpression(contained.pattern)});
      }
      matchRules.push_back(std::move(compiled));
    }

    for (const auto& region : syntax->regionRules) {
      CompiledRegionRule compiled{
          .style           = region.style,
          .startExpression = compileExpression(region.startPattern),
          .endExpression   = compileExpression(region.endPattern),
          .skipExpression  = region.skipPattern.empty()
                                 ? QRegularExpression{}
                                 : compileExpression(region.skipPattern),
          .containedRules  = {},
          .multiline       = region.multiline,
      };
      for (const auto& contained : region.containedRules) {
        compiled.containedRules.push_back(
            {.style      = contained.style,
             .expression = compileExpression(contained.pattern)});
      }
      regionRules.push_back(std::move(compiled));
    }
  }

  rehighlight();
}

void CodeSyntaxHighlighter::setPalette(const QPalette& palette)
{
  const bool dark = palette.color(QPalette::Base).lightness()
                    < palette.color(QPalette::Text).lightness();
  const auto tokens = dark ? SILICON::ui::theme::dark() : SILICON::ui::theme::light();
  const auto color  = [&tokens](const QString& name) { return tokens.value(name); };

  formats[static_cast<std::size_t>(CodeSyntaxStyle::Statement)] =
      colored(color("SILICON_BLUE"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Label)] =
      colored(color("SILICON_BLUE"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Conditional)] =
      colored(color("SILICON_VIOLET"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Repeat)] =
      colored(color("SILICON_VIOLET"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Todo)] =
      colored(color("SILICON_ORANGE"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Operator)] =
      colored(color("SILICON_MAGENTA"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Comment)] =
      colored(color("SILICON_GREY"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Global)] =
      colored(color("SILICON_VIOLET"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Constant)] =
      colored(color("SILICON_ORANGE"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Number)] =
      colored(color("SILICON_ORANGE"));
  QColor stringColor = color("SILICON_GREEN");
  if (!dark)
    stringColor = stringColor.darker(180);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::String)] = colored(stringColor);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Escape)] =
      colored(color("SILICON_ORANGE"));
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Directive)] =
      colored(color("SILICON_ORANGE"));

  formats[static_cast<std::size_t>(CodeSyntaxStyle::Statement)].setFontWeight(
      QFont::DemiBold);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Conditional)].setFontWeight(
      QFont::DemiBold);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Repeat)].setFontWeight(
      QFont::DemiBold);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Todo)].setFontWeight(QFont::Bold);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Global)].setFontWeight(
      QFont::DemiBold);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Comment)].setFontItalic(true);
  formats[static_cast<std::size_t>(CodeSyntaxStyle::Directive)].setFontItalic(true);
  rehighlight();
}

const QTextCharFormat& CodeSyntaxHighlighter::format(const CodeSyntaxStyle style) const
{
  return formats[static_cast<std::size_t>(style)];
}

CodeSyntaxHighlighter::RegionEnd
CodeSyntaxHighlighter::findRegionEnd(const QString& text, const qsizetype from,
                                     const CompiledRegionRule& region)
{
  qsizetype searchFrom = from;
  while (searchFrom <= text.size()) {
    const auto end = region.endExpression.match(text, searchFrom);
    if (!end.hasMatch())
      return {-1, -1};

    if (region.skipExpression.isValid() && !region.skipExpression.pattern().isEmpty()) {
      const auto skip = region.skipExpression.match(text, searchFrom);
      if (skip.hasMatch() && skip.capturedStart() <= end.capturedStart()) {
        const qsizetype next = skip.capturedEnd();
        searchFrom           = next > searchFrom ? next : searchFrom + 1;
        continue;
      }
    }
    return {end.capturedStart(), end.capturedEnd()};
  }
  return {-1, -1};
}

void CodeSyntaxHighlighter::applyContainedRules(
    const QString& text, const qsizetype start, const qsizetype end,
    const std::vector<CompiledContainedRule>& rules)
{
  for (const auto& rule : rules) {
    auto iterator = rule.expression.globalMatch(text, start);
    while (iterator.hasNext()) {
      const auto match = iterator.next();
      if (match.capturedStart() >= end)
        break;
      const qsizetype matchEnd = std::min(match.capturedEnd(), end);
      setFormat(match.capturedStart(), matchEnd - match.capturedStart(),
                format(rule.style));
    }
  }
}

void CodeSyntaxHighlighter::highlightBlock(const QString& text)
{
  setCurrentBlockState(-1);
  if (!syntax)
    return;

  for (const auto& group : keywordGroups) {
    auto iterator = group.expression.globalMatch(text);
    while (iterator.hasNext()) {
      const auto match = iterator.next();
      setFormat(match.capturedStart(), match.capturedLength(), format(group.style));
    }
  }
  for (const auto& rule : matchRules) {
    auto iterator = rule.expression.globalMatch(text);
    while (iterator.hasNext()) {
      const auto match = iterator.next();
      setFormat(match.capturedStart(), match.capturedLength(), format(rule.style));
      applyContainedRules(text, match.capturedStart(), match.capturedEnd(),
                          rule.containedRules);
    }
  }

  qsizetype scanFrom = 0;
  if (previousBlockState() > 0) {
    const std::size_t index = static_cast<std::size_t>(previousBlockState() - 1);
    if (index < regionRules.size()) {
      const auto&     region    = regionRules[index];
      const auto      end       = findRegionEnd(text, 0, region);
      const qsizetype regionEnd = end.end < 0 ? text.size() : end.end;
      setFormat(0, regionEnd, format(region.style));
      applyContainedRules(text, 0, regionEnd, region.containedRules);
      if (end.end < 0) {
        setCurrentBlockState(previousBlockState());
        return;
      }
      scanFrom = regionEnd;
    }
  }

  while (scanFrom < text.size()) {
    std::size_t selectedIndex = regionRules.size();
    qsizetype   selectedStart = text.size();
    qsizetype   selectedEnd   = -1;
    for (std::size_t index = 0; index < regionRules.size(); ++index) {
      const auto match = regionRules[index].startExpression.match(text, scanFrom);
      if (match.hasMatch() && match.capturedStart() < selectedStart) {
        selectedIndex = index;
        selectedStart = match.capturedStart();
        selectedEnd   = match.capturedEnd();
      }
    }
    if (selectedIndex == regionRules.size())
      break;

    const auto&     region    = regionRules[selectedIndex];
    const auto      end       = findRegionEnd(text, selectedEnd, region);
    const qsizetype regionEnd = end.end < 0 ? text.size() : end.end;
    setFormat(selectedStart, regionEnd - selectedStart, format(region.style));
    applyContainedRules(text, selectedStart, regionEnd, region.containedRules);

    if (end.end < 0 && region.multiline) {
      setCurrentBlockState(static_cast<int>(selectedIndex + 1));
      return;
    }
    if (regionEnd <= scanFrom)
      ++scanFrom;
    else
      scanFrom = regionEnd;
  }
}

}  // namespace SILICON::ui
