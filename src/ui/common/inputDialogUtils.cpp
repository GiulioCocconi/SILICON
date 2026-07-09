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

#include "inputDialogUtils.hpp"

#include <utility>

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QWidget>

namespace SiliconInputDialog {

QWidget* parentWidgetForGraphicsItem(const QGraphicsItem* item)
{
  if (!item || !item->scene() || item->scene()->views().empty())
    return nullptr;

  return item->scene()->views().front();
}

void getText(QWidget* parent, const QString& title, const QString& label,
             const QString& defaultText, TextCallback callback)
{
#ifdef __EMSCRIPTEN__
  auto* dialog = new QInputDialog(parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(title);
  dialog->setLabelText(label);
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setTextEchoMode(QLineEdit::Normal);
  dialog->setTextValue(defaultText);

  QObject::connect(
      dialog, &QInputDialog::accepted, dialog,
      [dialog, callback = std::move(callback)] { callback(dialog->textValue()); });
  dialog->open();
#else
  bool          ok = false;
  const QString text =
      QInputDialog::getText(parent, title, label, QLineEdit::Normal, defaultText, &ok);
  if (ok)
    callback(text);
#endif
}

void getItem(QWidget* parent, const QString& title, const QString& label,
             const QStringList& items, int current, bool editable, TextCallback callback)
{
#ifdef __EMSCRIPTEN__
  auto* dialog = new QInputDialog(parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(title);
  dialog->setLabelText(label);
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setComboBoxItems(items);
  dialog->setComboBoxEditable(editable);
  if (current >= 0 && current < items.size())
    dialog->setTextValue(items[current]);

  QObject::connect(
      dialog, &QInputDialog::accepted, dialog,
      [dialog, callback = std::move(callback)] { callback(dialog->textValue()); });
  dialog->open();
#else
  bool          ok = false;
  const QString text =
      QInputDialog::getItem(parent, title, label, items, current, editable, &ok);
  if (ok)
    callback(text);
#endif
}

void warning(QWidget* parent, const QString& title, const QString& text)
{
#ifdef __EMSCRIPTEN__
  auto* messageBox =
      new QMessageBox(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
  messageBox->setAttribute(Qt::WA_DeleteOnClose);
  messageBox->open();
#else
  QMessageBox::warning(parent, title, text);
#endif
}

void critical(QWidget* parent, const QString& title, const QString& text)
{
#ifdef __EMSCRIPTEN__
  auto* messageBox =
      new QMessageBox(QMessageBox::Critical, title, text, QMessageBox::Ok, parent);
  messageBox->setAttribute(Qt::WA_DeleteOnClose);
  messageBox->open();
#else
  QMessageBox::critical(parent, title, text);
#endif
}

void question(QWidget* parent, const QString& title, const QString& text,
              AcceptedCallback accepted)
{
#ifdef __EMSCRIPTEN__
  auto* messageBox = new QMessageBox(QMessageBox::Question, title, text,
                                     QMessageBox::Yes | QMessageBox::No, parent);
  messageBox->setDefaultButton(QMessageBox::No);
  messageBox->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(messageBox, &QMessageBox::finished, messageBox,
                   [accepted = std::move(accepted)](const int result) {
                     if (result == QMessageBox::Yes)
                       accepted();
                   });
  messageBox->open();
#else
  if (QMessageBox::question(parent, title, text, QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No)
      == QMessageBox::Yes) {
    accepted();
  }
#endif
}

}  // namespace SiliconInputDialog
