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

#include <memory>

#include <QByteArray>
#include <QColor>
#include <QIcon>

class QSvgRenderer;

namespace SILICON::ui {

namespace svg {
  // GUI thread only. Shared ownership keeps renderers alive across cache eviction.
  // Resolve data-silicon-fill/stroke annotations; invalid XML/tokens return empty.
  QByteArray                    preprocess(const QByteArray& source);
  std::shared_ptr<QSvgRenderer> renderer(const QString& path);
}  // namespace svg

// Rasterize lazily for the current theme and device scale. The application logo
// retains its source colors; other icons default to Ink unless explicitly colored.
class Icon : public QIcon {
public:
  explicit Icon(const QString& commonName);
  explicit Icon(const QString& commonName, const QColor& color);
};

}  // namespace SILICON::ui
