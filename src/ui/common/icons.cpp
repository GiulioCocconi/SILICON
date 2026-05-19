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

#include <utility>

#include <QIconEngine>

#include <ui/common/theme.hpp>

namespace {

QColor themedIconColor()
{
  const QColor color = ThemeEngine::getColor("SILICON_INK");
  return color.isValid() ? color : QColor(Qt::black);
}

QColor disabledIconColor()
{
  const QColor color = ThemeEngine::getColor("SILICON_DISABLED");
  return color.isValid() ? color : themedIconColor();
}

class SvgIconEngine : public QIconEngine {
public:
  SvgIconEngine(QString path, bool themed, QColor color, std::vector<QSize> targetSizes)
    : path(std::move(path)),
      themed(themed),
      color(std::move(color)),
      targetSizes(std::move(targetSizes))
  {
  }

  SvgIconEngine* clone() const override { return new SvgIconEngine(*this); }

  void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
             QIcon::State state) override
  {
    painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
  }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
  {
    Q_UNUSED(state)

    QSvgRenderer renderer{};
    if (!QFile::exists(path)) {
      qWarning() << "SVG file does not exist:" << path;
      return {};
    }

    if (!renderer.load(path)) {
      qWarning() << "Failed to load SVG file or invalid SVG format:" << path;
      return {};
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter, pixmap.rect());

    const QColor iconColor = colorForMode(mode);
    if (iconColor.isValid()) {
      painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      painter.fillRect(pixmap.rect(), iconColor);
    }

    painter.end();
    return pixmap;
  }

  QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
  {
    Q_UNUSED(mode)
    Q_UNUSED(state)

    if (size.isValid())
      return size;

    return targetSizes.empty() ? QSize(32, 32) : targetSizes.front();
  }

private:
  QString            path;
  bool               themed;
  QColor             color;
  std::vector<QSize> targetSizes;

  QColor colorForMode(QIcon::Mode mode) const
  {
    if (mode == QIcon::Disabled)
      return disabledIconColor();

    if (color.isValid())
      return color;

    return themed ? themedIconColor() : QColor();
  }
};

}  // namespace

Icon::Icon(const QString& commonName, const std::vector<QSize>& targetSizes)
  : QIcon(new SvgIconEngine(Icon::getPathFromCommonName(commonName),
                            commonName != "silicon", QColor(), targetSizes))
{
}

Icon::Icon(const QString& commonName, const QColor& color,
           const std::vector<QSize>& targetSizes)
  : QIcon(new SvgIconEngine(Icon::getPathFromCommonName(commonName), color.isValid(),
                            color, targetSizes))
{
}

QString Icon::getPathFromCommonName(const QString& commonName)
{
  auto it = getCommonToLucideMap().find(commonName);

  if (it == getCommonToLucideMap().end())
    return "NOT_FOUND";

  auto path = QString(":/icons/%1.svg").arg(it.value());
  return path;
}
