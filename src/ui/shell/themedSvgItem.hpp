/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#pragma once

#include <QGraphicsSvgItem>

#include <ui/lifetime.hpp>

namespace SILICON::ui {

/** A circuit SVG item backed by the current theme's cached renderer. */
class ThemedSvgItem : public QGraphicsSvgItem {
public:
  explicit ThemedSvgItem(QString path, QGraphicsItem* parent = nullptr);

private:
  void refreshTheme();

  QString       path;
  CallbackScope callbacks;
};

}  // namespace SILICON::ui
