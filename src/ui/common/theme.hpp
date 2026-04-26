#pragma once
#include <QApplication>
#include <QMap>
#include <QString>

namespace SiliconTheme {

enum class Mode { Light, Dark };

using ColorMap = QMap<QString, QColor>;

/* ---------- LIGHT ---------- */

inline ColorMap light()
{
  return {{"SILICON_ORANGE", {"#ff9845"}}, {"SILICON_LORANGE", {"#ffc999"}},
          {"SILICON_BLUE", {"#46a6ef"}},   {"SILICON_GREEN", {"#89f17f"}},
          {"SILICON_VIOLET", {"#ce35a8"}}, {"SILICON_INTERNAL", {"#ffe6d5"}},
          {"SILICON_INK", {"#2a2730"}},    {"SILICON_BACKGROUND", {"#fdfdfd"}}};
}

/* ---------- DARK ---------- */

inline ColorMap dark()
{
  // TODO!
  return light();
}

}  // namespace SiliconTheme

class ThemeEngine {
public:
  static void          apply(QApplication& app, SiliconTheme::Mode mode);
  static const QColor& getColor(const QString& key);

private:
  static QString loadQssTemplate();
  static QString injectTokens(const QString& qss, const SiliconTheme::ColorMap& tokens);
  static SiliconTheme::ColorMap currentMap;
};