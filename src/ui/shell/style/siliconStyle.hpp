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

#include <QProxyStyle>

namespace SILICON::ui {

class SiliconStyle : public QProxyStyle {
  Q_OBJECT
public:
  explicit SiliconStyle(QObject* parent = nullptr);

  void       drawPrimitive(PrimitiveElement, const QStyleOption*, QPainter*,
                           const QWidget* = nullptr) const override;
  void       drawControl(ControlElement, const QStyleOption*, QPainter*,
                         const QWidget* = nullptr) const override;
  void       drawComplexControl(ComplexControl, const QStyleOptionComplex*, QPainter*,
                                const QWidget* = nullptr) const override;
  QRect      subElementRect(SubElement, const QStyleOption*,
                            const QWidget* = nullptr) const override;
  QRect      subControlRect(ComplexControl, const QStyleOptionComplex*, SubControl,
                            const QWidget* = nullptr) const override;
  SubControl hitTestComplexControl(ComplexControl, const QStyleOptionComplex*,
                                   const QPoint&,
                                   const QWidget* = nullptr) const override;
  QSize      sizeFromContents(ContentsType, const QStyleOption*, const QSize&,
                              const QWidget* = nullptr) const override;
  int        pixelMetric(PixelMetric, const QStyleOption* = nullptr,
                         const QWidget* = nullptr) const override;
  int        styleHint(StyleHint, const QStyleOption* = nullptr, const QWidget* = nullptr,
                       QStyleHintReturn* = nullptr) const override;
  QPalette   standardPalette() const override;
  QIcon      standardIcon(StandardPixmap, const QStyleOption* = nullptr,
                          const QWidget* = nullptr) const override;
  using QProxyStyle::polish;
  using QProxyStyle::unpolish;
  void polish(QWidget*) override;
  void unpolish(QWidget*) override;
};
}  // namespace SILICON::ui
