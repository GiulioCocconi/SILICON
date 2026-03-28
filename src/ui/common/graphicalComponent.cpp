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

#include "graphicalComponent.hpp"

#include <QLabel>
#include <stdexcept>

GraphicalComponent::GraphicalComponent(QGraphicsItem* shape, QGraphicsItem* parent,
                                       bool scanShape)
  : GraphicalItem(parent)
{
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);

  setAcceptHoverEvents(true);
  setAcceptedMouseButtons(Qt::AllButtons);

  if (shape)
    setItemShape(shape);

  this->scanShape = scanShape;
}

void GraphicalComponent::setItemShape(QGraphicsItem* shape)
{
  if (!shape)
    throw std::invalid_argument("setItemShape: shape must not be null");

  if (this->itemShape) {
    prepareGeometryChange();
    delete this->itemShape;
  }

  this->itemShape = shape;
  this->itemShape->setParentItem(this);
}

QRectF GraphicalComponent::boundingRect() const
{
  const qreal margin = isSelected() ? 0 : 4;  // Pen width is 3
  auto        rect   = boundingRectWithoutMargins();
  rect.adjust(-margin, -margin, margin, margin);

  return rect;
}

QRectF GraphicalComponent::boundingRectWithoutMargins() const
{
  QRectF rect = itemShape->boundingRect();  // Start with shape bounds

  for (const QGraphicsItem* child : childItems())
    rect = rect.united(child->boundingRect());

  return rect;
}

void GraphicalComponent::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                               QWidget* widget)
{
  if (isSelected()) {
    auto color = isColliding() ? Qt::red : Qt::black;
    auto style = isColliding() ? Qt::SolidLine : Qt::DotLine;

    painter->setPen(QPen(color, 3, style));
    painter->drawRect(this->boundingRectWithoutMargins());
  }
}

QRectF GraphicalComponent::collisionRectForWires() const
{
  QPainterPath componentPath{};
  componentPath.addRect(itemShape->boundingRect());

  for (const auto port : inputPorts)
    componentPath.addRect(port->collisionRect());

  for (const auto port : outputPorts)
    componentPath.addRect(port->collisionRect());

  return componentPath.boundingRect();
}

void GraphicalComponent::rotate()
{
  setRotation(rotation() + 90);
  prepareGeometryChange();
  update();
}

QRectF GraphicalComponent::getCollisionRect() const
{
  return boundingRectWithoutMargins();
}

void GraphicalComponent::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && isSelected()) {
    showPropertiesDialog();
    return;
  }
  GraphicalItem::mouseDoubleClickEvent(event);
}

void GraphicalComponent::propertiesDialogRejected()
{
  if (!scene())
    throw std::logic_error("propertiesDialogRejected: component not in a scene");
  auto       diagramScene = dynamic_cast<DiagramScene*>(scene());
  const auto currentMode  = diagramScene->getInteractionMode();

  // If the changes are rejected when the component is being placed then we shouldn't
  // place it anymore
  if (currentMode == InteractionMode::COMPONENT_PLACING_MODE)
    diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);
}

void GraphicalComponent::setPorts(
    const std::vector<std::pair<std::string, QPoint>>& busToPortInputs,
    const std::vector<std::pair<std::string, QPoint>>& busToPortOutputs)
{
  for (const auto& [index, pair] : busToPortInputs | silicon::views::enumerate) {
    const auto& [name, pos] = pair;

    // The memory is not leaked since the port address is freed by Qt Garbage collector
    // ReSharper disable once CppDFAMemoryLeak
    auto p = new Port(index, pos, name);
    this->inputPorts.push_back(p);
    this->setPortLine(p);
  }

  for (const auto& [index, pair] : busToPortOutputs | silicon::views::enumerate) {
    const auto& [name, pos] = pair;

    // ReSharper disable once CppDFAMemoryLeak
    auto p = new Port(index, pos, name);
    this->outputPorts.push_back(p);
    this->setPortLine(p);
  }
}

void GraphicalComponent::showPropertiesDialog()
{
  if (this->propertiesDialog)
    propertiesDialog->show();
}

QPoint GraphicalComponent::scanImage(const QImage& image, const QPoint& initialPoint,
                                     const bool coordinate, const bool direction) const
{
  if (!this->scanShape) {
    return initialPoint;
  }

  const int initialCoord = coordinate ? initialPoint.x() : initialPoint.y();

  const QPoint topLeft     = image.rect().topLeft();
  const QPoint bottomRight = image.rect().bottomRight();

  const int leftUp    = coordinate ? topLeft.x() : topLeft.y();
  const int rightDown = coordinate ? bottomRight.x() : bottomRight.y();

  for (qreal coord = initialCoord; direction ? coord < rightDown : coord > leftUp;
       coord += direction ? 1 : -1) {
    auto p =
        coordinate ? QPoint(coord, initialPoint.y()) : QPoint(initialPoint.x(), coord);

    if (qAlpha(image.pixel(p)) != 0) {
      return p;
    }
  }
  return initialPoint;
}

void GraphicalComponent::setPortLine(Port* port)
{
  if (!itemShape)
    throw std::logic_error("setPortLine: item shape not set");

  // Get the shapeRect and its size
  const auto shapeRect = itemShape->boundingRect();
  if (shapeRect.isEmpty())
    throw std::logic_error("setPortLine: shape bounding rect is empty");

  const auto shapeSize = shapeRect.size().toSize();
  if (shapeSize.width() <= 0 || shapeSize.height() <= 0)
    throw std::logic_error("setPortLine: shape dimensions must be positive");

  // Create an image that supports transparency in order to alpha-scan
  auto image = QImage(shapeSize, QImage::Format_ARGB32);

  // Paint the shape on the image
  if (this->scanShape) {
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-shapeRect.topLeft());

    const auto options = QStyleOptionGraphicsItem();
    itemShape->paint(&painter, &options, nullptr);
  }

  // Get image coordinates
  const auto topLeftX     = shapeRect.topLeft().x();
  const auto topLeftY     = shapeRect.topLeft().y();
  const auto bottomRightX = shapeRect.bottomRight().x();
  const auto bottomRightY = shapeRect.bottomRight().y();

  // Get port position
  const auto portPos = port->getPosition();
  const auto portX   = portPos.x();
  const auto portY   = portPos.y();

  // Find the projection of the port on the shape
  QPoint projectionOnShape{};

  // Left side
  if (portX < topLeftX) {
    projectionOnShape = scanImage(image, QPoint(topLeftX, portY), true, true);
  }
  // Right side
  else if (portX > bottomRightX) {
    projectionOnShape = scanImage(image, QPoint(bottomRightX, portY), true, false);
  }
  // Up side
  else if (portY < topLeftY) {
    projectionOnShape = scanImage(image, QPoint(portX, topLeftY), false, true);
  }
  // Down side
  else if (portY > bottomRightY) {
    projectionOnShape = scanImage(image, QPoint(portX, bottomRightY), false, false);
  } else
    throw std::logic_error("setPortLine: port position is not outside the shape");

  // Create the line from port position to the projection
  port->setLine(new QGraphicsLineItem(QLineF(portPos, projectionOnShape), this));
}

Port::Port(const unsigned int index, const QPoint position, std::string name,
           QGraphicsItem* parent)
  : QGraphicsItem(parent)
{
  this->index    = index;
  this->position = position;
  this->name     = std::move(name);
}

void Port::setLine(QGraphicsLineItem* line)
{
  this->line = line;
  this->line->setPen(QPen(QBrush(Qt::black), 3));
  setParentItem(line->parentItem());
}

QRectF Port::collisionRect() const
{
  QPainterPath boundingPath{};
  boundingPath.addRect(boundingRect());

  QPainterPath subtract{};
  subtract.addEllipse(position, 5, 5);

  return boundingPath.subtracted(subtract).boundingRect();
}

PropertiesDialog::PropertiesDialog(const QList<QWidget*>& widgets, QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle("Properties");
  setFixedSize(1200, 300);
  setModal(true);

  auto mainLayout = new QVBoxLayout();
  setLayout(mainLayout);
  mainLayout->setSpacing(10);

  // ReSharper disable CppDFAMemoryLeak
  const auto titleLabel = new QLabel("Edit properties...", this);
  titleLabel->setFont(QFont("Chango", 20));
  mainLayout->addWidget(titleLabel);

  for (const auto widget : widgets) {
    mainLayout->addWidget(widget);
  }

  mainLayout->addStretch();

  const auto confirmationLayout = new QHBoxLayout();
  const auto confirmButton      = new QPushButton("Confirm", this);
  const auto cancelButton       = new QPushButton("Cancel", this);

  confirmButton->setDefault(true);

  connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

  confirmationLayout->addWidget(confirmButton);
  confirmationLayout->addWidget(cancelButton);

  mainLayout->addItem(confirmationLayout);
}
