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

#include "graphicalComponent.hpp"
#include "graphicalWire.hpp"
#include <ui/common/diagramScene.hpp>

void GraphicalItem::setCollidingStatus(const CollidingStatus newStatus)
{
  collidingStatus = newStatus;
  prepareGeometryChange();
}
QVariant GraphicalItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
  // Early exit if item is not yet added to a scene
  if (!scene())
    return QGraphicsItem::itemChange(change, value);

  if (change == ItemPositionChange) {
    // ==== Collision detection ====
    const auto proposedPos = DiagramScene::snapToGrid(value.toPointF());
    const auto offset      = proposedPos - pos();

    this->collidingStatus = CollidingStatus::NOT_COLLIDING;

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
    // Filter out: current item, child items, and items without valid types
    const auto collidingItems =
        scene()->items(newCollisionRect, Qt::IntersectsItemBoundingRect)
        | std::views::filter([this](auto item) {
            return item != this && !(childItems().contains(item))
                   && item->type() > UNKNOWN;
          })
        | std::ranges::to<std::vector>();

    // Step 6: Analyze each potentially colliding item to determine collision type
    for (QGraphicsItem* collidingItem : collidingItems) {
      // Initialize containers to categorize the colliding pair
      std::unordered_set<GraphicalComponent*>   componentsInPair;
      std::unordered_set<GraphicalWireSegment*> segmentsInPair;

      // Lambda function to categorize items as components or wire segments
      auto categorizeItem = [&](QGraphicsItem* item) {
        if (item->type() >= COMPONENT)
          componentsInPair.insert(qgraphicsitem_cast<GraphicalComponent*>(item));
        else if (item->type() == WIRE_SEGMENT)
          segmentsInPair.insert(qgraphicsitem_cast<GraphicalWireSegment*>(item));
      };

      // Categorize both the current item and the colliding item
      categorizeItem(this);
      categorizeItem(collidingItem);

      // Skip if categorization doesn't result in exactly 2 valid items
      if (componentsInPair.size() + segmentsInPair.size() != 2)
        continue;

      if (componentsInPair.empty()) {
        // Wire-Wire collision
        // TODO: IMPLEMENT
        continue;
      }

      else if (segmentsInPair.empty()) {
        // Component-component collision
        // Since we simply care if the bounding rects intersects we should reject all
        // changes
        for (auto el : componentsInPair)
          el->setCollidingStatus(COLLIDING_WITH_COMPONENT);
        return pos();
      }

      // Component-wire collision
      // The collision is checked against wire shape and the component's colliding rect
      // for wires

      // Extract the component and colliding segment
      auto        collidingComponent = *(componentsInPair.begin());
      auto        collidingSegment   = *(segmentsInPair.begin());
      const auto* collidingWire      = collidingSegment->getGraphicalWire();

      if (!collidingWire)
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
      QPointF collidingComponentOffset =
          (this->type() >= COMPONENT) ? proposedPos : collidingComponent->pos();

      // Check if the wire's collision path intersects with the component's wire collision
      // area
      const bool isCollidingWithWire =
          collisionPath.intersects(collidingComponent->collisionRectForWires().translated(
              collidingComponentOffset));

      // If collision detected, mark the component and reject the position change
      if (isCollidingWithWire) {
        collidingComponent->setCollidingStatus(COLLIDING_WITH_WIRE);
        return pos();
      }
    }

    // No collisions detected - notify the item of the successful position change
    onPositionChanged(offset);
    // Return the proposed new position
    return proposedPos;
  } else if (change == ItemSceneHasChanged) {
    // Connect the modeChanged from the new scene (only one scene during the object
    // lifetime is supported)

    const auto ds = dynamic_cast<DiagramScene*>(this->scene());
    connect(ds, &DiagramScene::modeChanged, this, &GraphicalItem::modeChanged);
  }

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
    default: assert(false);
  }
}
