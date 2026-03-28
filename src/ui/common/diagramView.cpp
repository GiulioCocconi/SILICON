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

DiagramView::DiagramView(QWidget* parent) : QGraphicsView(parent)
{
  setMouseTracking(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void DiagramView::setScene(DiagramScene* scene)
{
  connect(scene, &DiagramScene::modeChanged, this, &DiagramView::modeChanged);

  modeChanged(scene->getInteractionMode());

  QGraphicsView::setScene(scene);
}

void DiagramView::wheelEvent(QWheelEvent* event)
{
  const bool zoomDirection = event->angleDelta().y() > 0;

  // If the scene is in Wire Creation mode then the mouse wheel shouldn't do anything
  const auto dg = dynamic_cast<DiagramScene*>(scene());
  if (dg->getInteractionMode() == InteractionMode::WIRE_CREATION_MODE) {
    event->ignore();
    return;
  }

  // If the zoom level changes the scene should be centered to the pointer position
  if (zoom(zoomDirection))
    centerOn(mapToScene(event->position().toPoint()));
  event->accept();
}

bool DiagramView::zoom(bool dir)
{
  const int sign = dir ? 1 : -1;
  return zoom(zoomLevel + sign * 20);
}

bool DiagramView::zoom(int level)
{
  level = std::clamp(level, MIN_ZOOM_LEVEL, MAX_ZOOM_LEVEL);

  if (level != this->zoomLevel) {
    this->zoomLevel = level;
    updateZoom();
    return true;
  }

  return false;
}

void DiagramView::updateZoom()
{
  resetTransform();

  const float FACTOR = static_cast<float>(zoomLevel) / 100;

  scale(FACTOR, FACTOR);
}

int DiagramView::getZoomLevel() const
{
  return zoomLevel;
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
