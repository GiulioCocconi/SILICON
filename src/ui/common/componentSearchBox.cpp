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

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <ui/common/theme.hpp>

namespace {

int editDistance(QString lhs, QString rhs)
{
  lhs = lhs.toCaseFolded();
  rhs = rhs.toCaseFolded();

  std::vector<int> previous(rhs.size() + 1);
  std::vector<int> current(rhs.size() + 1);

  for (qsizetype i = 0; i <= rhs.size(); ++i) {
    previous[i] = static_cast<int>(i);
  }

  for (qsizetype i = 1; i <= lhs.size(); ++i) {
    current[0] = static_cast<int>(i);

    for (qsizetype j = 1; j <= rhs.size(); ++j) {
      const int substitutionCost = lhs[i - 1] == rhs[j - 1] ? 0 : 1;
      current[j]                 = std::min(
          {previous[j] + 1, current[j - 1] + 1, previous[j - 1] + substitutionCost});
    }

    std::swap(previous, current);
  }

  return previous[rhs.size()];
}

}  // namespace

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

  struct RankedCompletion {
    QString text;
    int     rank;
    int     distance;
  };

  std::vector<RankedCompletion> ranked;
  ranked.reserve(completionList.size());

  for (const auto& item : completionList) {
    const QString text     = QString::fromStdString(item);
    int           rank     = 3;
    int           distance = 0;

    if (!query.isEmpty()) {
      if (text.compare(query, Qt::CaseInsensitive) == 0)
        rank = 0;
      else if (text.startsWith(query, Qt::CaseInsensitive))
        rank = 1;
      else if (text.contains(query, Qt::CaseInsensitive))
        rank = 2;
      else
        rank = 3;

      distance = editDistance(query, text);
    }

    ranked.push_back({text, rank, distance});
  }

  std::ranges::sort(ranked, [](const RankedCompletion& lhs, const RankedCompletion& rhs) {
    if (lhs.rank != rhs.rank)
      return lhs.rank < rhs.rank;

    if (lhs.distance != rhs.distance)
      return lhs.distance < rhs.distance;

    if (lhs.text.size() != rhs.text.size())
      return lhs.text.size() < rhs.text.size();

    return QString::compare(lhs.text, rhs.text, Qt::CaseInsensitive) < 0;
  });

  QStringList strings;
  for (const auto& item : ranked) {
    strings.append(item.text);
  }
  completionModel->setStringList(strings);
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
