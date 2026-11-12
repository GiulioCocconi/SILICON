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

#include <algorithm>

#include <QGraphicsView>
#include <QWheelEvent>

#include <ui/circuit/diagram/scene/diagramScene.hpp>


namespace SILICON::ui {
class DiagramView : public QGraphicsView {
  Q_OBJECT
public:
  explicit DiagramView(QWidget* parent = nullptr);
  void setScene(DiagramScene* scene);

protected:
  void wheelEvent(QWheelEvent* event) override;

public slots:
  /** @brief Increases the circuit zoom by one step. */
  void zoomIn();
  /** @brief Decreases the circuit zoom by one step. */
  void zoomOut();
  /**
   * @brief Centers a scene rectangle and fits it within the existing zoom limits.
   * @param rect Rectangle in scene coordinates; QRect converts to QRectF implicitly.
   * Empty rectangles are ignored. The aspect ratio is preserved.
   */
  void zoomToRect(const QRectF& rect);
  /** @brief Pans the viewport so the circuit's bounds are centered. */
  void centerCircuit();
  void modeChanged(InteractionMode mode);

private:
  static constexpr int MIN_ZOOM_LEVEL     = 60;
  static constexpr int MAX_ZOOM_LEVEL     = 200;
  int                  zoomLevel          = 100;
  qreal                wheelZoomRemainder = 0;

  bool                 zoom(int level);
  void                 updateZoom();
};

}  // namespace SILICON::ui
