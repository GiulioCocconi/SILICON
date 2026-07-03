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

#include "fileDialogUtils.hpp"

#include <utility>

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QObject>
#include <QWidget>

namespace SiliconFileDialog {

void openFileContent(QWidget* parent, const QString& caption, const QString& filter,
                     OpenContentCallback callback)
{
#ifdef __EMSCRIPTEN__
  QFileDialog::getOpenFileContent(
      filter, [callback = std::move(callback)](const QString&    fileName,
                                               const QByteArray& fileContent) {
        if (!fileName.isEmpty())
          callback(fileName, fileContent);
      });
#else
  const QString fileName =
      QFileDialog::getOpenFileName(parent, caption, QString(), filter);
  if (fileName.isEmpty())
    return;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(
        parent, caption,
        QObject::tr("Cannot open file for reading:\n%1").arg(file.errorString()));
    return;
  }

  callback(fileName, file.readAll());
#endif
}

std::optional<QString> saveFileContent(QWidget* parent, const QString& caption,
                                       const QString& suggestedFileName,
                                       const QString& filter, const QByteArray& content)
{
#ifdef __EMSCRIPTEN__
  QFileDialog::saveFileContent(content, suggestedFileName);
  return suggestedFileName;
#else
  const QString fileName =
      QFileDialog::getSaveFileName(parent, caption, suggestedFileName, filter);
  if (fileName.isEmpty())
    return std::nullopt;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    QMessageBox::warning(parent, caption,
                         QObject::tr("Cannot write file:\n%1").arg(file.errorString()));
    return std::nullopt;
  }

  return fileName;
#endif
}

}  // namespace SiliconFileDialog
