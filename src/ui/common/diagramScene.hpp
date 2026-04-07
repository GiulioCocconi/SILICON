/*
 Copyright (c) 2025. Giulio Cocconi

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

#include <ranges>
#include <string>

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainter>
#include <QRect>

#include <ui/common/componentSearchBox.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/wireManager.hpp>

class GraphicalComponent;
class GraphicalWireSegment;
class WireManager;

class DiagramScene : public QGraphicsScene {
  Q_OBJECT
public:
  enum class InteractionMode {
    NORMAL_MODE,
    PAN_MODE,
    WIRE_CREATION_MODE,
    COMPONENT_PLACING_MODE,
    SIMULATION_MODE,
  };

  explicit DiagramScene(QObject* parent = nullptr);

  void                          setInteractionMode(InteractionMode mode);
  [[nodiscard]] InteractionMode getInteractionMode() const
  {
    return currentInteractionMode;
  }

  [[nodiscard]] GraphicalComponent* getComponentToBeDrawn() const;

  void showCSB(QPointF pos);

  void clearWireShadow();
  void setComponentShadow();
  void clearComponentShadow();

  void addComponent(GraphicalComponent* component, QPointF pos);

  void placeComponent(std::string typeName);

  static QPointF snapToGrid(QPointF point);

  static constexpr int GRID_SIZE = 10;

  ~DiagramScene() override;

public slots:
  void hideCSB();

  void calculateWiresForComponents() const;

signals:
  void modeChanged(InteractionMode mode);

private:
  void drawBackground(QPainter* painter, const QRectF& rect) override;

  void setInteractionMode(InteractionMode newMode, bool force);

  void mouseMoveEvent(QGraphicsSceneMouseEvent* mouseEvent) override;
  void mousePressEvent(QGraphicsSceneMouseEvent* mouseEvent) override;
  void keyPressEvent(QKeyEvent* event) override;

  InteractionMode currentInteractionMode = InteractionMode::NORMAL_MODE;

  // Wire and component shadows to be used in `WIRE_CREATION_MODE` and
  // `COMPONENT_PLACING_MODE`
  GraphicalComponent*   componentToBeDrawn   = nullptr;
  GraphicalWireSegment* wireSegmentToBeDrawn = nullptr;

  ComponentSearchBox* csb = nullptr;

  WireManager wireManager;

  std::string lastPlacedComponentType;
};

using InteractionMode = DiagramScene::InteractionMode;
