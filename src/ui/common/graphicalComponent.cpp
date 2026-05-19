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

#include <stdexcept>

#include <utils/ranges_wrapper.hpp>

#include <ui/serialization/gui_component_factory.hpp>

#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <ui/common/theme.hpp>

namespace {

class ThemedPortLineItem : public QGraphicsLineItem {
public:
  using QGraphicsLineItem::QGraphicsLineItem;

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override
  {
    setPen(QPen(QBrush(ThemeEngine::getColor("SILICON_INK")), 3));
    QGraphicsLineItem::paint(painter, option, widget);
  }
};

}  // namespace

GraphicalComponent::GraphicalComponent(QGraphicsItem* shape, QGraphicsItem* parent,
                                       bool scanShape)
  : GraphicalComponent(ItemCategory::Component, shape, parent, scanShape)
{
}

GraphicalComponent::GraphicalComponent(ItemCategory category, QGraphicsItem* shape,
                                       QGraphicsItem* parent, bool scanShape)
  : GraphicalItem(category | ItemCategory::Component, parent)
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
    auto color = isColliding() ? Qt::red : ThemeEngine::getColor("SILICON_INK");
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
  GraphicalItem::mouseDoubleClickEvent(event);
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
  port->setLine(new ThemedPortLineItem(QLineF(portPos, projectionOnShape), this));
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

std::string GraphicalComponent::getTypeName() const
{
  return "Unknown";
}

nlohmann::ordered_json GraphicalComponent::serialize() const
{
  return {{"uiId", getUiId()},
          {"position", {{"x", pos().x()}, {"y", pos().y()}}},
          {"rotation", static_cast<int>(rotation())}};
}

std::unique_ptr<GraphicalComponent>
GraphicalComponent::deserialize(const nlohmann::json& j, GUIComponentFactory& factory)
{
  std::string type = j.value("type", "");
  if (type.empty()) {
    throw std::runtime_error("GraphicalComponent deserialization: missing 'type' field");
  }

  auto component = factory.create(type);
  if (!component) {
    throw std::runtime_error(
        std::format("GraphicalComponent deserialization: unknown type '{}'", type));
  }

  if (j.contains("position")) {
    const auto& posJson = j["position"];
    component->setPos(posJson.value("x", 0.0), posJson.value("y", 0.0));
  }

  if (j.contains("uiId"))
    component->setUiId(j["uiId"].get<uint64_t>());

  component->setRotation(j.value("rotation", 0.0));

  return component;
}
