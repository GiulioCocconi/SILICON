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

#pragma once

#include <QPlainTextEdit>

class HDLLineNumberArea;
class QEvent;
class QPaintEvent;
class QResizeEvent;
class QWidget;

/**
 * @brief Plain-text HDL editor with a non-editable line-number gutter.
 *
 * The gutter tracks visible QTextBlocks, scrolling, font changes, palette changes, and
 * the current cursor line. Language compilation and code-mode state belong to
 * LogiFlowWindow; this widget intentionally contains no Yosys or project-file logic.
 */
class HDLCodeEditor : public QPlainTextEdit {
public:
  explicit HDLCodeEditor(QWidget* parent = nullptr);

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  friend class HDLLineNumberArea;

  /** @brief Returns the gutter width required by the current document line count. */
  [[nodiscard]] int lineNumberAreaWidth() const;
  /** @brief Paints visible one-based line numbers into the gutter widget. */
  void              paintLineNumberArea(QPaintEvent* event);
  /** @brief Reserves viewport space for the current gutter width. */
  void              updateLineNumberAreaWidth();

  HDLLineNumberArea* lineNumberArea;
};
