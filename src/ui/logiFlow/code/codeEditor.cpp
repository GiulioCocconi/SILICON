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
#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <QAbstractItemView>
#include <QByteArray>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QWheelEvent>
#include <QWidget>

#include <ui/common/codeFilePresentation.hpp>

namespace SILICON::ui {
namespace {

  [[nodiscard]] QChar closingDelimiter(const QChar opening)
  {
    switch (opening.unicode()) {
      case '(': return QLatin1Char(')');
      case '[': return QLatin1Char(']');
      case '{': return QLatin1Char('}');
      default: return {};
    }
  }

  [[nodiscard]] bool isClosingDelimiter(const QChar character)
  {
    return character == QLatin1Char(')') || character == QLatin1Char(']')
           || character == QLatin1Char('}');
  }

}  // namespace

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
  updateIndentationSettings();
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
  if (!SILICON::project::isCodeDocument(type))
    throw std::invalid_argument("Code editor requires a code document type");

  const auto* syntax = codeFilePresentation(type).syntax;
  if (!syntax)
    throw std::invalid_argument("Code document type has no syntax definition");

  fileTypeValue = type;
  syntaxHighlighter->setSyntax(syntax);
  refreshTheme();
  rebuildCompletionCandidates();
  rebuildIndentationTriggers();
  updateIndentationSettings();
}

void CodeEditor::clearFileType()
{
  fileTypeValue.reset();
  syntaxHighlighter->setSyntax(nullptr);
  completionModel->setStringList({});
  indentationTriggers.clear();
  completer->popup()->hide();
  clear();
}

const std::optional<SILICON::project::DocumentType>& CodeEditor::fileType() const
{
  return fileTypeValue;
}

void CodeEditor::refreshTheme()
{
  syntaxHighlighter->setPalette(palette());
}

void CodeEditor::rebuildCompletionCandidates()
{
  QStringList candidates;
  if (fileTypeValue) {
    const auto& syntax = *codeFilePresentation(*fileTypeValue).syntax;
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

void CodeEditor::rebuildIndentationTriggers()
{
  indentationTriggers.clear();
  if (!fileTypeValue)
    return;

  const auto* indentation = codeFilePresentation(*fileTypeValue).syntax->indentation;
  if (!indentation)
    return;

  indentationTriggers.reserve(indentation->triggerPatterns.size());
  for (const auto pattern : indentation->triggerPatterns) {
    QRegularExpression expression(
        QString::fromUtf8(pattern.data(), static_cast<qsizetype>(pattern.size())));
    if (!expression.isValid())
      throw std::invalid_argument(QString("Invalid indentation trigger '%1': %2")
                                      .arg(expression.pattern(), expression.errorString())
                                      .toStdString());
    indentationTriggers.push_back(std::move(expression));
  }
}

bool CodeEditor::currentLineMatchesIndentationTrigger() const
{
  const auto line = textCursor().block().text();
  return std::ranges::any_of(indentationTriggers, [&line](const auto& expression) {
    return expression.match(line).hasMatch();
  });
}

void CodeEditor::indentCurrentLine()
{
  if (!fileTypeValue)
    return;

  const auto* indentation = codeFilePresentation(*fileTypeValue).syntax->indentation;
  if (!indentation)
    return;

  QTextCursor cursor       = textCursor();
  const auto  currentBlock = cursor.block();
  const auto  currentText  = currentBlock.text();
  const auto  currentUtf8  = currentText.toUtf8();

  std::array<QByteArray, 2>       previousUtf8;
  std::array<std::string_view, 2> previousLines;
  std::size_t                     previousCount = 0;
  for (auto block = currentBlock.previous(); block.isValid() && previousCount < 2;
       block      = block.previous()) {
    if (block.text().trimmed().isEmpty())
      continue;
    previousUtf8[previousCount] = block.text().toUtf8();
    previousLines[previousCount] =
        std::string_view(previousUtf8[previousCount].constData(),
                         static_cast<std::size_t>(previousUtf8[previousCount].size()));
    ++previousCount;
  }

  const CodeIndentationContext context{
      .currentLine = std::string_view(currentUtf8.constData(),
                                      static_cast<std::size_t>(currentUtf8.size())),
      .previousNonBlankLines =
          std::span<const std::string_view>(previousLines).first(previousCount),
      .indentWidth = currentIndentWidth(),
  };
  const auto desiredIndentUtf8 = indentation->indentationFor(context);
  const auto desiredIndent     = QString::fromUtf8(
      desiredIndentUtf8.data(), static_cast<qsizetype>(desiredIndentUtf8.size()));

  qsizetype existingIndentLength = 0;
  while (existingIndentLength < currentText.size()
         && (currentText[existingIndentLength] == QLatin1Char(' ')
             || currentText[existingIndentLength] == QLatin1Char('\t'))) {
    ++existingIndentLength;
  }
  if (currentText.first(existingIndentLength) == desiredIndent)
    return;

  const int contentOffset =
      std::max(0, cursor.positionInBlock() - static_cast<int>(existingIndentLength));
  cursor.beginEditBlock();
  QTextCursor indentationCursor(document());
  indentationCursor.setPosition(currentBlock.position());
  indentationCursor.setPosition(currentBlock.position()
                                    + static_cast<int>(existingIndentLength),
                                QTextCursor::KeepAnchor);
  indentationCursor.insertText(desiredIndent);
  cursor.setPosition(currentBlock.position() + desiredIndent.size() + contentOffset);
  setTextCursor(cursor);
  cursor.endEditBlock();
}

bool CodeEditor::isWordDelimiter(const QChar character) const
{
  if (!fileTypeValue)
    return true;
  const auto& syntax = *codeFilePresentation(*fileTypeValue).syntax;
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
  cursor.beginEditBlock();
  const auto prefix = completionPrefix();
  cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefix.size());
  cursor.insertText(completion);
  setTextCursor(cursor);
  if (currentLineMatchesIndentationTrigger())
    indentCurrentLine();
  cursor.endEditBlock();
}

bool CodeEditor::insertAutoPair(const QChar typed)
{
  QTextCursor cursor = textCursor();
  const auto  line   = cursor.block().text();
  const int   offset = cursor.positionInBlock();
  const QChar next   = offset < line.size() ? line.at(offset) : QChar{};
  const QChar close  = closingDelimiter(typed);

  if (!close.isNull()) {
    if (cursor.hasSelection()) {
      const int  start = cursor.selectionStart();
      const auto selection =
          cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
      cursor.insertText(QString(typed) + selection + close);
      cursor.setPosition(start + 1);
      cursor.setPosition(start + 1 + selection.size(), QTextCursor::KeepAnchor);
    } else if (next == close) {
      cursor.insertText(QString(typed));
    } else if (next.isNull() || next.isSpace() || isClosingDelimiter(next)
               || next == QLatin1Char(';') || next == QLatin1Char(',')) {
      const int position = cursor.position();
      cursor.insertText(QString(typed) + close);
      cursor.setPosition(position + 1);
    } else {
      return false;
    }
    setTextCursor(cursor);
    return true;
  }

  if (!cursor.hasSelection() && isClosingDelimiter(typed) && next == typed) {
    cursor.movePosition(QTextCursor::Right);
    setTextCursor(cursor);
    return true;
  }

  return false;
}

bool CodeEditor::removeEmptyPair()
{
  QTextCursor cursor = textCursor();
  if (cursor.hasSelection())
    return false;

  const auto line   = cursor.block().text();
  const int  offset = cursor.positionInBlock();
  if (offset <= 0 || offset >= line.size()
      || closingDelimiter(line.at(offset - 1)) != line.at(offset))
    return false;

  cursor.setPosition(cursor.position() - 1);
  cursor.setPosition(cursor.position() + 2, QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  setTextCursor(cursor);
  return true;
}

bool CodeEditor::expandPairedDelimiters()
{
  if (!fileTypeValue)
    return false;
  const auto* indentation = codeFilePresentation(*fileTypeValue).syntax->indentation;
  if (!indentation || !indentation->expandPairsOnNewline)
    return false;

  QTextCursor cursor = textCursor();
  if (cursor.hasSelection())
    return false;

  const auto line   = cursor.block().text();
  const int  offset = cursor.positionInBlock();
  if (offset <= 0 || offset >= line.size()
      || closingDelimiter(line.at(offset - 1)) != line.at(offset))
    return false;

  qsizetype prefixLength = 0;
  while (prefixLength < line.size()
         && (line.at(prefixLength) == QLatin1Char(' ')
             || line.at(prefixLength) == QLatin1Char('\t')))
    ++prefixLength;

  const auto baseIndent = line.first(prefixLength);
  const auto innerIndent =
      baseIndent + QString(indentation->indentWidth, QLatin1Char(' '));
  const int position = cursor.position();
  cursor.insertText(QLatin1Char('\n') + innerIndent + QLatin1Char('\n') + baseIndent);
  cursor.setPosition(position + 1 + innerIndent.size());
  setTextCursor(cursor);
  return true;
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

  const bool plainKey =
      fileTypeValue && !isReadOnly()
      && !(event->modifiers()
           & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  if (plainKey && event->key() == Qt::Key_Backspace && removeEmptyPair()) {
    completer->popup()->hide();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
      QPlainTextEdit::keyPressEvent(event);
      return;
    }
    const int indent = currentIndentWidth();
    if (event->key() == Qt::Key_Tab) {
      QTextCursor cursor = textCursor();
      cursor.insertText(QString(indent, QLatin1Char(' ')));
      setTextCursor(cursor);
    } else {
      dedentSelection();
    }
    event->accept();
    return;
  }

  if (event->matches(QKeySequence::InsertParagraphSeparator)) {
    if (plainKey && expandPairedDelimiters()) {
      completer->popup()->hide();
      event->accept();
      return;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    QPlainTextEdit::keyPressEvent(event);
    indentCurrentLine();
    cursor.endEditBlock();
    completer->popup()->hide();
    return;
  }
  const bool explicitRequest =
      event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Space;
  const bool typedText =
      !event->text().isEmpty()
      && !(event->modifiers()
           & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  if (plainKey && event->text().size() == 1 && insertAutoPair(event->text().front())) {
    completer->popup()->hide();
    event->accept();
    return;
  }
  if (!explicitRequest)
    QPlainTextEdit::keyPressEvent(event);
  if (typedText && currentLineMatchesIndentationTrigger())
    indentCurrentLine();
  if (explicitRequest || typedText)
    showCompletion(explicitRequest);
  else
    completer->popup()->hide();
}

#ifndef QT_NO_CONTEXTMENU
void CodeEditor::contextMenuEvent(QContextMenuEvent* event)
{
  #ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  #else
  QMenu stackMenu(this);
  auto* menu = &stackMenu;
  #endif

  auto* cutAction = menu->addAction(tr("Cut"), this, &QPlainTextEdit::cut);
  cutAction->setEnabled(!isReadOnly() && textCursor().hasSelection());
  auto* copyAction = menu->addAction(tr("Copy"), this, &QPlainTextEdit::copy);
  copyAction->setEnabled(textCursor().hasSelection());
  auto* pasteAction = menu->addAction(tr("Paste"), this, &QPlainTextEdit::paste);
  pasteAction->setEnabled(!isReadOnly() && canPaste());
  auto* deleteAction = menu->addAction(tr("Delete"), this, [this] {
    auto cursor = textCursor();
    cursor.removeSelectedText();
  });
  deleteAction->setEnabled(!isReadOnly() && textCursor().hasSelection());
  menu->addSeparator();
  auto* selectAllAction =
      menu->addAction(tr("Select All"), this, &QPlainTextEdit::selectAll);
  selectAllAction->setEnabled(!document()->isEmpty());

  #ifdef __EMSCRIPTEN__
  menu->popup(event->globalPos());
  #else
  menu->exec(event->globalPos());
  #endif
  event->accept();
}
#endif

void CodeEditor::wheelEvent(QWheelEvent* event)
{
  const int delta = event->angleDelta().y();
  if (!(event->modifiers() & Qt::ControlModifier) || delta == 0) {
    QPlainTextEdit::wheelEvent(event);
    return;
  }

  constexpr qreal MinimumFontSize = 6.0;
  constexpr qreal MaximumFontSize = 48.0;
  const int       steps           = std::max(1, std::abs(delta) / 120);

  if (zoomFontPointSize <= 0.0)
    zoomFontPointSize = QFontInfo(font()).pointSizeF();
  if (zoomFontPointSize <= 0.0)
    zoomFontPointSize = 10.0;
  zoomFontPointSize = std::clamp(zoomFontPointSize + (delta > 0 ? steps : -steps),
                                 MinimumFontSize, MaximumFontSize);

  QFont editorFont = font();
  editorFont.setPointSizeF(zoomFontPointSize);
  setFont(editorFont);
  document()->setDefaultFont(editorFont);
  updateIndentationSettings();
  updateLineNumberAreaWidth();
  lineNumberArea->update();
  event->accept();
}

QFont CodeEditor::lineNumberFont() const
{
  QFont lineFont = document()->defaultFont();
  if (zoomFontPointSize > 0.0)
    lineFont.setPointSizeF(zoomFontPointSize);
  return lineFont;
}

int CodeEditor::lineNumberAreaWidth() const
{
  int digits = 1;
  for (int lines = std::max(1, blockCount()); lines >= 10; lines /= 10)
    ++digits;
  return 12 + QFontMetrics(lineNumberFont()).horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::paintLineNumberArea(QPaintEvent* event)
{
  QPainter painter(lineNumberArea);
  painter.setFont(lineNumberFont());
  const QFontMetrics metrics = painter.fontMetrics();
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
      painter.drawText(0, top, lineNumberArea->width() - 6, metrics.height(),
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
  updateLineNumberAreaGeometry();
}

void CodeEditor::changeEvent(QEvent* event)
{
  QPlainTextEdit::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    refreshTheme();
    lineNumberArea->update();
  } else if (event->type() == QEvent::FontChange
             || event->type() == QEvent::ApplicationFontChange) {
    updateIndentationSettings();
    updateLineNumberAreaWidth();
    lineNumberArea->update();
  }
}

void CodeEditor::updateLineNumberAreaWidth()
{
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  updateLineNumberAreaGeometry();
}

void CodeEditor::updateLineNumberAreaGeometry()
{
  const QRect contents = contentsRect();
  lineNumberArea->setGeometry(
      QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

int CodeEditor::currentIndentWidth() const
{
  if (!fileTypeValue)
    return 4;
  const auto* indentation = codeFilePresentation(*fileTypeValue).syntax->indentation;
  return indentation ? indentation->indentWidth : 4;
}

void CodeEditor::updateIndentationSettings()
{
  const int width = currentIndentWidth();
  setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' '))
                     * static_cast<qreal>(width));
}

void CodeEditor::dedentSelection()
{
  QTextCursor cursor = textCursor();
  const int   indent = currentIndentWidth();
  cursor.beginEditBlock();

  const QTextBlock firstBlock = cursor.block();
  const QTextBlock lastBlock =
      cursor.hasSelection() ? document()->findBlock(cursor.selectionEnd()) : firstBlock;

  std::vector<std::pair<int, int>> removals;
  for (QTextBlock block = firstBlock;
       block.isValid() && block.position() <= lastBlock.position();
       block = block.next()) {
    const QString text        = block.text();
    int           removeCount = 0;
    while (removeCount < text.size() && removeCount < indent) {
      const QChar ch = text.at(removeCount);
      if (ch == QLatin1Char(' ')) {
        ++removeCount;
      } else if (ch == QLatin1Char('\t')) {
        ++removeCount;
        break;
      } else {
        break;
      }
    }
    if (removeCount > 0)
      removals.emplace_back(block.position(), removeCount);
  }

  for (auto it = removals.rbegin(); it != removals.rend(); ++it) {
    QTextCursor blockCursor(document());
    blockCursor.setPosition(it->first);
    blockCursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
    blockCursor.removeSelectedText();
  }

  cursor.endEditBlock();
  setTextCursor(cursor);
}

}  // namespace SILICON::ui
