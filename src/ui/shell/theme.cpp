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

#include <stdexcept>

#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QWidget>

namespace SILICON::ui::theme {
namespace {
  // Array indices follow ColorToken; JSON and SVG annotations use these names.
  constexpr std::array names{u"SILICON_ORANGE",     u"SILICON_LORANGE",
                             u"SILICON_BLUE",       u"SILICON_GREEN",
                             u"SILICON_VIOLET",     u"SILICON_INTERNAL",
                             u"SILICON_INK",        u"SILICON_BACKGROUND",
                             u"SILICON_SURFACE",    u"SILICON_SURFACE_RAISED",
                             u"SILICON_GRID",       u"SILICON_GREY",
                             u"SILICON_DISABLED",   u"SILICON_SELECTED",
                             u"SILICON_HOVER",      u"SILICON_MENU_HOVER",
                             u"SILICON_ERROR",      u"SILICON_ERROR_HOVER",
                             u"SILICON_ERROR_FILL", u"SILICON_SIGNAL_LOW",
                             u"SILICON_ON_ACCENT",  u"SILICON_ON_VIOLET"};
  static_assert(names.size() == static_cast<std::size_t>(ColorToken::Count));

  const std::array<Theme, 2>& themes()
  {
    // Validate both immutable palettes once, before either becomes available.
    static const auto result = [] {
      QFile file(":/palette.json");
      if (!file.open(QFile::ReadOnly))
        throw std::runtime_error("Cannot read shared palette");
      QJsonParseError error;
      const auto      doc = QJsonDocument::fromJson(file.readAll(), &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()
          || doc.object().keys() != QStringList({"dark", "light"}))
        throw std::runtime_error("Shared palette must contain exactly light and dark");
      return std::array{
          Theme{.colors = ColorTokens::fromJson(doc.object().value("light").toObject())},
          Theme{.colors = ColorTokens::fromJson(doc.object().value("dark").toObject())}};
    }();
    return result;
  }
}  // namespace

QStringView tokenName(ColorToken token)
{
  return names.at(static_cast<std::size_t>(token));
}

std::optional<ColorToken> tokenFromName(QStringView name)
{
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (name == QStringView(names[i]))
      return static_cast<ColorToken>(i);
  }
  return std::nullopt;
}

ColorTokens ColorTokens::fromJson(const QJsonObject& object)
{
  if (object.size() != static_cast<qsizetype>(names.size()))
    throw std::runtime_error("Missing or unknown shared palette token");
  const QRegularExpression hex("^#[0-9a-fA-F]{6}$");
  ColorTokens              result;
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto name  = QStringView(names[i]).toString();
    const auto value = object.value(name);
    if (!value.isString() || !hex.match(value.toString()).hasMatch())
      throw std::runtime_error("Missing or invalid shared palette token: "
                               + name.toStdString());
    result.values[i] = QColor(value.toString());
  }
  return result;
}

const QColor& ColorTokens::operator[](ColorToken token) const
{
  return values.at(static_cast<std::size_t>(token));
}

QPalette Theme::palette() const
{
  QPalette   p;
  const auto set = [&](QPalette::ColorRole role, ColorToken token) {
    p.setColor(QPalette::All, role, colors[token]);
  };
  set(QPalette::Window, ColorToken::Background);
  set(QPalette::WindowText, ColorToken::Ink);
  set(QPalette::Base, ColorToken::Surface);
  set(QPalette::AlternateBase, ColorToken::SurfaceRaised);
  set(QPalette::Text, ColorToken::Ink);
  set(QPalette::Button, ColorToken::Surface);
  set(QPalette::ButtonText, ColorToken::Ink);
  set(QPalette::BrightText, ColorToken::Orange);
  set(QPalette::Highlight, ColorToken::Selected);
  set(QPalette::HighlightedText, ColorToken::Ink);
  set(QPalette::Link, ColorToken::Blue);
  set(QPalette::LinkVisited, ColorToken::Violet);
  set(QPalette::PlaceholderText, ColorToken::Grey);
  set(QPalette::Light, ColorToken::SurfaceRaised);
  set(QPalette::Midlight, ColorToken::SurfaceRaised);
  set(QPalette::Mid, ColorToken::Grid);
  set(QPalette::Dark, ColorToken::Grey);
  set(QPalette::Shadow, ColorToken::Ink);
  set(QPalette::ToolTipBase, ColorToken::SurfaceRaised);
  set(QPalette::ToolTipText, ColorToken::Ink);
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                    QPalette::HighlightedText})
    p.setColor(QPalette::Disabled, role, colors[ColorToken::Disabled]);
  return p;
}

const Theme& light()
{
  return themes()[0];
}
const Theme& dark()
{
  return themes()[1];
}
}  // namespace SILICON::ui::theme

namespace SILICON::ui {

ThemeEngine::ThemeEngine(QObject* parent) : QObject(parent) {}

ThemeEngine& ThemeEngine::instance()
{
  Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
  static QPointer<ThemeEngine> engine;
  if (!engine)
    engine = new ThemeEngine(qApp);
  return *engine;
}

const QColor& ThemeEngine::getColor(const QString& key)
{
  const auto token = theme::tokenFromName(key);
  if (!token)
    throw std::invalid_argument("Unknown theme token: " + key.toStdString());
  return getColor(*token);
}

const theme::Theme& ThemeEngine::current()
{
  return mode() == theme::Mode::Dark ? theme::dark() : theme::light();
}

void ThemeEngine::apply(QApplication& app, theme::Mode mode)
{
  auto&      engine = instance();
  const auto palette =
      (mode == theme::Mode::Dark ? theme::dark() : theme::light()).palette();
  if (engine.currentMode == mode && app.palette() == palette)
    return;
  engine.currentMode = mode;
  app.setPalette(palette);
  // Deliver palette changes before observers refresh their palette-backed views.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::ApplicationPaletteChange);
  emit engine.themeChanged();
  for (auto* window : QApplication::topLevelWidgets())
    window->update();
}

}  // namespace SILICON::ui
