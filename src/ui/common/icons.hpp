/*
  Copyright (C) 2025 Giulio Cocconi

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

#include <QColor>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>

#include <QDebug>

#include <vector>


namespace SILICON {
namespace ui {

class Icon : public QIcon {
public:
  explicit Icon(const QString&            commonName,
                const std::vector<QSize>& targetSizes = {QSize(32, 32)});

  explicit Icon(const QString& commonName, const QColor& color,
                const std::vector<QSize>& targetSizes = {QSize(32, 32)});

private:
  static const QHash<QString, QString>& getCommonToLucideMap()
  {
    static const QHash<QString, QString> commonToLucideMap{
        {"silicon", "silicon-icon"},
        {"mouse-pointer", "mouse-pointer-2"},
        {"pan", "move"},
        {"chart", "file-chart-line"},
        {"check", "circle-check"},
        {"info", "info"},
        {"xmark-circle", "circle-alert"},
        {"copy", "copy"},
        {"explosion", "zap"},
        {"export", "file-output"},
        {"file", "file"},
        {"save", "save"},
        {"open", "folder-open"},
        {"link", "link"},
        {"paste", "file-input"},
        {"pencil", "pencil"},
        {"plug-error", "circle-alert"},
        {"play", "play"},
        {"plug", "plug"},
        {"plus", "circle-plus"},
        {"minus", "circle-minus"},
        {"print", "printer"},
        {"undo", "undo-2"},
        {"redo", "redo-2"},
        {"rotate", "rotate-cw"},
        {"cut", "scissors"},
        {"delete", "trash"},
        {"xmark", "x"},
        {"binary", "binary"},
        {"box", "box"},
        {"circle-power", "circle-power"},
        {"circuit-board", "circuit-board"},
        {"clock", "clock"},
        {"code", "code"},
        {"container", "container"},
        {"cpu", "cpu"},
        {"file-cog", "file-cog"},
        {"funnel", "funnel"},
        {"hard-drive", "hard-drive"},
        {"memory-stick", "memory-stick"},
        {"puzzle", "puzzle"},
        {"settings", "settings"},
        {"sticky-note", "sticky-note"},
        {"rearrange", "layout-freeform"},
        {"text-cursor-input", "text-cursor-input"},
        {"toy-brick", "toy-brick"},
        {"usb", "usb"}};
    return commonToLucideMap;
  }

  static QString getPathFromCommonName(const QString& commonName);
};

}  // namespace ui
}  // namespace SILICON
