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

#include <QFile>
#include <QStyle>
#include <QWidget>

QString ThemeEngine::loadQssTemplate()
{
  QFile file(":/theme.qss");

  if (!file.open(QFile::ReadOnly))
    throw std::runtime_error("Cannot read theme file!");

  return file.readAll();
}

QString ThemeEngine::injectTokens(const QString&                qss,
                                  const SiliconTheme::ColorMap& tokens)
{
  QString out = qss;

  for (auto it = tokens.begin(); it != tokens.end(); ++it) {
    out.replace(it.key(), it.value().name());
  }
  return out;
}

SiliconTheme::ColorMap ThemeEngine::currentMap;

void ThemeEngine::apply(QApplication& app, SiliconTheme::Mode mode)
{
  currentMap =
      (mode == SiliconTheme::Mode::Dark) ? SiliconTheme::dark() : SiliconTheme::light();

  const QString base     = loadQssTemplate();
  const QString finalQss = injectTokens(base, currentMap);
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