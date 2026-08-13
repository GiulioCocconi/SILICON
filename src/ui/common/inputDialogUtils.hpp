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

#include <QString>
#include <QStringList>

class QGraphicsItem;
class QWidget;


namespace SILICON {
namespace ui {

namespace inputDialog {

using TextCallback     = std::function<void(const QString&)>;
using AcceptedCallback = std::function<void()>;

enum class Choice { Primary, Secondary, Cancel };
using ChoiceCallback = std::function<void(Choice)>;

QWidget* parentWidgetForGraphicsItem(const QGraphicsItem* item);

void getText(QWidget* parent, const QString& title, const QString& label,
             const QString& defaultText, TextCallback callback);

void getItem(QWidget* parent, const QString& title, const QString& label,
             const QStringList& items, int current, bool editable, TextCallback callback);

void warning(QWidget* parent, const QString& title, const QString& text);

void critical(QWidget* parent, const QString& title, const QString& text);

void question(QWidget* parent, const QString& title, const QString& text,
              AcceptedCallback accepted);

/** @brief Shows a three-way warning using caller-provided action labels. */
void warningChoice(QWidget* parent, const QString& title, const QString& text,
                   const QString& primaryText, const QString& secondaryText,
                   ChoiceCallback callback);

}  // namespace inputDialog

}  // namespace ui
}  // namespace SILICON
