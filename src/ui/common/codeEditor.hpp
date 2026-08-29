/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <optional>

#include <QPlainTextEdit>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>

#include <core/codeFile.hpp>

class QCompleter;
class QEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QStringListModel;
class QWidget;

namespace KSyntaxHighlighting {
class SyntaxHighlighter;
}

namespace SILICON::ui {

class CodeLineNumberArea;

/** Reusable, metadata-driven source editor with KDE highlighting and completion. */
class CodeEditor : public QPlainTextEdit {
public:
  explicit CodeEditor(QWidget* parent = nullptr);

  void setFileType(SILICON::project::CodeFileType type);
  void clearFileType();

  [[nodiscard]] const std::optional<SILICON::project::CodeFileType>& fileType() const;
  [[nodiscard]] QStringList completionCandidates() const;
  [[nodiscard]] QString     completionPrefix() const;
  [[nodiscard]] QString     highlightingThemeName() const;
  [[nodiscard]] bool        isCompletionPopupVisible() const;

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  friend class CodeLineNumberArea;

  [[nodiscard]] int  lineNumberAreaWidth() const;
  void               paintLineNumberArea(QPaintEvent* event);
  void               updateLineNumberAreaWidth();
  void               refreshTheme();
  void               rebuildCompletionCandidates();
  void               showCompletion(bool explicitRequest);
  void               insertCompletion(const QString& completion);
  [[nodiscard]] bool isWordDelimiter(QChar character) const;

  CodeLineNumberArea*                               lineNumberArea;
  KSyntaxHighlighting::Repository                   repository;
  KSyntaxHighlighting::SyntaxHighlighter*           syntaxHighlighter;
  KSyntaxHighlighting::Definition                   definition;
  QCompleter*                                       completer;
  QStringListModel*                                 completionModel;
  std::optional<SILICON::project::CodeFileType> fileTypeValue;
};

}  // namespace SILICON::ui
