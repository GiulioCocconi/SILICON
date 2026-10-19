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

#include "graphicalItem.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

#include <QVariant>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/graphicalComponent.hpp>
#include <ui/circuit/diagram/graphicalWire.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/shell/siliconWindow.hpp>


namespace SILICON::ui {
using namespace SILICON::core;

namespace {

std::atomic<uint64_t> NextGraphicalItemUiId = 1;

bool wireTouchesComponentPort(const GraphicalWire& wire,
                              const GraphicalComponent& component)
{
  const auto touchesPort = [&](const Port* port) {
    const QPointF position = component.mapToScene(port->getPosition());
    for (const auto* segment : wire.getSegments()) {
      const auto points = segment->getScenePoints();
      if (!points.empty() && (points.front() == position || points.back() == position))
        return true;
    }
    return false;
  };

  return std::ranges::any_of(component.getInputPorts(), touchesPort)
         || std::ranges::any_of(component.getOutputPorts(), touchesPort);
}

}

GraphicalItem::GraphicalItem(ItemCategory category, QGraphicsItem* parent)
  : QGraphicsObject(parent), uiId(generateUiId())
{
  setData(std::to_underlying(ItemDataRole::Category),
          std::to_underlying(category | ItemCategory::GraphicalItem));
}

GraphicalItem::~GraphicalItem()
{
  if (owningScene)
    owningScene->unregisterGraphicalItem(this);
}

uint64_t GraphicalItem::generateUiId()
{
  return NextGraphicalItemUiId.fetch_add(1, std::memory_order_relaxed);
}

void GraphicalItem::setCollidingStatus(const CollidingStatus newStatus)
{
  collidingStatus = newStatus;
  prepareGeometryChange();
}

void GraphicalItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  QGraphicsObject::mousePressEvent(event);

  if (event->button() == Qt::LeftButton) {
    for (QGraphicsItem* item : scene()->selectedItems()) {
      if (auto gi = dynamic_cast<GraphicalItem*>(item)) {
        gi->setInitialPosition();
      }
    }
  }
}

void GraphicalItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  QGraphicsObject::mouseReleaseEvent(event);

  const auto ds = qobject_cast<DiagramScene*>(scene());

  if (!ds)
    return;

  auto topologyEdit = ds->finishMixedDrag();
  auto wireMoves = ds->finishComponentDrag();

  if (this->isColliding())
    this->setCollidingStatus(NOT_COLLIDING);

  bool itemsActuallyMoved = false;
  auto undoStack          = ds->getUndoStack();
  auto moveCmd            = new MoveItemCommand();

  for (QGraphicsItem* item : scene()->selectedItems()) {
    if (const auto gi = dynamic_cast<GraphicalItem*>(item)) {
      if (gi->pos() != gi->getInitialPosition()) {
        itemsActuallyMoved = true;
        moveCmd->addItemMove(gi, gi->getInitialPosition(), gi->pos());
        gi->setInitialPosition();  // Update for the next interaction
      }
    }
  }

  if (itemsActuallyMoved) {
    moveCmd->setWireMoves(std::move(wireMoves));
    moveCmd->setTopologyEdit(std::move(topologyEdit));
    if (undoStack)
      undoStack->push(moveCmd);
    else
      delete moveCmd;
  } else {
    delete moveCmd;
  }
}

QVariant GraphicalItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
  auto* ds = dynamic_cast<DiagramScene*>(scene());
  if (!ds)
    return QGraphicsItem::itemChange(change, value);

  if (change == ItemSceneHasChanged) {
    // Keep the scene lookup table in sync as Qt reparents/moves items between scenes.
    if (owningScene != ds) {
      if (owningScene)
        owningScene->unregisterGraphicalItem(this);

      ds->registerGraphicalItem(this);
      connect(ds, &DiagramScene::modeChanged, this, &GraphicalItem::modeChanged,
              Qt::UniqueConnection);
    }

    modeChanged(ds->getInteractionMode());
  }

  if (change == ItemPositionChange) {
    // ==== Collision detection ====
    const auto proposedPos = DiagramScene::snapToGrid(value.toPointF());
    const auto offset      = proposedPos - pos();

    this->collidingStatus = CollidingStatus::NOT_COLLIDING;

    if (!ds->itemCollisionChecksEnabled()) {
      onPositionChanged(offset);
      return proposedPos;
    }

    // Calculate the bounding rectangle at the *new* position in scene coordinates.
    // Use the item's bounding rectangle, offset by the proposed new position.
    auto newCollisionRect = getCollisionRect().translated(proposedPos);

    if (canRotate()) {
      // Rotate newRect by item's rotation centered at item most-topleft pos using
      // QTransform
      const auto transform = QTransform()
                                 .translate(proposedPos.x(), proposedPos.y())
                                 .rotate(rotation())
                                 .translate(-proposedPos.x(), -proposedPos.y());
      newCollisionRect = transform.mapRect(newCollisionRect);
    }

    // Find all graphics items that intersect with our collision rectangle
    // Filter out: current item, other selected items (as they are moving too), child
    // items, and items without valid types

    /*  FIXME: Because we are ignoring selected items for the collision check some things
     * are not allowed (for example moving two different horizzontaly aligned items
     * against a wall will lead to the overlapping of the two items without triggering
     * collision detection */

    const auto children = childItems();
    const auto collidingItems =
        scene()->items(newCollisionRect, Qt::IntersectsItemBoundingRect)
        | std::views::filter([this, &children](auto item) {
            return !(item == this || item->isSelected() || children.contains(item)
                     || item->type() <= UNKNOWN);
          })
        | std::ranges::to<std::vector>();

    QPointF finalPos          = proposedPos;
    bool    collisionRejected = false;
    auto* movingComponent = category_cast<GraphicalComponent>(this, ItemCategory::Component);
    auto* movingSegment = category_cast<GraphicalWireSegment>(this,
                                                               ItemCategory::WireSegment);

    // Analyze each potentially colliding item to determine collision type
    for (QGraphicsItem* collidingItem : collidingItems) {
      auto* otherComponent = category_cast<GraphicalComponent>(
          collidingItem, ItemCategory::Component);
      if (movingComponent && otherComponent) {
        // Component-component collision
        // Since we simply care if the bounding rects intersects we should reject all
        // changes
        movingComponent->setCollidingStatus(COLLIDING_WITH_COMPONENT);
        otherComponent->setCollidingStatus(COLLIDING_WITH_COMPONENT);

        finalPos          = pos();
        collisionRejected = true;
        break;  // Reject immediately, stop checking others
      }

      // Check component-wire collisions for both component and wire movement.
      auto* collidingComponent = movingComponent ? movingComponent : otherComponent;
      auto* collidingSegment = movingSegment ? movingSegment
          : category_cast<GraphicalWireSegment>(collidingItem,
                                                ItemCategory::WireSegment);
      if (!collidingComponent || !collidingSegment)
        continue;
      const auto* collidingWire      = collidingSegment->getGraphicalWire();

      if (!collidingWire)
        continue;

      // An attached net follows its component during a drag. Other nets must
      // still prevent the component from moving onto their wires.
      if (this == collidingComponent
          && ds->getInteractionMode() == InteractionMode::NORMAL_MODE
          && (ds->isDraggingComponent(collidingComponent)
                  ? ds->isWireAttachedToDraggedComponent(collidingComponent,
                                                          collidingWire)
                  : wireTouchesComponentPort(*collidingWire, *collidingComponent)))
        continue;

      // Container for exclusion zones around wire vertices
      QPainterPath toBeSubtractedWire{};

      // Get wire vertices and create the base collision shape
      // GraphicalWire::shape() and getVertices() already return scene coordinates
      auto         vertices      = collidingWire->getVertices();
      QPainterPath collisionPath = collidingWire->shape();

      // If dragged item is a wire segment, translate the path to match the proposed
      // position
      if (this->type() == WIRE_SEGMENT) {
        collisionPath.translate(offset);
        for (auto& vertex : vertices)
          vertex += offset;
      }

      // Create circular exclusion zones (5px radius) around each wire vertex
      // These represent port connection points that should not trigger collision
      for (const auto vertex : vertices) {
        toBeSubtractedWire.addEllipse(vertex, 5, 5);
      }

      // GraphicalWire::shape() already returns scene-coordinate paths, no mapToScene
      // needed. Subtract the port exclusion zones from the wire's collision path.
      collisionPath = collisionPath.subtracted(toBeSubtractedWire);

      // Determine the component position for collision check
      QPointF collidingComponentOffset = hasCategory(this, ItemCategory::Component)
                                             ? proposedPos
                                             : collidingComponent->pos();

      // Check if the wire's collision path intersects with the component's wire collision
      // area
      const bool isCollidingWithWire =
          collisionPath.intersects(collidingComponent->collisionRectForWires().translated(
              collidingComponentOffset));

      // If collision detected, mark the component and reject the position change
      if (isCollidingWithWire) {
        collidingComponent->setCollidingStatus(COLLIDING_WITH_WIRE);
        finalPos          = pos();
        collisionRejected = true;
        break;  // Reject immediately
      }
    }

    if (!collisionRejected) {
      onPositionChanged(offset);
    }

    return finalPos;
  }

  if (change == ItemSelectedChange && isColliding())
    setCollidingStatus(NOT_COLLIDING);

  // For all other item changes, use default Qt behavior
  return QGraphicsItem::itemChange(change, value);
}

void GraphicalItem::modeChanged(InteractionMode mode)
{
  switch (mode) {
    case InteractionMode::NORMAL_MODE:
      setFlag(QGraphicsItem::ItemIsMovable);
      setFlag(QGraphicsItem::ItemIsFocusable);
      setFlag(QGraphicsItem::ItemIsSelectable);
      break;
    case InteractionMode::WIRE_CREATION_MODE:
    case InteractionMode::COMPONENT_PLACING_MODE:
    case InteractionMode::SIMULATION_MODE:
    case InteractionMode::PAN_MODE:
      setFlag(QGraphicsItem::ItemIsSelectable, false);
      setFlag(QGraphicsItem::ItemIsMovable, false);
      setFlag(QGraphicsItem::ItemIsFocusable, false);
      break;
    default: throw std::logic_error("Unhandled InteractionMode in modeChanged");
  }
}

}  // namespace SILICON::ui
