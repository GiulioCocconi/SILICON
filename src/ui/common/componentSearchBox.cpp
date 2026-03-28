/*
 Copyright (c) 2025. Giulio Cocconi

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

#include "componentSearchBox.hpp"

#include <stdexcept>

ComponentSearchBox::ComponentSearchBox(SearchMap map, QString title,
                                       QGraphicsItem* parent)
  : QGraphicsProxyWidget(parent)
{
  if (map.empty())
    throw std::invalid_argument("ComponentSearchBox: completion map must not be empty");

  this->completionMap = std::move(map);

  titleItem = new QGraphicsTextItem(this);
  titleItem->setFont(QFont("Chango"));
  titleItem->setPlainText(title);

  // Calculate font baseline offset
  QFontMetrics fm(titleItem->font());
  titleItem->setPos(0, -fm.ascent() * 1.5);

  titleItem->setZValue(100);

  const int width = titleItem->boundingRect().width();
  le              = new QLineEdit();
  le->setFixedHeight(30);
  le->setFixedWidth(width);

  completer = new QCompleter(std::ranges::views::keys(this->completionMap)
                             | std::ranges::to<QStringList>());

  completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setFilterMode(Qt::MatchContains);
  completer->popup()->setFixedWidth(width);

  // Connect signals to handle showing the completer popup
  connect(le, &QLineEdit::textChanged, this, &ComponentSearchBox::showCompleter);
  connect(le, &QLineEdit::cursorPositionChanged, this,
          &ComponentSearchBox::showCompleter);

  le->setCompleter(completer);

  this->setWidget(le);
  this->setPos(0, 0);
}

void ComponentSearchBox::showCompleter()
{
  completer->setCompletionPrefix(le->text());
  completer->complete();
}

void ComponentSearchBox::keyPressEvent(QKeyEvent* event)
{
  qDebug() << event->key();
  if (event->key() == Qt::Key_Escape) {
    emit requestHide();
    return;
  }

  if (event->key() == Qt::Key_Return) {
    const std::string insertedText = le->text().toStdString();
    const auto        find         = completionMap.find(insertedText);

    if (find == completionMap.end())
      emit requestHide();
    else
      emit selectedComponent(find->second, mapToScene(this->pos()));

    return;
  }

  QGraphicsProxyWidget::keyPressEvent(event);
}
