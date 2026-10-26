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

#include "theme.hpp"

#include <algorithm>
#include <stdexcept>

#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QPointer>
#include <QStyle>
#include <QSvgRenderer>
#include <QWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

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

  void resolveSvgAnnotations(QXmlStreamAttributes&  attributes,
                             const theme::ColorMap& tokens)
  {
    for (const QString& property : {QStringLiteral("fill"), QStringLiteral("stroke")}) {
      const QString annotation = "data-silicon-" + property;
      if (!attributes.hasAttribute(annotation))
        continue;

      const QString token = attributes.value(annotation).toString();
      const auto    color = tokens.constFind(token);
      if (color == tokens.cend() || !color.value().isValid()) {
        qWarning() << "Unknown SVG theme token:" << token;
        continue;
      }

      // Inline declarations take precedence over presentation attributes in SVG.
      QStringList declarations   = attributes.value("style").toString().split(';');
      bool        inlineProperty = false;
      for (QString& declaration : declarations) {
        const auto colon = declaration.indexOf(':');
        if (colon >= 0 && declaration.left(colon).trimmed() == property) {
          declaration    = declaration.left(colon + 1) + color.value().name();
          inlineProperty = true;
        }
      }
      if (inlineProperty)
        setAttribute(attributes, "style", declarations.join(';'));
      else
        setAttribute(attributes, property, color.value().name());
    }
  }

QColor color(const SILICON::ui::theme::ColorMap& tokens, const QString& key)
{
  return tokens.value(key);
}

QPalette paletteFromTokens(const SILICON::ui::theme::ColorMap& tokens)
{
  QPalette palette;

  const QColor background = color(tokens, "SILICON_BACKGROUND");
  const QColor surface    = color(tokens, "SILICON_SURFACE");
  const QColor raised     = color(tokens, "SILICON_SURFACE_RAISED");
  const QColor ink        = color(tokens, "SILICON_INK");
  const QColor disabled   = color(tokens, "SILICON_DISABLED");
  const QColor blue       = color(tokens, "SILICON_BLUE");
  const QColor selected   = color(tokens, "SILICON_SELECTED");

  palette.setColor(QPalette::Window, background);
  palette.setColor(QPalette::WindowText, ink);
  palette.setColor(QPalette::Base, surface);
  palette.setColor(QPalette::AlternateBase, raised);
  palette.setColor(QPalette::Text, ink);
  palette.setColor(QPalette::Button, surface);
  palette.setColor(QPalette::ButtonText, ink);
  palette.setColor(QPalette::BrightText, color(tokens, "SILICON_ORANGE"));
  palette.setColor(QPalette::Highlight, selected);
  palette.setColor(QPalette::HighlightedText, ink);
  palette.setColor(QPalette::Link, blue);
  palette.setColor(QPalette::ToolTipBase, raised);
  palette.setColor(QPalette::ToolTipText, ink);

  palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
  palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);

  return palette;
}

}  // namespace

QString ThemeEngine::loadQssTemplate()
{
  QFile file(":/theme.qss");

  if (!file.open(QFile::ReadOnly))
    throw std::runtime_error("Cannot read theme file!");

  return file.readAll();
}

QString ThemeEngine::injectTokens(const QString&                qss,
                                  const SILICON::ui::theme::ColorMap& tokens)
{
  QString out = qss;

  QList<QString> keys = tokens.keys();
  std::sort(keys.begin(), keys.end(), [](const QString& lhs, const QString& rhs) {
    return lhs.size() > rhs.size();
  });

  for (const QString& key : keys) {
    out.replace(key, tokens.value(key).name());
  }
  return out;
}

ThemeEngine& ThemeEngine::instance()
{
  Q_ASSERT(qApp);
  static QPointer<ThemeEngine> engine;
  if (!engine)
    engine = new ThemeEngine(qApp);
  return *engine;
}

QByteArray ThemeEngine::preprocessSvg(const QByteArray& svg)
{
  QXmlStreamReader reader(svg);
  QByteArray       result;
  QXmlStreamWriter writer(&result);
  const auto&      tokens = instance().currentMap;

  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      auto attributes = reader.attributes();
      resolveSvgAnnotations(attributes, tokens);
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

QSvgRenderer* ThemeEngine::svgRenderer(const QString& path)
{
  auto&      engine = instance();
  const auto key    = std::make_pair(path, engine.currentMode);
  if (const auto it = engine.svgCache.constFind(key); it != engine.svgCache.cend())
    return it.value();

  QFile file(path);
  if (!file.open(QFile::ReadOnly)) {
    qWarning() << "Cannot read SVG:" << path << file.errorString();
    return nullptr;
  }
  const QByteArray svg = preprocessSvg(file.readAll());
  if (svg.isEmpty())
    return nullptr;

  auto* renderer = new QSvgRenderer(&engine);
  if (!renderer->load(svg)) {
    qWarning() << "Cannot load SVG:" << path;
    delete renderer;
    return nullptr;
  }
  engine.svgCache.insert(key, renderer);
  return renderer;
}

void ThemeEngine::apply(QApplication& app, SILICON::ui::theme::Mode mode)
{
  auto& engine       = instance();
  auto& currentMap   = engine.currentMap;
  engine.currentMode = mode;
  currentMap =
      (mode == SILICON::ui::theme::Mode::Dark) ? SILICON::ui::theme::dark() : SILICON::ui::theme::light();

  const QString base     = loadQssTemplate();
  const QString finalQss = injectTokens(base, currentMap);

  // Remove palette overrides installed while polishing the previous stylesheet before
  // changing the application palette. This lets existing widgets inherit the new palette
  // instead of retaining resolved colors from the previous theme.
  app.setStyleSheet({});
  app.setPalette(paletteFromTokens(currentMap));
  app.setStyleSheet(finalQss);

  /* CRITICAL: repolish everything */
  for (QWidget* w : QApplication::allWidgets()) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
  }
  emit engine.themeChanged();
}

const QColor& ThemeEngine::getColor(const QString& key)
{
  return instance().currentMap[key];
}

}  // namespace SILICON::ui
