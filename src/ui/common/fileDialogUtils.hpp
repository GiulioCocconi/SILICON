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
#include <optional>

#include <QByteArray>
#include <QString>

class QWidget;

namespace SiliconFileDialog {

using OpenContentCallback = std::function<void(const QString&, const QByteArray&)>;

void openFileContent(QWidget* parent, const QString& caption, const QString& filter,
                     OpenContentCallback callback);

std::optional<QString> saveFileContent(QWidget* parent, const QString& caption,
                                       const QString& suggestedFileName,
                                       const QString& filter, const QByteArray& content);

}  // namespace SiliconFileDialog
