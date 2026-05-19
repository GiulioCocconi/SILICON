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
  return {{"SILICON_ORANGE", {"#ffad66"}}, {"SILICON_LORANGE", {"#7a4b2c"}},
          {"SILICON_BLUE", {"#67b7ff"}},   {"SILICON_GREEN", {"#91e58c"}},
          {"SILICON_VIOLET", {"#df62c2"}}, {"SILICON_INTERNAL", {"#3a2f2b"}},
          {"SILICON_INK", {"#f1edf6"}},    {"SILICON_BACKGROUND", {"#17161b"}}};
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
