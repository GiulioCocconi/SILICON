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

  /** Loads the embedded, shared palette; cached for the application lifetime. */
  ColorMap light();
  ColorMap dark();

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
