#pragma once
#include <QApplication>
#include <QColor>
#include <QMap>
#include <QString>


namespace SILICON {
namespace ui {

namespace theme {

enum class Mode { Light, Dark };

using ColorMap = QMap<QString, QColor>;

/* ---------- LIGHT ---------- */

inline ColorMap light()
{
  return {{"SILICON_ORANGE", {"#ff9845"}},         {"SILICON_LORANGE", {"#ffc999"}},
          {"SILICON_BLUE", {"#46a6ef"}},           {"SILICON_GREEN", {"#89f17f"}},
          {"SILICON_VIOLET", {"#ce35a8"}},         {"SILICON_MAGENTA", {"#ce35a8"}},
          {"SILICON_INTERNAL", {"#ffe6d5"}},       {"SILICON_INK", {"#2a2730"}},
          {"SILICON_BACKGROUND", {"#fdfdfd"}},     {"SILICON_SURFACE", {"#ffffff"}},
          {"SILICON_SURFACE_RAISED", {"#ffffff"}}, {"SILICON_GRID", {"#d8d4dd"}},
          {"SILICON_DISABLED", {"#a0a0a0"}}};
}

/* ---------- DARK ---------- */

inline ColorMap dark()
{
  return {{"SILICON_ORANGE", {"#ffad66"}},         {"SILICON_LORANGE", {"#9d6740"}},
          {"SILICON_BLUE", {"#69b9ff"}},           {"SILICON_GREEN", {"#8ee889"}},
          {"SILICON_VIOLET", {"#e36ac8"}},         {"SILICON_MAGENTA", {"#e36ac8"}},
          {"SILICON_INTERNAL", {"#3c3130"}},       {"SILICON_INK", {"#f3eef8"}},
          {"SILICON_BACKGROUND", {"#17161d"}},     {"SILICON_SURFACE", {"#24212b"}},
          {"SILICON_SURFACE_RAISED", {"#2e2a36"}}, {"SILICON_GRID", {"#383340"}},
          {"SILICON_DISABLED", {"#857c8e"}}};
}

}  // namespace theme

class ThemeEngine {
public:
  static void          apply(QApplication& app, SILICON::ui::theme::Mode mode);
  static const QColor& getColor(const QString& key);

private:
  static QString loadQssTemplate();
  static QString injectTokens(const QString& qss, const SILICON::ui::theme::ColorMap& tokens);
  static SILICON::ui::theme::ColorMap currentMap;
};

}  // namespace ui
}  // namespace SILICON
