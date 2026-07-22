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

#include "utils.hpp"

#include <cmath>
#include <utility>

#include <QPainter>

#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/theme.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>

SubcircuitRectShape::SubcircuitRectShape(const QSize& size, QGraphicsItem* parent)
  : QGraphicsRectItem(0, 0, size.width(), size.height(), parent)
{
}

void SubcircuitRectShape::paint(QPainter* painter,
                                const QStyleOptionGraphicsItem* option,
                                QWidget* widget)
{
  setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));
  setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
  QGraphicsRectItem::paint(painter, option, widget);
}

int gridToPixels(const int value)
{ return value * DiagramScene::GRID_SIZE; }

int pixelsToGrid(const int value)
{ return value / DiagramScene::GRID_SIZE; }

QPoint gridToPixels(const QPoint& point)
{ return {gridToPixels(point.x()), gridToPixels(point.y())}; }

QPoint pixelsToGrid(const QPoint& point)
{ return {pixelsToGrid(point.x()), pixelsToGrid(point.y())}; }

QSize pixelsToGrid(const QSize& size)
{ return {pixelsToGrid(size.width()), pixelsToGrid(size.height())}; }

int pixelsToNearestGrid(const qreal value)
{ return static_cast<int>(std::lround(value / DiagramScene::GRID_SIZE)); }

QPoint pixelsToNearestGrid(const QPointF& point)
{ return {pixelsToNearestGrid(point.x()), pixelsToNearestGrid(point.y())}; }

silicon::project::Document preparedSubcircuitDocument(std::string path,
                                                       std::string sceneJson)
{
  auto coreJson = graphicalSubcircuitCoreCircuitJson(sceneJson);
  return {std::move(path), std::move(sceneJson), std::move(coreJson)};
}
