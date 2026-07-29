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
#include <vector>

#include <ui/common/componentSearchMatcher.hpp>
#include <ui/common/theme.hpp>


namespace SILICON {
namespace ui {

ComponentSearchBox::ComponentSearchBox(std::vector<std::string> list, QString title,
                                       QGraphicsItem* parent)
  : QGraphicsProxyWidget(parent)
{
  if (list.empty())
    throw std::invalid_argument("ComponentSearchBox: completion list must not be empty");

  this->completionList = std::move(list);
  setFlag(QGraphicsItem::ItemIgnoresTransformations);

  titleItem = new QGraphicsTextItem(this);
  titleItem->setFont(QFont("Chango"));
  titleItem->setDefaultTextColor(ThemeEngine::getColor("SILICON_INK"));
  titleItem->setPlainText(title);

  const QFontMetrics fm(titleItem->font());
  titleItem->setPos(0, -fm.ascent() * 1.5);

  titleItem->setZValue(100);

  const auto width = static_cast<int>(titleItem->boundingRect().width());
  le               = new QLineEdit();
  le->setFixedHeight(30);
  le->setFixedWidth(width);

  completionModel = new QStringListModel(this);
  completer       = new QCompleter(completionModel, this);

  completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setFilterMode(Qt::MatchContains);
  completer->setMaxVisibleItems(5);
  completer->popup()->setFixedWidth(le->width());
  completer->popup()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  connect(le, &QLineEdit::textChanged, this, &ComponentSearchBox::showCompleter);
  connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
          &ComponentSearchBox::acceptCompletion);

  le->installEventFilter(this);
  completer->popup()->installEventFilter(this);
  completer->setWidget(le);

  this->setWidget(le);
  this->setPos(0, 0);
  updateCompletionModel();
}

void ComponentSearchBox::showCompleter() const
{
  titleItem->setDefaultTextColor(ThemeEngine::getColor("SILICON_INK"));
  updateCompletionModel();
  if (completionModel->rowCount() == 0) {
    completer->popup()->hide();
    return;
  }

  completer->setCompletionPrefix(QString());
  completer->popup()->setFixedWidth(le->width());
  completer->complete(le->rect());
}

void ComponentSearchBox::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Escape) {
    completer->popup()->hide();
    emit requestCancel();
    return;
  }

  if (event->key() == Qt::Key_Return) {
    const auto typeName = selectedTypeName();
    if (typeName.empty())
      emit requestHide();
    else
      emit selectedComponent(typeName, scenePos());

    event->accept();
    return;
  }

  QGraphicsProxyWidget::keyPressEvent(event);
}

bool ComponentSearchBox::eventFilter(QObject* watched, QEvent* event)
{
  if ((watched == le || watched == completer->popup())
      && event->type() == QEvent::KeyPress) {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Escape) {
      completer->popup()->hide();
      emit requestCancel();
      return true;
    }
  }

  return QGraphicsProxyWidget::eventFilter(watched, event);
}

void ComponentSearchBox::updateCompletionModel() const
{
  const QString query = le->text().trimmed();

  QStringList strings;
  strings.reserve(static_cast<qsizetype>(completionList.size()));
  for (const auto& item : completionList) {
    strings.append(QString::fromStdString(item));
  }

  const auto ranked = SILICON::ui::componentSearchMatcher::rank(strings, query, true);

  QStringList sortedStrings;
  sortedStrings.reserve(static_cast<qsizetype>(ranked.size()));
  for (const auto& match : ranked) {
    sortedStrings.append(strings[match.index]);
  }
  completionModel->setStringList(sortedStrings);
}

std::string ComponentSearchBox::selectedTypeName() const
{
  return typeNameForCompletion(activeCompletionText());
}

QString ComponentSearchBox::activeCompletionText() const
{
  if (completionModel->rowCount() == 0)
    return {};

  const auto* popup = completer->popup();
  if (popup && popup->isVisible() && popup->currentIndex().isValid())
    return popup->currentIndex().data().toString();

  return completionModel->index(0, 0).data().toString();
}

std::string ComponentSearchBox::typeNameForCompletion(QString completion) const
{
  if (completion.isEmpty())
    return {};

  auto it = std::ranges::find_if(completionList, [&](const std::string& name) {
    return QString::fromStdString(name).compare(completion, Qt::CaseInsensitive) == 0;
  });

  if (it == completionList.end())
    return {};

  return *it;
}

void ComponentSearchBox::acceptCompletion(QString completion)
{
  const auto typeName = typeNameForCompletion(std::move(completion));
  if (typeName.empty()) {
    emit requestHide();
    return;
  }

  emit selectedComponent(typeName, scenePos());
}

}  // namespace ui
}  // namespace SILICON
