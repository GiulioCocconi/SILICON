/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <optional>
#include <vector>

#include <QPlainTextEdit>
#include <QRegularExpression>

#include <core/projectDocument.hpp>

class QCompleter;
class QContextMenuEvent;
class QEvent;
class QFont;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QStringListModel;
class QWidget;

namespace SILICON::ui {

class CodeLineNumberArea;
class CodeSyntaxHighlighter;

/** Metadata-driven source editor with highlighting and completion. */
class CodeEditor : public QPlainTextEdit {
public:
  explicit CodeEditor(QWidget* parent = nullptr);

  void setFileType(SILICON::project::DocumentType type);
  void clearFileType();

  [[nodiscard]] const std::optional<SILICON::project::DocumentType>& fileType() const;
  [[nodiscard]] QString completionPrefix() const;

protected:
#ifndef QT_NO_CONTEXTMENU
  void contextMenuEvent(QContextMenuEvent* event) override;
#endif
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  friend class CodeLineNumberArea;

  [[nodiscard]] int   lineNumberAreaWidth() const;
  void                paintLineNumberArea(QPaintEvent* event);
  [[nodiscard]] QFont lineNumberFont() const;
  void                updateLineNumberAreaWidth();
  void                updateLineNumberAreaGeometry();
  void                refreshTheme();
  void                rebuildCompletionCandidates();
  void                rebuildIndentationTriggers();
  [[nodiscard]] int   currentIndentWidth() const;
  void                updateIndentationSettings();
  void                showCompletion(bool explicitRequest);
  void                insertCompletion(const QString& completion);
  [[nodiscard]] bool  insertAutoPair(QChar typed);
  [[nodiscard]] bool  removeEmptyPair();
  [[nodiscard]] bool  expandPairedDelimiters();
  void                indentCurrentLine();
  void                dedentSelection();
  [[nodiscard]] bool  currentLineMatchesIndentationTrigger() const;
  [[nodiscard]] bool  isWordDelimiter(QChar character) const;

  CodeLineNumberArea*                           lineNumberArea;
  CodeSyntaxHighlighter*                        syntaxHighlighter;
  QCompleter*                                   completer;
  QStringListModel*                             completionModel;
  std::vector<QRegularExpression>               indentationTriggers;
  std::optional<SILICON::project::DocumentType> fileTypeValue;
  qreal                                         zoomFontPointSize = 0.0;
};

}  // namespace SILICON::ui
