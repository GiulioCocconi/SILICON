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

#include <string>

#include <QGraphicsRectItem>
#include <QPoint>
#include <QPointF>
#include <QSize>

#include <core/projectDocument.hpp>

class SubcircuitRectShape : public QGraphicsRectItem {
public:
  explicit SubcircuitRectShape(const QSize& size, QGraphicsItem* parent = nullptr);
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;
};

[[nodiscard]] int gridToPixels(int value);
[[nodiscard]] int pixelsToGrid(int value);
[[nodiscard]] QPoint gridToPixels(const QPoint& point);
[[nodiscard]] QPoint pixelsToGrid(const QPoint& point);
[[nodiscard]] QSize pixelsToGrid(const QSize& size);
[[nodiscard]] int pixelsToNearestGrid(qreal value);
[[nodiscard]] QPoint pixelsToNearestGrid(const QPointF& point);

[[nodiscard]] silicon::project::Document
preparedSubcircuitDocument(std::string path, std::string sceneJson);
