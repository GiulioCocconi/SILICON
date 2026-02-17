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

#include <QGraphicsItem>
#include <QKeyEvent>
#include <QPainter>
#include <QPoint>
#include <QRect>

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <core/component.hpp>
#include <ui/common/diagramScene.hpp>
#include <ui/common/enums.hpp>
#include <ui/common/graphicalItem.hpp>

class Port : public QGraphicsItem {
private:
  unsigned int       index;
  QPoint             position;
  QGraphicsLineItem* line;
  std::string        name;

public:
  Port(unsigned int index, QPoint position, std::string name,
       QGraphicsItem* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override {};

  QRectF boundingRect() const override { return this->line->boundingRect(); };

  unsigned int getIndex() const { return this->index; }
  QPoint       getPosition() const { return this->position; }
  std::string  getName() const { return this->name; }

  void setLine(QGraphicsLineItem* line);

  [[nodiscard]] QRectF collisionRect() const;
};

class PropertiesDialog : public QDialog {
public:
  explicit PropertiesDialog(const QList<QWidget*>& widgets, QWidget* parent = nullptr);
};

class GraphicalComponent : public GraphicalItem {
  Q_OBJECT
public:
  [[nodiscard]] QRectF collisionRectForWires() const;

protected:
  void                         setItemShape(QGraphicsItem* shape);
  [[nodiscard]] QGraphicsItem* getItemShape() const { return itemShape; }

  [[nodiscard]] QRectF boundingRect() const override;
  [[nodiscard]] QRectF boundingRectWithoutMargins() const;

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  [[nodiscard]] QRectF getCollisionRect() const override;
  [[nodiscard]] bool   canRotate() const override { return true; }

  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

  std::vector<Port*> inputPorts;
  std::vector<Port*> outputPorts;

  void setPortLine(Port* port);

  bool scanShape = false;

  PropertiesDialog* propertiesDialog = nullptr;

public slots:
  virtual void propertiesDialogAccepted() {};
  virtual void propertiesDialogRejected();

public:
  explicit GraphicalComponent(QGraphicsItem* shape, QGraphicsItem* parent = nullptr,
                              bool scanShape = false);

  void               rotate();

  virtual void
  setPorts(const std::vector<std::pair<std::string, QPoint>>& busToPortInputs,
           const std::vector<std::pair<std::string, QPoint>>& busToPortOutputs);

  virtual void showPropertiesDialog();

  [[nodiscard]] std::vector<Port*> getInputPorts() const { return inputPorts; };
  [[nodiscard]] std::vector<Port*> getOutputPorts() const { return outputPorts; };

private:
  QPoint scanImage(const QImage& image, const QPoint& initialPoint, bool coordinate,
                   bool direction) const;
  QGraphicsItem* itemShape = nullptr;
};
