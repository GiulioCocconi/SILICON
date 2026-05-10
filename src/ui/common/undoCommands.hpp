#pragma once

#include <QPointF>
#include <QUndoCommand>
#include <vector>

#include <ui/common/graphicalWire.hpp>

class GraphicalItem;
class GraphicalComponent;

class MoveItemCommand : public QUndoCommand {
public:
  explicit MoveItemCommand(QUndoCommand* parent = nullptr) : QUndoCommand(parent)
  {
    setText("Move Component(s)");
  }

  void addItemMove(GraphicalItem* item, const QPointF& oldPos, const QPointF& newPos)
  {
    moves.push_back({item, oldPos, newPos});
  }

  void undo() override;
  void redo() override;

private:
  struct ItemMove {
    GraphicalItem* item;
    QPointF        oldPos;
    QPointF        newPos;
  };
  std::vector<ItemMove> moves;
  bool                  skipInitialRedo = true;
};

class MoveWirePointCommand : public QUndoCommand {
public:
  explicit MoveWirePointCommand(GraphicalWireSegment* segment, size_t pointIndex,
                                const QPointF& oldPos, const QPointF& newPos,
                                QUndoCommand* parent = nullptr)
    : QUndoCommand(parent),
      segment(segment),
      pointIndex(pointIndex),
      oldPos(oldPos),
      newPos(newPos)
  {
    setText("Move Wire Point");
  }

  void undo() override;
  void redo() override;

private:
  GraphicalWireSegment* segment;
  size_t                pointIndex;
  QPointF               oldPos;
  QPointF               newPos;
  bool                  skipInitialRedo = true;
};

class RotateItemCommand : public QUndoCommand {
public:
  explicit RotateItemCommand(GraphicalComponent* component, qreal oldRotation,
                             qreal newRotation, QUndoCommand* parent = nullptr)
    : QUndoCommand(parent),
      component(component),
      oldRotation(oldRotation),
      newRotation(newRotation)
  {
    setText("Rotate Component");
  }

  void undo() override;
  void redo() override;

private:
  GraphicalComponent* component;
  qreal               oldRotation;
  qreal               newRotation;
  bool                skipInitialRedo = true;
};
