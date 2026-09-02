/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <ui/logiFlow/code/codeEditor.hpp>

#include "codeSyntaxHighlighter.hpp"

#include <algorithm>
#include <stdexcept>

#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QWidget>

namespace SILICON::ui {

class CodeLineNumberArea : public QWidget {
public:
  explicit CodeLineNumberArea(CodeEditor* editor) : QWidget(editor), editor(editor) {}

  [[nodiscard]] QSize sizeHint() const override
  {
    return {editor->lineNumberAreaWidth(), 0};
  }

protected:
  void paintEvent(QPaintEvent* event) override { editor->paintLineNumberArea(event); }

private:
  CodeEditor* editor;
};

CodeEditor::CodeEditor(QWidget* parent)
  : QPlainTextEdit(parent),
    lineNumberArea(new CodeLineNumberArea(this)),
    syntaxHighlighter(new CodeSyntaxHighlighter(document())),
    completer(new QCompleter(this)),
    completionModel(new QStringListModel(this))
{
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setProperty("class", "mono");
  completer->setWidget(this);
  completer->setModel(completionModel);
  completer->setCaseSensitivity(Qt::CaseSensitive);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setModelSorting(QCompleter::CaseSensitivelySortedModel);

  connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
          [this](const QString& completion) { insertCompletion(completion); });
  connect(this, &QPlainTextEdit::blockCountChanged, this,
          [this] { updateLineNumberAreaWidth(); });
  connect(this, &QPlainTextEdit::updateRequest, this,
          [this](const QRect& rect, const int dy) {
            if (dy)
              lineNumberArea->scroll(0, dy);
            else
              lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
            if (rect.contains(viewport()->rect()))
              updateLineNumberAreaWidth();
          });
  connect(this, &QPlainTextEdit::cursorPositionChanged, lineNumberArea, [this] {
    lineNumberArea->update();
    completer->popup()->hide();
  });
  updateLineNumberAreaWidth();
}

void CodeEditor::setFileType(const SILICON::project::DocumentType type)
{
  if (SILICON::project::categoryOf(type) != SILICON::project::DocumentCategory::Code)
    throw std::invalid_argument("Code editor requires a code document type");

  const auto syntax = SILICON::project::syntaxDefinition(type);
  if (!syntax)
    throw std::invalid_argument("Code document type has no syntax definition");

  fileTypeValue = type;
  syntaxHighlighter->setSyntax(&syntax->get());
  refreshTheme();
  rebuildCompletionCandidates();
}

void CodeEditor::clearFileType()
{
  fileTypeValue.reset();
  syntaxHighlighter->setSyntax(nullptr);
  completionModel->setStringList({});
  completer->popup()->hide();
  clear();
}

const std::optional<SILICON::project::DocumentType>& CodeEditor::fileType() const
{
  return fileTypeValue;
}

QStringList CodeEditor::completionCandidates() const
{
  return completionModel->stringList();
}

bool CodeEditor::isCompletionPopupVisible() const
{
  return completer->popup()->isVisible();
}

void CodeEditor::refreshTheme()
{
  syntaxHighlighter->setPalette(palette());
}

void CodeEditor::rebuildCompletionCandidates()
{
  QStringList candidates;
  if (fileTypeValue) {
    const auto& syntax = SILICON::project::syntaxDefinition(*fileTypeValue)->get();
    for (const auto& group : syntax.keywordGroups) {
      for (const auto word : group.words) {
        candidates.append(
            QString::fromUtf8(word.data(), static_cast<qsizetype>(word.size())));
      }
    }
  }
  candidates.removeDuplicates();
  candidates.sort(Qt::CaseSensitive);
  completionModel->setStringList(candidates);
}

bool CodeEditor::isWordDelimiter(const QChar character) const
{
  if (!fileTypeValue)
    return true;
  const auto& syntax = SILICON::project::syntaxDefinition(*fileTypeValue)->get();
  const auto  extra =
      QString::fromUtf8(syntax.extraWordCharacters.data(),
                        static_cast<qsizetype>(syntax.extraWordCharacters.size()));
  return !character.isLetterOrNumber() && character != QLatin1Char('_')
         && !extra.contains(character);
}

QString CodeEditor::completionPrefix() const
{
  const QTextCursor cursor = textCursor();
  const QString     text   = cursor.block().text();
  int               start  = cursor.positionInBlock();
  while (start > 0 && !isWordDelimiter(text.at(start - 1)))
    --start;
  return text.mid(start, cursor.positionInBlock() - start);
}

void CodeEditor::showCompletion(const bool explicitRequest)
{
  if (!fileTypeValue || completionModel->rowCount() == 0)
    return;
  const QString prefix = completionPrefix();
  if (!explicitRequest && prefix.size() < 2) {
    completer->popup()->hide();
    return;
  }
  completer->setCompletionPrefix(prefix);
  if (completer->completionCount() == 0) {
    completer->popup()->hide();
    return;
  }
  constexpr int CompletionPopupPadding = 8;
  QRect         rect                   = cursorRect();
  rect.translate(0, CompletionPopupPadding);
  rect.setWidth(completer->popup()->sizeHintForColumn(0)
                + completer->popup()->verticalScrollBar()->sizeHint().width());
  completer->complete(rect);
  completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
}

void CodeEditor::insertCompletion(const QString& completion)
{
  QTextCursor cursor = textCursor();
  const auto  prefix = completionPrefix();
  cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefix.size());
  cursor.insertText(completion);
  setTextCursor(cursor);
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
  if (completer->popup()->isVisible()) {
    switch (event->key()) {
      case Qt::Key_Enter:
      case Qt::Key_Return:
      case Qt::Key_Tab:
      case Qt::Key_Backtab:
        if (const auto index = completer->popup()->currentIndex(); index.isValid())
          insertCompletion(index.data(Qt::DisplayRole).toString());
        completer->popup()->hide();
        event->accept();
        return;
      case Qt::Key_Escape:
        completer->popup()->hide();
        event->accept();
        return;
      default: break;
    }
  }

  if (event->matches(QKeySequence::InsertParagraphSeparator)) {
    QPlainTextEdit::keyPressEvent(event);
    completer->popup()->hide();
    return;
  }
  const bool explicitRequest =
      event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Space;
  const bool typedText =
      !event->text().isEmpty()
      && !(event->modifiers()
           & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  if (!explicitRequest)
    QPlainTextEdit::keyPressEvent(event);
  if (explicitRequest || typedText)
    showCompletion(explicitRequest);
  else
    completer->popup()->hide();
}

int CodeEditor::lineNumberAreaWidth() const
{
  int digits = 1;
  for (int lines = std::max(1, blockCount()); lines >= 10; lines /= 10)
    ++digits;
  return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::paintLineNumberArea(QPaintEvent* event)
{
  QPainter painter(lineNumberArea);
  painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
  QTextBlock block       = firstVisibleBlock();
  int        blockNumber = block.blockNumber();
  int        top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int        bottom       = top + qRound(blockBoundingRect(block).height());
  const int  currentBlock = textCursor().blockNumber();
  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      painter.setPen(palette().color(
          blockNumber == currentBlock ? QPalette::Text : QPalette::PlaceholderText));
      painter.drawText(0, top, lineNumberArea->width() - 6, fontMetrics().height(),
                       Qt::AlignRight, QString::number(blockNumber + 1));
    }
    block  = block.next();
    top    = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++blockNumber;
  }
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
  QPlainTextEdit::resizeEvent(event);
  const QRect contents = contentsRect();
  lineNumberArea->setGeometry(
      QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void CodeEditor::changeEvent(QEvent* event)
{
  QPlainTextEdit::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    refreshTheme();
    lineNumberArea->update();
  } else if (event->type() == QEvent::FontChange
             || event->type() == QEvent::ApplicationFontChange) {
    updateLineNumberAreaWidth();
    lineNumberArea->update();
  }
}

void CodeEditor::updateLineNumberAreaWidth()
{
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

}  // namespace SILICON::ui
