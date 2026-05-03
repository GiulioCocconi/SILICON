#include "undoCommands.hpp"
#include "graphicalComponent.hpp"
#include "graphicalItem.hpp"
#include "wireManager.hpp"

// --- MoveItemCommand ---

void MoveItemCommand::undo()
{
  for (const auto& move : moves) {
    move.item->setPos(move.oldPos);
    move.item->setInitialPosition();

    // If the moved item is a wire segment, update the topology
    if (auto* segment = dynamic_cast<GraphicalWireSegment*>(move.item)) {
      if (auto* wire = segment->getGraphicalWire()) {
        if (auto* manager = wire->getManager()) {
          manager->updateSegmentTopology(segment);
        }
      }
    }
  }
}

void MoveItemCommand::redo()
{
  for (const auto& move : moves) {
    move.item->setPos(move.newPos);
    move.item->setInitialPosition();

    // If the moved item is a wire segment, update the topology
    if (auto* segment = dynamic_cast<GraphicalWireSegment*>(move.item)) {
      if (auto* wire = segment->getGraphicalWire()) {
        if (auto* manager = wire->getManager()) {
          manager->updateSegmentTopology(segment);
        }
      }
    }
  }
}

// --- MoveWirePointCommand ---

void MoveWirePointCommand::undo()
{
  if (segment) {
    segment->movePointTo(pointIndex, oldPos);

    // Update topology for point modification
    if (auto* wire = segment->getGraphicalWire()) {
      if (auto* manager = wire->getManager()) {
        manager->updateSegmentTopology(segment);
      }
    }
  }
}

void MoveWirePointCommand::redo()
{
  if (segment) {
    segment->movePointTo(pointIndex, newPos);

    // Update topology for point modification
    if (auto* wire = segment->getGraphicalWire()) {
      if (auto* manager = wire->getManager()) {
        manager->updateSegmentTopology(segment);
      }
    }
  }
}

// --- RotateItemCommand ---

void RotateItemCommand::undo()
{
  if (component) {
    component->setRotation(oldRotation);
    component->setInitialRotation();
    component->update();
  }
}

void RotateItemCommand::redo()
{
  if (component) {
    component->setRotation(newRotation);
    component->setInitialRotation();
    component->update();
  }
}