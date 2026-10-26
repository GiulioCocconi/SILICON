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
#include <QGraphicsView>
#include <QLabel>
#include <QMarginsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTextOption>

#include <ui/shell/theme.hpp>
#include <ui/documents/documentNavigator.hpp>
#include <ui/serialization/gui_component_factory.hpp>
#include <utils/ranges_wrapper.hpp>


namespace SILICON::ui {
using namespace SILICON::core;

namespace {

constexpr qreal SelectionMargin = 4.0;
constexpr qreal SelectionPenWidth = 3.0;
constexpr qreal PortPenWidth = 3.0;
constexpr qreal PortErrorPenWidth = 4.0;
constexpr qreal SlashLength = 12.0;
constexpr qreal SlashPenWidth = 2.0;
constexpr qreal ClockIconWidth = 16.0;
constexpr qreal ClockIconHeight = 8.0;
constexpr qreal ClockIconPenWidth = 1.5;

class ThemedPortLineItem : public QGraphicsLineItem {
public:
  ThemedPortLineItem(const QLineF& line, QGraphicsItem* parent)
    : QGraphicsLineItem(line, parent)
  {
    setPen(QPen(Qt::black, PortPenWidth));
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override
  {
    auto themedPen = pen();
    themedPen.setColor(ThemeEngine::getColor("SILICON_INK"));
    painter->setPen(themedPen);
    painter->drawLine(line());
  }
};

QFont portMarkerFont(const QFont& baseFont)
{
  return QFont("NovaMono", baseFont.pointSize() * 0.6);
}

QRectF strokedLineRect(const QLineF& line, const qreal width)
{
  QPainterPath path;
  path.moveTo(line.p1());
  path.lineTo(line.p2());
  QPainterPathStroker stroker;
  stroker.setWidth(width);
  stroker.setCapStyle(Qt::SquareCap);
  return stroker.createStroke(path).boundingRect();
}

QLineF portSlash(const QLineF& line)
{
  QTransform transform;
  transform.translate(line.center().x(), line.center().y());
  transform.rotate(-line.angle() + 60.0);
  return transform.map(QLineF(-SlashLength / 2.0, 0, SlashLength / 2.0, 0));
}

QRectF portNameRect(const QLineF& line, const QString& name, const PortSide side,
                    const QFont& font)
{
  constexpr qreal     margin = DiagramScene::GRID_SIZE / 2.0;
  const QFontMetricsF metrics(font);
  const QString       label  = name.startsWith('!') ? name.mid(1) : name;
  const QPointF       point  = line.p2();
  const bool          clock  = name == QStringLiteral("clk");
  const qreal         width  = clock ? ClockIconWidth : metrics.horizontalAdvance(label);
  const qreal         height = clock ? ClockIconHeight : metrics.height();
  QMarginsF           padding;
  if (clock) {
    // Match the visible text edge, including font padding and the icon's stroke.
    const QRectF glyphs = metrics.tightBoundingRect(label);
    constexpr qreal halfPen = ClockIconPenWidth / 2.0;
    padding = QMarginsF(glyphs.left() + halfPen,
                        metrics.ascent() + glyphs.top() + halfPen,
                        metrics.horizontalAdvance(label) - glyphs.right() + halfPen,
                        metrics.height() - metrics.ascent() - glyphs.bottom() + halfPen);
  }
  switch (side) {
    case PortSide::DOWN:
      return {point.x() - width / 2.0,
              point.y() - margin / 1.5 - padding.bottom() - height, width, height};
    case PortSide::UP:
      return {point.x() - width / 2.0, point.y() + margin / 1.5 + padding.top(),
              width, height};
    case PortSide::LEFT:
      return {point.x() + margin + padding.left(), point.y() - height / 2.0,
              width, height};
    case PortSide::RIGHT:
      return {point.x() - margin - padding.right() - width,
              point.y() - height / 2.0, width, height};
  }
  std::unreachable();
}

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

  if (itemShape == shape)
    return;

  prepareGeometryChange();
  if (this->itemShape)
    delete std::exchange(this->itemShape, nullptr);

  this->itemShape = shape;
  this->itemShape->setParentItem(this);
  update();
}

QRectF GraphicalComponent::boundingRect() const
{
  return boundingRectWithoutMargins().adjusted(-SelectionMargin, -SelectionMargin,
                                              SelectionMargin, SelectionMargin);
}

QRectF GraphicalComponent::boundingRectWithoutMargins() const
{
  return childrenBoundingRect();
}

void GraphicalComponent::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                               QWidget* widget)
{
  if (isSelected()) {
    auto color = isColliding() ? Qt::red : ThemeEngine::getColor("SILICON_INK");
    auto style = isColliding() ? Qt::SolidLine : Qt::DotLine;

    painter->setPen(QPen(color, SelectionPenWidth, style));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(this->boundingRectWithoutMargins());
  }
}

QRectF GraphicalComponent::collisionRectForWires() const
{
  QRectF rect = itemShape ? itemShape->mapRectToParent(itemShape->boundingRect())
                         : QRectF{};

  for (const auto port : getPorts())
    rect = rect.united(port->mapRectToParent(port->collisionRect()));

  return rect;
}

void GraphicalComponent::rotate()
{
  setRotation(rotation() + 90);
  update();
}

QRectF GraphicalComponent::getCollisionRect() const
{
  QRectF rect = itemShape ? itemShape->mapRectToParent(itemShape->boundingRect())
                         : QRectF{};
  for (const auto* port : getPorts()) {
    if (port->line)
      rect = rect.united(port->line->mapRectToParent(port->line->boundingRect()));
  }
  return rect;
}

void GraphicalComponent::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  GraphicalItem::mouseDoubleClickEvent(event);
}

bool GraphicalComponent::openDocumentOnDoubleClick(QGraphicsSceneMouseEvent* event,
                                                   const std::string& path)
{
  auto* diagram = qobject_cast<DiagramScene*>(scene());
  if (event->button() != Qt::LeftButton || !diagram
      || diagram->getInteractionMode() != InteractionMode::NORMAL_MODE
      || !diagram->getDocumentNavigator())
    return false;

  // Activating another circuit can delete this item. Wait until event dispatch ends.
  QMetaObject::invokeMethod(
      this,
      [this, path] {
        if (auto* diagram = qobject_cast<DiagramScene*>(scene())) {
          if (auto* navigator = diagram->getDocumentNavigator())
            navigator->activateDocument(path);
        }
      },
      Qt::QueuedConnection);
  event->accept();
  return true;
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
  if (auto* diagram = qobject_cast<DiagramScene*>(scene()))
    diagram->requestConnectionsRefresh();
}

void GraphicalComponent::clearPorts()
{
  if (auto* diagram = qobject_cast<DiagramScene*>(scene()))
    diagram->requestConnectionsRefresh();
  prepareGeometryChange();

  for (Port* port : getPorts()) {
    if (!port)
      continue;

    port->prepareGeometryChange();
    delete std::exchange(port->line, nullptr);
    delete port;
  }
  inputPorts.clear();
  outputPorts.clear();
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
  prepareGeometryChange();
  port->side = portSide;
  port->printName = printPortNames;
  port->setLine(new ThemedPortLineItem(QLineF(portPos, projectionOnShape), this));

  if (!printPortNames) {
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

QLineF Port::connectionLine() const
{
  const auto portLine = line->line();
  return {mapFromItem(line, portLine.p1()), mapFromItem(line, portLine.p2())};
}

void Port::prepareBoundsChange()
{
  // The parent paints its selection outline around the accumulated child bounds.
  if (auto* component = dynamic_cast<GraphicalComponent*>(parentItem()))
    component->prepareGeometryChange();
  prepareGeometryChange();
}

QRectF Port::boundingRect() const
{
  if (!line)
    return {};

  const QLineF portLine = connectionLine();
  // Reserve the error stroke even while it is hidden, keeping bounds stable.
  QRectF rect = mapRectFromItem(line, line->boundingRect())
                    .united(strokedLineRect(portLine, PortErrorPenWidth));
  if (!printName && size == 1)
    return rect;

  const auto includeLabelBounds = [&](const QFont& baseFont) {
    const QFont markerFont = portMarkerFont(baseFont);
    if (printName) {
      QRectF labelRect = portNameRect(portLine, name, side, markerFont);
      if (name == QStringLiteral("clk")) {
        constexpr qreal halfPen = ClockIconPenWidth / 2.0;
        labelRect.adjust(-halfPen, -halfPen, halfPen, halfPen);
      }
      rect = rect.united(labelRect);
    }
    if (size != 1) {
      const QFontMetricsF metrics(markerFont);
      const QRectF        textRect =
          metrics.boundingRect(QString::number(size)).adjusted(-4.0, -2.0, 4.0, 2.0);
      rect = rect.united(portSizeLabelRect(portLine.center(), textRect, side))
                 .united(strokedLineRect(portSlash(portLine), SlashPenWidth));
    }
  };

  // Each view's painter inherits its viewport font. Measure those same fonts.
  const auto views = scene() ? scene()->views() : QList<QGraphicsView*>{};
  if (views.isEmpty())
    includeLabelBounds(QApplication::font());
  else
    for (const auto* view : views)
      includeLabelBounds(view->viewport()->font());

  return rect;
}

void Port::setLine(QGraphicsLineItem* line)
{
  prepareBoundsChange();
  if (line->parentItem() != parentItem()) {
    if (auto* component = dynamic_cast<GraphicalComponent*>(line->parentItem()))
      component->prepareGeometryChange();
  }
  this->line = line;
  setParentItem(line->parentItem());
  setZValue(line->zValue() + 1.0);
}

void Port::setSize(const unsigned int newSize)
{
  const unsigned int normalizedSize = std::max(1U, newSize);
  if (size == normalizedSize)
    return;

  prepareBoundsChange();
  size = normalizedSize;
  update();
  if (parentItem())
    parentItem()->update();
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

  const bool   isBusPort  = (size != 1);
  const QLineF portLine   = connectionLine();
  const QFont  markerFont = portMarkerFont(painter->font());
  const QColor portColor =
      inputAssignmentError ? Qt::red : ThemeEngine::getColor("SILICON_INK");

  if (inputAssignmentError) {
    painter->save();
    painter->setPen(QPen(portColor, PortErrorPenWidth, Qt::SolidLine));
    painter->drawLine(portLine);
    painter->restore();
  }

  if (printName) {
    const bool        inverted  = name.startsWith('!');
    const QString     label     = inverted ? name.mid(1) : name;
    const QRectF      labelRect = portNameRect(portLine, name, side, markerFont);
    const QTextOption textOption(Qt::AlignCenter);

    painter->save();
    painter->setFont(markerFont);
    painter->setPen(QPen(portColor));
    if (name == QStringLiteral("clk")) {
      // A rising edge: low on the left, high on the right.
      QPainterPath clock;
      clock.moveTo(labelRect.left(), labelRect.bottom());
      clock.lineTo(labelRect.center().x(), labelRect.bottom());
      clock.lineTo(labelRect.center().x(), labelRect.top());
      clock.lineTo(labelRect.right(), labelRect.top());
      painter->setPen(QPen(portColor, ClockIconPenWidth, Qt::SolidLine,
                          Qt::FlatCap, Qt::MiterJoin));
      painter->setBrush(Qt::NoBrush);
      painter->drawPath(clock);
    } else {
      painter->drawText(labelRect, label, textOption);
    }
    if (inverted && !label.isEmpty()) {
      const QFontMetricsF metrics(markerFont, painter->device());
      const QRectF textBounds = painter->boundingRect(labelRect, label, textOption);
      // Follow the visible glyph tops instead of the font's high overline position.
      const qreal y = textBounds.top() + metrics.ascent()
                      + metrics.tightBoundingRect(label).top() - metrics.lineWidth()
                      - 4.5;
      painter->setPen(QPen(portColor, metrics.lineWidth(), Qt::SolidLine, Qt::FlatCap));
      painter->drawLine(QLineF(textBounds.left(), y, textBounds.right(), y));
    }
    painter->restore();
  }

  if (isBusPort) {
    const QString       sizeText = QString::number(size);
    const QFontMetricsF metrics(markerFont);

    painter->save();
    painter->setPen(QPen(portColor, SlashPenWidth));
    painter->drawLine(portSlash(portLine));
    painter->restore();

    const QRectF textRect = metrics.boundingRect(sizeText).adjusted(-4.0, -2.0, 4.0, 2.0);
    const QRectF labelRect = portSizeLabelRect(portLine.center(), textRect, side);

    painter->save();
    painter->setFont(markerFont);
    painter->setPen(portColor);
    painter->drawText(labelRect, sizeText, QTextOption(Qt::AlignCenter));
    painter->restore();
  }
}

QRectF Port::collisionRect() const
{
  if (!line)
    return {};

  // Decorations describe the bus; they must not obstruct a wire reaching its tip.
  QPainterPath boundingPath{};
  boundingPath.addRect(mapRectFromItem(line, line->boundingRect()));

  QPainterPath subtract{};
  subtract.addEllipse(connectionLine().p1(), 5, 5);

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

}  // namespace SILICON::ui
