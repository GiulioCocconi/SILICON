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

#include "diagramView.hpp"

#include <stdexcept>

namespace SILICON::ui {
using namespace SILICON::core;

DiagramView::DiagramView(QWidget* parent) : QGraphicsView(parent)
{
  setMouseTracking(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void DiagramView::setScene(DiagramScene* scene)
{
  if (auto* previousScene = qobject_cast<DiagramScene*>(QGraphicsView::scene()))
    disconnect(previousScene, &DiagramScene::modeChanged, this,
               &DiagramView::modeChanged);

  QGraphicsView::setScene(scene);

  if (!scene)
    return;

  connect(scene, &DiagramScene::modeChanged, this, &DiagramView::modeChanged);
  modeChanged(scene->getInteractionMode());
}

void DiagramView::wheelEvent(QWheelEvent* event)
{
  const auto* dg = qobject_cast<DiagramScene*>(scene());

  if (!dg) {
    QGraphicsView::wheelEvent(event);
    return;
  }

  if (dg->getInteractionMode() == InteractionMode::WIRE_CREATION_MODE
      && dg->isWireCreationInProgress()) {
    event->ignore();
    return;
  }

  const bool isPixelDelta = !event->pixelDelta().isNull();
  const int  delta = isPixelDelta ? event->pixelDelta().y() : event->angleDelta().y();

  constexpr qreal ZOOM_STEP  = 20.0;
  constexpr qreal ANGLE_STEP = 120.0;
  constexpr qreal PIXEL_STEP = 100.0;

  wheelZoomRemainder += delta * ZOOM_STEP / (isPixelDelta ? PIXEL_STEP : ANGLE_STEP);

  const int increment = static_cast<int>(wheelZoomRemainder);
  wheelZoomRemainder -= increment;

  const auto cursor = event->position().toPoint();
  const auto before = mapToScene(cursor);

  if (increment != 0 && zoom(zoomLevel + increment)) {
    const auto after = mapToScene(cursor);
    centerOn(mapToScene(viewport()->rect().center()) + before - after);
  }

  event->accept();
}

void DiagramView::zoomIn()
{
  zoom(zoomLevel + 20);
}

void DiagramView::zoomOut()
{
  zoom(zoomLevel - 20);
}

bool DiagramView::zoom(int level)
{
  level = std::clamp(level, MIN_ZOOM_LEVEL, MAX_ZOOM_LEVEL);

  if (level == zoomLevel)
    return false;

  zoomLevel = level;
  updateZoom();
  return true;
}

void DiagramView::updateZoom()
{
  const qreal factor = static_cast<qreal>(zoomLevel) / 100.0;
  setTransform(QTransform::fromScale(factor, factor));
}

void DiagramView::centerCircuit()
{
  const auto* currentScene = scene();
  centerOn(currentScene ? currentScene->itemsBoundingRect().center() : QPointF{});
}

void DiagramView::modeChanged(InteractionMode mode)
{
  // TODO: Set items flags using this->items()

  setCursor(Qt::ArrowCursor);
  setDragMode(QGraphicsView::NoDrag);

  switch (mode) {
    case InteractionMode::NORMAL_MODE: setDragMode(QGraphicsView::RubberBandDrag); break;
    case InteractionMode::WIRE_CREATION_MODE: setCursor(Qt::CrossCursor); break;
    case InteractionMode::PAN_MODE:
      setDragMode(QGraphicsView::ScrollHandDrag);
      setCursor(Qt::SizeAllCursor);
      break;
    case InteractionMode::COMPONENT_PLACING_MODE:
    case InteractionMode::SIMULATION_MODE: break;
    default: throw std::logic_error("Unhandled InteractionMode in modeChanged");
  }
}

}  // namespace SILICON::ui
