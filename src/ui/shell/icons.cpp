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

#include "icons.hpp"
#include "theme.hpp"
#include "themedSvgItem.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QCache>
#include <QDebug>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QPointer>
#include <QSvgRenderer>
#include <QThread>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

namespace SILICON::ui {
namespace {
  void setAttribute(QXmlStreamAttributes& attributes, const QString& name,
                    const QString& value)
  {
    for (auto& attribute : attributes) {
      if (attribute.qualifiedName() == name) {
        attribute = QXmlStreamAttribute(name, value);
        return;
      }
    }
    attributes.append(name, value);
  }

  bool resolveAnnotations(QXmlStreamAttributes& attributes)
  {
    for (const QString& property : {QStringLiteral("fill"), QStringLiteral("stroke")}) {
      const QString annotation = "data-silicon-" + property;
      if (!attributes.hasAttribute(annotation))
        continue;
      const auto token = theme::tokenFromName(attributes.value(annotation));
      if (!token) {
        qWarning() << "Unknown SVG theme token:" << attributes.value(annotation);
        return false;
      }
      const auto color = ThemeEngine::getColor(*token).name();
      // Inline styles take precedence in SVG. Keep both representations coherent,
      // replacing all repeated declarations of an annotated property as well.
      setAttribute(attributes, property, color);
      auto declarations = attributes.value("style").toString().split(';');
      bool changed      = false;
      for (auto& declaration : declarations) {
        const auto colon = declaration.indexOf(':');
        if (colon >= 0
            && declaration.left(colon).trimmed().compare(property, Qt::CaseInsensitive)
                   == 0) {
          declaration = declaration.left(colon + 1) + color;
          changed     = true;
        }
      }
      if (changed)
        setAttribute(attributes, "style", declarations.join(';'));
    }
    return true;
  }

  QString iconPath(const QString& name)
  {
    static const QHash<QString, QString> aliases{{"silicon", "silicon-icon"},
                                                 {"mouse-pointer", "mouse-pointer-2"},
                                                 {"pan", "move"},
                                                 {"chart", "file-chart-line"},
                                                 {"check", "circle-check"},
                                                 {"xmark-circle", "circle-alert"},
                                                 {"explosion", "zap"},
                                                 {"import", "file-input"},
                                                 {"export", "file-output"},
                                                 {"open", "folder-open"},
                                                 {"paste", "clipboard-paste"},
                                                 {"plug-error", "circle-alert"},
                                                 {"plus", "circle-plus"},
                                                 {"minus", "circle-minus"},
                                                 {"print", "printer"},
                                                 {"undo", "undo-2"},
                                                 {"redo", "redo-2"},
                                                 {"rotate", "rotate-cw"},
                                                 {"cut", "scissors"},
                                                 {"delete", "trash"},
                                                 {"xmark", "x"},
                                                 {"build", "hammer"},
                                                 {"diagram", "layout-template"},
                                                 {"rearrange", "layout-freeform"}};
    return QStringLiteral(":/icons/%1.svg").arg(aliases.value(name, name));
  }

  // QApplication owns the cache; items retain shared renderers after eviction.
  class SvgCache : public QObject {
  public:
    using QObject::QObject;
    QCache<QString, std::shared_ptr<QSvgRenderer>> renderers{256};
  };

  class SvgIconEngine : public QIconEngine {
  public:
    SvgIconEngine(QString path, bool themed, QColor color = {})
      : path(std::move(path)), themed(themed), color(std::move(color))
    {
    }

    SvgIconEngine* clone() const override { return new SvgIconEngine(*this); }
    QString        key() const override { return QStringLiteral("silicon-svg"); }
    bool           isNull() override { return !svg::renderer(path); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override
    {
      if (!painter || rect.isEmpty())
        return;
      // deviceTransform includes DPR, rotation, shear and world/view scaling.
      // The largest singular value gives sufficient resolution even under shear.
      const auto  transform = painter->deviceTransform();
      const qreal trace =
          transform.m11() * transform.m11() + transform.m12() * transform.m12()
          + transform.m21() * transform.m21() + transform.m22() * transform.m22();
      const qreal determinant =
          transform.m11() * transform.m22() - transform.m12() * transform.m21();
      const qreal scale = std::sqrt(
          (trace
           + std::sqrt(std::max(qreal(0), trace * trace - 4 * determinant * determinant)))
          / 2);
      const auto pm = raster(rect.size(), std::max(scale, qreal(0.01)), mode);
      painter->drawPixmap(QRectF(rect), pm, QRectF(pm.rect()));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State) override
    {
      return raster(size, 1.0, mode);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State,
                         qreal scale) override
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
      // QIcon passed physical dimensions before Qt 6.8.
      return raster(QSizeF(size) / scale, scale, mode);
#else
      return raster(size, scale, mode);
#endif
    }

  private:
    QString path;
    bool    themed;
    QColor  color;

    QColor colorForMode(QIcon::Mode mode) const
    {
      if (mode == QIcon::Disabled)
        return ThemeEngine::getColor(theme::ColorToken::Disabled);
      if (color.isValid())
        return color;
      return themed ? ThemeEngine::getColor(theme::ColorToken::Ink) : QColor();
    }

    QPixmap raster(QSizeF logicalSize, qreal dpr, QIcon::Mode mode)
    {
      if (logicalSize.isEmpty() || !std::isfinite(dpr) || dpr <= 0)
        return {};
      const QSize pixels(qCeil(logicalSize.width() * dpr),
                         qCeil(logicalSize.height() * dpr));
      if (pixels.isEmpty())
        return {};
      const QColor effectiveColor = colorForMode(mode);
      // Length-prefix identity so even paths containing separators cannot collide.
      // Round-trip precision keeps fractional device pixel ratios distinct.
      const QString cacheKey =
          QStringLiteral("silicon-svg:%1:%2:%3x%4:%5:%6:%7")
              .arg(path.size())
              .arg(path)
              .arg(pixels.width())
              .arg(pixels.height())
              .arg(QString::number(dpr, 'g', 17))
              .arg(effectiveColor.isValid() ? effectiveColor.name(QColor::HexArgb)
                                            : "multicolor")
              .arg(int(ThemeEngine::mode()));
      QPixmap pm;
      if (QPixmapCache::find(cacheKey, &pm))
        return pm;
      const auto renderer = svg::renderer(path);
      if (!renderer)
        return {};
      pm = QPixmap(pixels);
      pm.fill(Qt::transparent);
      {
        QPainter painter(&pm);
        renderer->render(&painter, QRectF(QPointF(), QSizeF(pixels)));
        if (effectiveColor.isValid()) {
          painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
          painter.fillRect(pm.rect(), effectiveColor);
        }
      }
      pm.setDevicePixelRatio(dpr);
      // Qt bounds and evicts raster entries; both immutable themes can reuse their
      // entries.
      QPixmapCache::insert(cacheKey, pm);
      return pm;
    }
  };

}  // namespace

QByteArray svg::preprocess(const QByteArray& svg)
{
  QXmlStreamReader reader(svg);
  QByteArray       result;
  QXmlStreamWriter writer(&result);
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      auto attributes = reader.attributes();
      if (!resolveAnnotations(attributes))
        return {};
      writer.writeStartElement(reader.qualifiedName().toString());
      for (const auto& declaration : reader.namespaceDeclarations()) {
        if (declaration.prefix().isEmpty())
          writer.writeDefaultNamespace(declaration.namespaceUri().toString());
        else
          writer.writeNamespace(declaration.namespaceUri().toString(),
                                declaration.prefix().toString());
      }
      writer.writeAttributes(attributes);
    } else if (!reader.hasError()) {
      writer.writeCurrentToken(reader);
    }
  }
  if (reader.hasError()) {
    qWarning() << "Cannot preprocess SVG:" << reader.errorString();
    return {};
  }
  return result;
}

std::shared_ptr<QSvgRenderer> svg::renderer(const QString& path)
{
  Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
  static QPointer<SvgCache> cache;
  if (!cache)
    cache = new SvgCache(qApp);
  // Preprocessing bakes in palette colors, so each theme needs its own renderer.
  const QString key = QString::number(int(ThemeEngine::mode())) + ':' + path;
  if (const auto* cached = cache->renderers.object(key))
    return *cached;
  QFile file(path);
  if (!file.open(QFile::ReadOnly)) {
    qWarning() << "Cannot read SVG:" << path << file.errorString();
    return {};
  }
  const auto source = preprocess(file.readAll());
  if (source.isEmpty())
    return {};
  auto result = std::make_shared<QSvgRenderer>();
  result->setAspectRatioMode(Qt::KeepAspectRatio);
  if (!result->load(source)) {
    qWarning() << "Cannot load SVG:" << path;
    return {};
  }
  cache->renderers.insert(key, new std::shared_ptr<QSvgRenderer>(result));
  return result;
}

Icon::Icon(const QString& commonName)
  : QIcon(new SvgIconEngine(iconPath(commonName), commonName != "silicon"))
{
}

Icon::Icon(const QString& commonName, const QColor& color)
  : QIcon(new SvgIconEngine(iconPath(commonName), color.isValid(), color))
{
}

class ThemedSvgItem::RendererLease : public QObject {
public:
  using QObject::QObject;
  std::shared_ptr<QSvgRenderer> renderer;
};

ThemedSvgItem::ThemedSvgItem(QString path, QGraphicsItem* parent)
  : QGraphicsSvgItem(parent),
    path(std::move(path)),
    rendererLease(new RendererLease(this))
{
  refreshTheme();
  callbacks.connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged,
                    [this] { refreshTheme(); });
}

void ThemedSvgItem::refreshTheme()
{
  auto renderer = svg::renderer(path);
  if (!renderer)
    renderer = std::make_shared<QSvgRenderer>();
  // The item borrows the pointer. Replace it before releasing the previous lease.
  setSharedRenderer(renderer.get());
  rendererLease->renderer = std::move(renderer);
  update();
}

ThemedSvgItem::~ThemedSvgItem() = default;

}  // namespace SILICON::ui
