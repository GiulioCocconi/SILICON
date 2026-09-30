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

#include <functional>

#include <QPlainTextEdit>

class QFocusEvent;
class QWidget;


namespace SILICON::ui {

/**
 * @brief Description editor that commits pending metadata when focus leaves it.
 *
 * The caller supplies the commit operation, keeping project metadata and undo-stack
 * policy outside this reusable widget.
 */
class MetadataDescriptionEdit : public QPlainTextEdit {
public:
  explicit MetadataDescriptionEdit(QWidget* parent = nullptr);

  std::function<void()> commit;

protected:
  void focusOutEvent(QFocusEvent* event) override;
};

}  // namespace SILICON::ui
