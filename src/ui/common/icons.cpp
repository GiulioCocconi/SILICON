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

#include "icons.hpp"

Icon::Icon(const QString& commonName, const std::vector<QSize>& targetSizes)
  : Icon(commonName, QColor(), targetSizes)
{
}

Icon::Icon(const QString& commonName, const QColor& color,
           const std::vector<QSize>& targetSizes)
{
  QSvgRenderer renderer{};

  const QString path = Icon::getPathFromCommonName(commonName);

  if (!QFile::exists(path)) {
    qWarning() << "SVG file does not exist:" << path;
    return;
  }

  if (!renderer.load(path)) {
    qWarning() << "Failed to load SVG file or invalid SVG format:" << path;
    return;
  }

  for (auto ts : targetSizes) {
    QPixmap pixmap(ts);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter, pixmap.rect());

    if (color.isValid()) {
      painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      painter.fillRect(pixmap.rect(), color);
    }

    painter.end();

    addPixmap(pixmap);
  }
}

QString Icon::getPathFromCommonName(const QString& commonName)
{
  auto it = getCommonToLucideMap().find(commonName);

  if (it == getCommonToLucideMap().end())
    return "NOT_FOUND";

  auto path = QString(":/icons/%1.svg").arg(it.value());
  return path;
}