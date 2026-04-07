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

#include "componentSearchBox.hpp"

#include <stdexcept>

ComponentSearchBox::ComponentSearchBox(std::vector<std::string> list, QString title,
                                       QGraphicsItem* parent)
  : QGraphicsProxyWidget(parent)
{
  if (list.empty())
    throw std::invalid_argument("ComponentSearchBox: completion list must not be empty");

  this->completionList = std::move(list);

  titleItem = new QGraphicsTextItem(this);
  titleItem->setFont(QFont("Chango"));
  titleItem->setPlainText(title);

  const QFontMetrics fm(titleItem->font());
  titleItem->setPos(0, -fm.ascent() * 1.5);

  titleItem->setZValue(100);

  const auto width = static_cast<int>(titleItem->boundingRect().width());
  le               = new QLineEdit();
  le->setFixedHeight(30);
  le->setFixedWidth(width);

  QStringList stringList;
  for (const auto& item : completionList) {
    stringList.append(QString::fromStdString(item));
  }
  completer = new QCompleter(stringList);

  completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setFilterMode(Qt::MatchContains);
  completer->popup()->setFixedWidth(width);

  connect(le, &QLineEdit::textChanged, this, &ComponentSearchBox::showCompleter);
  connect(le, &QLineEdit::cursorPositionChanged, this,
          &ComponentSearchBox::showCompleter);

  le->setCompleter(completer);

  this->setWidget(le);
  this->setPos(0, 0);
}

void ComponentSearchBox::showCompleter() const
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
    const QString insertedText = le->text();

    auto it = std::ranges::find_if(completionList, [&](const std::string& name) {
      return QString::fromStdString(name).compare(insertedText, Qt::CaseInsensitive) == 0;
    });

    if (it == completionList.end())
      emit requestHide();
    else
      emit selectedComponent(*it, mapToScene(this->pos()));

    return;
  }

  QGraphicsProxyWidget::keyPressEvent(event);
}
