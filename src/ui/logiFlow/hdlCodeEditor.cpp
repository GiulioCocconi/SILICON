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

#include "hdlCodeEditor.hpp"

#include <algorithm>

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QWidget>

class HDLLineNumberArea : public QWidget {
public:
  explicit HDLLineNumberArea(HDLCodeEditor* editor)
    : QWidget(editor),
      editor(editor)
  {
  }

  [[nodiscard]] QSize sizeHint() const override
  {
    return {editor->lineNumberAreaWidth(), 0};
  }

protected:
  void paintEvent(QPaintEvent* event) override
  {
    editor->paintLineNumberArea(event);
  }

private:
  HDLCodeEditor* editor;
};

HDLCodeEditor::HDLCodeEditor(QWidget* parent)
  : QPlainTextEdit(parent),
    lineNumberArea(new HDLLineNumberArea(this))
{
  connect(this, &QPlainTextEdit::blockCountChanged, this,
          [this] { updateLineNumberAreaWidth(); });
  connect(this, &QPlainTextEdit::updateRequest, this,
          [this](const QRect& rect, const int dy) {
            if (dy)
              lineNumberArea->scroll(0, dy);
            else
              lineNumberArea->update(0, rect.y(), lineNumberArea->width(),
                                     rect.height());

            if (rect.contains(viewport()->rect()))
              updateLineNumberAreaWidth();
          });
  connect(this, &QPlainTextEdit::cursorPositionChanged, lineNumberArea,
          [this] { lineNumberArea->update(); });
  updateLineNumberAreaWidth();
}

int HDLCodeEditor::lineNumberAreaWidth() const
{
  int digits = 1;
  for (int lines = std::max(1, blockCount()); lines >= 10; lines /= 10)
    ++digits;
  return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void HDLCodeEditor::paintLineNumberArea(QPaintEvent* event)
{
  QPainter painter(lineNumberArea);
  painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));

  QTextBlock block       = firstVisibleBlock();
  int        blockNumber = block.blockNumber();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());
  const int currentBlock = textCursor().blockNumber();

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      painter.setPen(palette().color(blockNumber == currentBlock
                                         ? QPalette::Text
                                         : QPalette::PlaceholderText));
      painter.drawText(0, top, lineNumberArea->width() - 6,
                       fontMetrics().height(), Qt::AlignRight,
                       QString::number(blockNumber + 1));
    }

    block = block.next();
    top   = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++blockNumber;
  }
}

void HDLCodeEditor::resizeEvent(QResizeEvent* event)
{
  QPlainTextEdit::resizeEvent(event);
  const QRect contents = contentsRect();
  lineNumberArea->setGeometry(
      QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void HDLCodeEditor::changeEvent(QEvent* event)
{
  QPlainTextEdit::changeEvent(event);
  if (event->type() == QEvent::FontChange
      || event->type() == QEvent::ApplicationFontChange
      || event->type() == QEvent::PaletteChange) {
    updateLineNumberAreaWidth();
    lineNumberArea->update();
  }
}

void HDLCodeEditor::updateLineNumberAreaWidth()
{
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}
