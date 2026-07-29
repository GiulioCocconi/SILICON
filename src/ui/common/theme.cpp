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

#include <QFile>
#include <QPalette>
#include <QStyle>
#include <QWidget>


namespace SILICON {
namespace ui {

namespace {

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

  palette.setColor(QPalette::Window, background);
  palette.setColor(QPalette::WindowText, ink);
  palette.setColor(QPalette::Base, surface);
  palette.setColor(QPalette::AlternateBase, raised);
  palette.setColor(QPalette::Text, ink);
  palette.setColor(QPalette::Button, surface);
  palette.setColor(QPalette::ButtonText, ink);
  palette.setColor(QPalette::BrightText, color(tokens, "SILICON_ORANGE"));
  palette.setColor(QPalette::Highlight, blue);
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

SILICON::ui::theme::ColorMap ThemeEngine::currentMap;

void ThemeEngine::apply(QApplication& app, SILICON::ui::theme::Mode mode)
{
  currentMap =
      (mode == SILICON::ui::theme::Mode::Dark) ? SILICON::ui::theme::dark() : SILICON::ui::theme::light();

  const QString base     = loadQssTemplate();
  const QString finalQss = injectTokens(base, currentMap);
  app.setPalette(paletteFromTokens(currentMap));
  app.setStyleSheet(finalQss);

  /* CRITICAL: repolish everything */
  for (QWidget* w : QApplication::allWidgets()) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
  }
}

const QColor& ThemeEngine::getColor(const QString& key)
{
  return currentMap[key];
}

}  // namespace ui
}  // namespace SILICON
