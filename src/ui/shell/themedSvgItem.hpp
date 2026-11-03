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
  ~ThemedSvgItem() override;

private:
  void refreshTheme();

  QString path;
  class RendererLease;
  // QObject child teardown follows QGraphicsSvgItem teardown, preserving the lease
  // throughout the base item's lifetime as required by setSharedRenderer().
  RendererLease* rendererLease;
  CallbackScope  callbacks;
};

}  // namespace SILICON::ui
