#pragma once
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QString>
#include <utility>

class QSvgRenderer;

namespace SILICON::ui {

namespace theme {

  enum class Mode { Light, Dark };

  using ColorMap = QMap<QString, QColor>;

  /* ---------- LIGHT ---------- */

  inline ColorMap light()
  {
    return {{"SILICON_ORANGE", {"#ea6d29"}},   {"SILICON_LORANGE", {"#f4a261"}},
            {"SILICON_BLUE", {"#2587d6"}},     {"SILICON_GREEN", {"#1da15a"}},
            {"SILICON_VIOLET", {"#d43a96"}},   {"SILICON_INTERNAL", {"#ffe2cd"}},
            {"SILICON_INK", {"#292c31"}},      {"SILICON_BACKGROUND", {"#f7f7f6"}},
            {"SILICON_SURFACE", {"#ffffff"}},  {"SILICON_SURFACE_RAISED", {"#eceef1"}},
            {"SILICON_GRID", {"#d8dce0"}},     {"SILICON_GREY", {"#68717c"}},
            {"SILICON_DISABLED", {"#969da5"}}, {"SILICON_SELECTED", {"#b7ddfa"}},
            {"SILICON_HOVER", {"#bcebc9"}},    {"SILICON_MENU_HOVER", {"#ffd1a8"}}};
  }

  /* ---------- DARK ---------- */

  inline ColorMap dark()
  {
    return {{"SILICON_ORANGE", {"#e8893f"}},   {"SILICON_LORANGE", {"#a87550"}},
            {"SILICON_BLUE", {"#5599d1"}},     {"SILICON_GREEN", {"#4bac76"}},
            {"SILICON_VIOLET", {"#ce70a7"}},   {"SILICON_INTERNAL", {"#40343a"}},
            {"SILICON_INK", {"#eeeef1"}},      {"SILICON_BACKGROUND", {"#1c1e23"}},
            {"SILICON_SURFACE", {"#272a30"}},  {"SILICON_SURFACE_RAISED", {"#32363e"}},
            {"SILICON_GRID", {"#3a3d45"}},     {"SILICON_GREY", {"#a2a5ad"}},
            {"SILICON_DISABLED", {"#8c9099"}}, {"SILICON_SELECTED", {"#3e5f7c"}},
            {"SILICON_HOVER", {"#304d3c"}},    {"SILICON_MENU_HOVER", {"#59412e"}}};
  }

}  // namespace theme

class ThemeEngine : public QObject {
  Q_OBJECT
public:
  static ThemeEngine&  instance();
  static void          apply(QApplication& app, theme::Mode mode);
  static const QColor& getColor(const QString& key);

  /** Resolves SVG theme annotations without changing the source resource. */
  static QByteArray preprocessSvg(const QByteArray& svg);
  /** Borrows an application-owned renderer cached by resource path and theme. */
  static QSvgRenderer* svgRenderer(const QString& path);

signals:
  void themeChanged();

private:
  explicit ThemeEngine(QObject* parent) : QObject(parent) {}
  static QString  loadQssTemplate();
  static QString  injectTokens(const QString& qss, const theme::ColorMap& tokens);
  theme::ColorMap currentMap  = theme::light();
  theme::Mode     currentMode = theme::Mode::Light;
  QMap<std::pair<QString, theme::Mode>, QSvgRenderer*> svgCache;
};

}  // namespace SILICON::ui
