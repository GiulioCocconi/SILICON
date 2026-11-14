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

#include "utils.hpp"

#include <QPainter>

#include <ui/shell/theme.hpp>

namespace SILICON::ui {
SubcircuitRectShape::SubcircuitRectShape(const QSize& size, QGraphicsItem* parent)
  : QGraphicsRectItem(0, 0, size.width(), size.height(), parent)
{
}

void SubcircuitRectShape::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                                QWidget* widget)
{
  setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink), 3));
  setBrush(ThemeEngine::getColor(theme::ColorToken::Internal));
  QGraphicsRectItem::paint(painter, option, widget);
}

}  // namespace SILICON::ui
