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

#include <array>
#include <optional>

#include <QApplication>
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QStringView>

namespace SILICON::ui::theme {

enum class Mode { Light, Dark };

// This is the palette's schema, not another palette. Values only live in palette.json.
enum class ColorToken {
  Orange,
  Lorange,
  Blue,
  Green,
  Violet,
  Internal,
  Ink,
  Background,
  Surface,
  SurfaceRaised,
  Grid,
  Grey,
  Disabled,
  Selected,
  Hover,
  MenuHover,
  Error,
  ErrorHover,
  ErrorFill,
  SignalLow,
  OnAccent,
  OnViolet,
  Count
};

QStringView               tokenName(ColorToken token);
std::optional<ColorToken> tokenFromName(QStringView name);

class ColorTokens {
public:
  static ColorTokens fromJson(const QJsonObject& object);
  const QColor&      operator[](ColorToken token) const;

private:
  std::array<QColor, static_cast<std::size_t>(ColorToken::Count)> values;
};

struct Theme {
  ColorTokens colors;
  QPalette    palette() const;
};

const Theme& light();
const Theme& dark();

}  // namespace SILICON::ui::theme

namespace SILICON::ui {

// GUI-thread service owned by QApplication. Colors reference immutable palettes;
// views that store colors or pixmaps must refresh them on themeChanged().
class ThemeEngine : public QObject {
  Q_OBJECT
public:
  static ThemeEngine&        instance();
  static void                apply(QApplication& app, theme::Mode mode);
  static const theme::Theme& current();
  static theme::Mode         mode() { return instance().currentMode; }
  static const QColor&       getColor(theme::ColorToken token)
  {
    return current().colors[token];
  }
  static const QColor& getColor(const QString& key);

signals:
  void themeChanged();

private:
  explicit ThemeEngine(QObject* parent);
  theme::Mode currentMode = theme::Mode::Light;
};

}  // namespace SILICON::ui
