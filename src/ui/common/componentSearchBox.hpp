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

#pragma once

#include <ranges>

#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QGraphicsProxyWidget>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStringListModel>

#include <ui/common/enums.hpp>

class ComponentSearchBox : public QGraphicsProxyWidget {
  Q_OBJECT
public:
  explicit ComponentSearchBox(std::vector<std::string> completionList = {},
                              QString                  title  = "Insert component...",
                              QGraphicsItem*           parent = nullptr);
  void showCompleter() const;

  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

  void focus() { le->setFocus(Qt::OtherFocusReason); }
  void clear() { le->clear(); }

  void setCompletionList(std::vector<std::string> list)
  {
    this->completionList = std::move(list);
    updateCompletionModel();
  }

signals:

  void requestHide();
  void requestCancel();
  void selectedComponent(std::string typeName, QPointF pos);

private:
  void                      updateCompletionModel() const;
  [[nodiscard]] std::string selectedTypeName() const;
  [[nodiscard]] QString     activeCompletionText() const;
  [[nodiscard]] std::string typeNameForCompletion(QString completion) const;
  void                      acceptCompletion(QString completion);

  QLineEdit*         le;
  QCompleter*        completer;
  QStringListModel*  completionModel;
  QGraphicsTextItem* titleItem;

  std::vector<std::string> completionList;
};
