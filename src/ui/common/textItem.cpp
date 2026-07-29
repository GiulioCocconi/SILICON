/*
  Copyright (C) 2025 Giulio Cocconi

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

#include "textItem.hpp"


namespace SILICON {
namespace ui {
using namespace SILICON::core;

TextItem::TextItem(std::string_view text, QGraphicsItem* parent) : QGraphicsItem(parent)
{
  this->setText(text);
}

void TextItem::setText(std::string_view text)
{
  const QString qText =
      QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
  this->rect = QFontMetrics(QApplication::font()).boundingRect(qText);
  this->text = qText;
}

}  // namespace ui
}  // namespace SILICON
