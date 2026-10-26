/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#include "themedSvgItem.hpp"

#include <utility>

#include "theme.hpp"

namespace SILICON::ui {

ThemedSvgItem::ThemedSvgItem(QString path, QGraphicsItem* parent)
  : QGraphicsSvgItem(parent), path(std::move(path))
{
  refreshTheme();
  callbacks.connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged,
                    [this] { refreshTheme(); });
}

void ThemedSvgItem::refreshTheme()
{
  if (auto* themedRenderer = ThemeEngine::svgRenderer(path)) {
    setSharedRenderer(themedRenderer);
    update();
  }
}

}  // namespace SILICON::ui
