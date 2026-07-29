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

#include "graphicalComponent.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <QDialog>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QTextOption>

#include <ui/common/theme.hpp>
#include <ui/serialization/gui_component_factory.hpp>
#include <utils/ranges_wrapper.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

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

QRectF portSizeLabelRect(const QPointF& midpoint, const QRectF& textRect,
                         const PortSide direction)
{
  if (direction == PortSide::UP || direction == PortSide::DOWN) {
    return {midpoint.x() - textRect.width(), midpoint.y() - textRect.height() / 2.0,
            textRect.width(), textRect.height()};
  }

  return {midpoint.x() - textRect.width() / 2.0, midpoint.y() - textRect.height(),
          textRect.width(), textRect.height()};
}

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

void GraphicalComponent::setPorts(const std::vector<PortPair>& busToPortInputs,
                                  const std::vector<PortPair>& busToPortOutputs)
{
  clearPorts();

  for (const auto& [index, pair] : busToPortInputs | SILICON::views::enumerate) {
    const auto& [name, pos] = pair;

    // The memory is not leaked since the port address is freed by Qt Garbage collector
    // ReSharper disable once CppDFAMemoryLeak
    auto p = new Port(index, pos, name);
    this->inputPorts.push_back(p);
    this->setPortLine(p);
  }

  for (const auto& [index, pair] : busToPortOutputs | SILICON::views::enumerate) {
    const auto& [name, pos] = pair;

    // ReSharper disable once CppDFAMemoryLeak
    auto p = new Port(index, pos, name);
    this->outputPorts.push_back(p);
    this->setPortLine(p);
  }
}

void GraphicalComponent::clearPorts()
{
  prepareGeometryChange();

  auto deletePorts = [](std::vector<Port*>& ports) {
    for (Port* port : ports) {
      if (!port)
        continue;

      delete port->line;
      delete port;
    }
    ports.clear();
  };

  deletePorts(inputPorts);
  deletePorts(outputPorts);
}

QPoint GraphicalComponent::scanImage(const QImage& image, const PortSide side,
                                     const QPoint initialPoint) const
{
  if (!this->scanShape) {
    return initialPoint;
  }

  const bool horizontal = side == PortSide::LEFT || side == PortSide::RIGHT;
  const int  direction  = (side == PortSide::LEFT || side == PortSide::UP) ? 1 : -1;

  const int initialCoord = horizontal ? initialPoint.x() : initialPoint.y();

  const QPoint topLeft     = image.rect().topLeft();
  const QPoint bottomRight = image.rect().bottomRight();

  const int leftUp    = horizontal ? topLeft.x() : topLeft.y();
  const int rightDown = horizontal ? bottomRight.x() : bottomRight.y();

  for (qreal coord = initialCoord; (direction > 0) ? coord <= rightDown : coord >= leftUp;
       coord += direction) {
    auto p =
        horizontal ? QPoint(coord, initialPoint.y()) : QPoint(initialPoint.x(), coord);

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

  const auto portPos  = port->getPosition();
  const auto portRect = shapeRect.toRect();
  // nearestPortSide also handles interior points for editor drag sanitization,
  // but component ports must already satisfy the exterior-position invariant.
  if (!isPortPositionOutside(portPos, portRect))
    throw std::logic_error("setPortLine: port position is not outside the shape");

  const auto [portSide, naiveProjection] = nearestPortSide(portPos, portRect);

  const QPoint projectionOnShape = scanImage(image, portSide, naiveProjection);

  // Create the line from port position to the projection
  port->side = portSide;
  port->setLine(new ThemedPortLineItem(QLineF(portPos, projectionOnShape), this));

  if (printPortNames) {
    port->printName = true;
  } else {
    port->setToolTip(port->name);
  }
}

Port::Port(const unsigned int index, const QPoint position, QString name, bool printName,
           QGraphicsItem* parent)
  : QGraphicsItem(parent)
{
  this->index     = index;
  this->position  = position;
  this->printName = printName;
  this->name      = std::move(name);
}

QRectF Port::boundingRect() const
{
  if (!line)
    return {};

  QRectF rect = line->boundingRect();
  if (size == 1)
    return rect;

  const QLineF  portLine = line->line();
  const QPointF midpoint((portLine.p1().x() + portLine.p2().x()) / 2.0,
                         (portLine.p1().y() + portLine.p2().y()) / 2.0);

  const QFontMetricsF metrics(QFont("NovaMono"));
  const QRectF        labelRect =
      metrics.boundingRect(QString::number(size)).adjusted(-4.0, -2.0, 4.0, 2.0);
  rect = rect.united(portSizeLabelRect(midpoint, labelRect, side));

  return rect;
}

void Port::setLine(QGraphicsLineItem* line)
{
  prepareGeometryChange();
  this->line = line;
  setParentItem(line->parentItem());
  setZValue(line->zValue() + 1.0);
}

void Port::setSize(const unsigned int newSize)
{
  const unsigned int normalizedSize = std::max(1U, newSize);
  if (size == normalizedSize)
    return;

  prepareGeometryChange();
  size = normalizedSize;
  update();
}

void Port::setInputAssignmentError(const bool failed)
{
  if (inputAssignmentError == failed)
    return;

  inputAssignmentError = failed;
  update();
  if (line)
    line->update();
}

void Port::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                 QWidget* widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  if (!line)
    return;

  const bool          isBusPort = (size != 1);
  const QLineF        portLine  = line->line();
  const QFont         markerFont("NovaMono", painter->font().pointSize() * 0.6);
  const QFontMetricsF metrics(markerFont);
  const QColor        portColor =
      inputAssignmentError ? Qt::red : ThemeEngine::getColor("SILICON_INK");

  if (inputAssignmentError) {
    painter->save();
    painter->setPen(QPen(portColor, 4.0, Qt::SolidLine));
    painter->drawLine(portLine);
    painter->restore();
  }

  if (printName) {
    auto getNameRect = [portLine, metrics, this]() -> QRectF {
      constexpr qreal margin = DiagramScene::GRID_SIZE / 2.0;
      const QPointF   linePt = portLine.p2();

      const qreal textWidth  = metrics.horizontalAdvance(name);
      const qreal textHeight = metrics.height();

      switch (this->side) {
        case PortSide::DOWN:
          return {linePt.x() - textWidth / 2.0, linePt.y() - margin / 1.5 - textHeight,
                  textWidth, textHeight};
        case PortSide::UP:
          return {linePt.x() - textWidth / 2.0, linePt.y() + margin / 1.5, textWidth,
                  textHeight};
        case PortSide::LEFT:
          return {linePt.x() + margin, linePt.y() - textHeight / 2.0, textWidth,
                  textHeight};
        case PortSide::RIGHT:
          return {linePt.x() - margin - textWidth, linePt.y() - textHeight / 2.0,
                  textWidth, textHeight};
      }
      std::unreachable();
    };

    painter->save();
    painter->setFont(markerFont);
    painter->setPen(QPen(portColor));
    painter->drawText(getNameRect(), name, QTextOption(Qt::AlignCenter));
    painter->restore();
  }

  if (isBusPort) {
    constexpr qreal slashLength = 12.0;
    constexpr qreal slashAngle  = 60.0;

    const QPointF midpoint((portLine.p1().x() + portLine.p2().x()) / 2.0,
                           (portLine.p1().y() + portLine.p2().y()) / 2.0);
    const qreal   lineAngle = portLine.angle();
    const QString sizeText  = QString::number(size);

    painter->save();
    painter->setPen(QPen(portColor, 2.0));
    painter->setFont(markerFont);

    painter->translate(midpoint);
    painter->rotate(-lineAngle + slashAngle);
    painter->drawLine(QPointF(-slashLength / 2.0, 0), QPointF(slashLength / 2.0, 0));
    painter->restore();

    const QRectF textRect = metrics.boundingRect(sizeText).adjusted(-4.0, -2.0, 4.0, 2.0);
    const QRectF labelRect = portSizeLabelRect(midpoint, textRect, side);

    painter->save();
    painter->setFont(markerFont);
    painter->setPen(portColor);
    painter->drawText(labelRect, sizeText, QTextOption(Qt::AlignCenter));
    painter->restore();
  }
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
{ return "Unknown"; }

nlohmann::ordered_json GraphicalComponent::serialize() const
{
  return {{"uiId", getUiId()},
          {"position", {{"x", pos().x()}, {"y", pos().y()}}},
          {"rotation", static_cast<int>(rotation())}};
}

void GraphicalComponent::loadSerializedState(const nlohmann::json& j)
{ Q_UNUSED(j); }

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
  component->loadSerializedState(j);

  return component;
}

}  // namespace ui
}  // namespace SILICON
