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

#include "graphicalIO.hpp"

#include <core/simulator.hpp>
#include <core/wireUtils.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/theme.hpp>
#include <utils/num_formatting.hpp>

#include <QPointer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

constexpr int              BusIoMinWidth     = 8 * DiagramScene::GRID_SIZE;
constexpr int              BusIoHeight       = 4 * DiagramScene::GRID_SIZE;
constexpr int              BusIoPortX        = 0;
constexpr int              BusIoPortY        = 6 * DiagramScene::GRID_SIZE;
constexpr int              BusIoPaddingX     = DiagramScene::GRID_SIZE;
constexpr int              BusIoValueTextGap = 8;
constexpr int              MaxInlineValueCharacters = 18;
constexpr int              IoPortExtension         = 2 * DiagramScene::GRID_SIZE;
constexpr int              ConstantSize             = 4 * DiagramScene::GRID_SIZE;
constexpr int              ConstantPortExtension    = 2 * DiagramScene::GRID_SIZE;
constexpr std::string_view PortOrientationProperty = "portOrientation";
constexpr std::string_view PortOrientationUp       = "UP";
constexpr std::string_view PortOrientationDown     = "DOWN";
constexpr std::string_view PortOrientationLeft     = "LEFT";
constexpr std::string_view PortOrientationRight    = "RIGHT";
constexpr std::string_view DefaultPortOrientation  = PortOrientationDown;

// Re-use static fonts to prevent allocations
const QFont& getWidthFont()
{
  static const QFont font("NovaMono", 8);
  return font;
}

QRectF busIoNameRect(const QRectF& shapeRect, const QString& name)
{
  const QFontMetrics metrics(GraphicalIO::UI_FONT);
  const qreal width = std::max<qreal>(shapeRect.width(), metrics.horizontalAdvance(name));
  const qreal height = metrics.height();
  return {shapeRect.right() - width, shapeRect.top() - height, width, height};
}

QRectF busIoNameRect(const QRectF& shapeRect, const QString& name, const bool below)
{
  QRectF rect = busIoNameRect(shapeRect, name);
  if (below)
    rect.moveTop(shapeRect.bottom());
  return rect;
}

int snapBusIoWidthToGrid(const int width)
{
  return std::max(BusIoMinWidth, ((width + 2 * DiagramScene::GRID_SIZE - 1)
                                  / (2 * DiagramScene::GRID_SIZE))
                                     * (2 * DiagramScene::GRID_SIZE));
}

int normalizedBusSize(const int size)
{
  return std::clamp(size, 1,
                    static_cast<int>(std::numeric_limits<unsigned short>::max()));
}

QString compactValueText(QString value)
{
  if (value.size() <= MaxInlineValueCharacters)
    return value;
  return value.left(9) + QStringLiteral("…") + value.right(8);
}

QString compactValueText(const BusValue& value, const BusValueFormat format,
                         const std::size_t fixedWidth = 0)
{
  return compactValueText(QString::fromStdString(formatValue(value, format, fixedWidth)));
}

bool isInteractiveSimulation(const GraphicalItem* item)
{
  const auto* diagramScene =
      dynamic_cast<const DiagramScene*>(item ? item->scene() : nullptr);
  return diagramScene
         && diagramScene->getInteractionMode()
                == DiagramScene::InteractionMode::SIMULATION_MODE;
}

enum class BusIoKind { Input, Output };

enum class IoPortOrientation { Up, Down, Left, Right };

class BusIoShape : public QGraphicsItem {
public:
  BusIoShape(BusIoKind kind, unsigned int busWidth, QGraphicsItem* parent = nullptr)
    : QGraphicsItem(parent), kind(kind), busWidth(busWidth)
  {
    updateLayoutCache();  // Initial cache generation
  }

  QRectF boundingRect() const override
  {
    // Zero-cost geometric return - no text allocation!
    return {-cachedWidth / 2.0, 0, cachedWidth, BusIoHeight};
  }

  void setBusWidth(const unsigned int width)
  {
    if (busWidth == width)
      return;
    prepareGeometryChange();
    busWidth = width;
    updateLayoutCache();  // Calculate width ONLY when width actually changes
    update();
  }

  void setValueText(QString text)
  {
    if (valueText == text)
      return;
    valueText = std::move(text);
    update();
  }

  void setDisplayState(const State state)
  {
    displayState = state;
    update();
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
             QWidget* /*widget*/) override
  {
    const QColor ink = ThemeEngine::getColor("SILICON_INK");

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(ink, 3));
    painter->setBrush(backgroundColor());
    const QRectF body = boundingRect().adjusted(1.5, 1.5, -1.5, -1.5);
    painter->drawRect(body);

    painter->setPen(ink);
    painter->setFont(GraphicalIO::UI_FONT);
    painter->drawText(QRectF(body.left() + BusIoPaddingX, 3,
                             body.width() - 2 * BusIoPaddingX - cachedWidthTextWidth
                                 - BusIoValueTextGap,
                             34),
                      Qt::AlignCenter, valueText);

    painter->setFont(getWidthFont());
    painter->drawText(QRectF(body.right() - BusIoPaddingX - cachedWidthTextWidth, 3,
                             cachedWidthTextWidth, 16),
                      Qt::AlignCenter, cachedWidthText);
  }

private:
  void updateLayoutCache()
  {
    const QFontMetrics valueMetrics(GraphicalIO::UI_FONT);
    const QFontMetrics widthMetrics(getWidthFont());

    cachedWidthText      = QString("[%1]").arg(busWidth);
    cachedWidthTextWidth = widthMetrics.horizontalAdvance(cachedWidthText);

    const QString largestKnown =
        busWidth <= 64
            ? compactValueText(maxValueForBusWidth(busWidth), BusValueFormat::Hex)
            : QStringLiteral("0xFFFFFFFF…FFFFFFFF");
    const QString unknownBits(static_cast<int>(std::min<unsigned int>(busWidth, 8)),
                              QLatin1Char(std::to_underlying(State::UNKNOWN)));

    int maxAdvance = valueMetrics.horizontalAdvance(largestKnown);
    for (const QString& candidate :
         {unknownBits, QStringLiteral("ERR"), QStringLiteral("BUS")}) {
      maxAdvance = std::max(maxAdvance, valueMetrics.horizontalAdvance(candidate));
    }

    const int contentWidth =
        2 * BusIoPaddingX + maxAdvance + BusIoValueTextGap + cachedWidthTextWidth;
    cachedWidth = snapBusIoWidthToGrid(contentWidth);
  }

  QColor backgroundColor() const
  {
    if (kind == BusIoKind::Input)
      return ThemeEngine::getColor("SILICON_INTERNAL");

    switch (displayState) {
      case State::HIGH: return ThemeEngine::getColor("SILICON_BLUE");
      case State::LOW: return QColor("#ececec");
      case State::UNKNOWN: return ThemeEngine::getColor("SILICON_VIOLET");
      case State::ERROR: return Qt::red;
    }
    return Qt::magenta;
  }

  BusIoKind    kind;
  unsigned int busWidth     = 1;
  QString      valueText    = "0x0";
  State        displayState = State::UNKNOWN;

  // Cached dimension metrics
  qreal   cachedWidth = BusIoMinWidth;
  QString cachedWidthText;
  int     cachedWidthTextWidth = 0;
};

class ConstantShape : public QGraphicsItem {
public:
  QRectF boundingRect() const override
  {
    return {0, 0, static_cast<qreal>(cachedWidth), static_cast<qreal>(ConstantSize)};
  }

  void setValue(QString newValue)
  {
    if (value == newValue)
      return;
    prepareGeometryChange();
    value                  = std::move(newValue);
    const QString label    = displayValue();
    const int contentWidth = QFontMetrics(GraphicalIO::UI_FONT).horizontalAdvance(label)
                             + 2 * DiagramScene::GRID_SIZE;
    cachedWidth = std::max(ConstantSize, ((contentWidth + 2 * DiagramScene::GRID_SIZE - 1)
                                          / (2 * DiagramScene::GRID_SIZE))
                                             * (2 * DiagramScene::GRID_SIZE));
    update();
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
             QWidget* /*widget*/) override
  {
    const QColor ink = ThemeEngine::getColor("SILICON_INK");

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(ink, 3));
    painter->setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
    painter->drawRect(boundingRect().adjusted(1.5, 1.5, -1.5, -1.5));

    painter->setPen(ink);
    painter->setFont(GraphicalIO::UI_FONT);
    painter->drawText(boundingRect(), Qt::AlignCenter, displayValue());
  }

private:
  [[nodiscard]] QString displayValue() const
  {
    return (value.size() > 1 ? QStringLiteral("0b") : QString()) + value.toUpper();
  }

  QString value       = QStringLiteral("0");
  int     cachedWidth = ConstantSize;
};

ConstantShape* getConstantShape(QGraphicsItem* itemShape, const char* context)
{
  auto* shape = dynamic_cast<ConstantShape*>(itemShape);
  Q_ASSERT_X(shape != nullptr, context, "Constant item is missing its ConstantShape");
  return shape;
}

BusIoShape* getBusIoShape(QGraphicsItem* itemShape, const char* context)
{
  auto* shape = dynamic_cast<BusIoShape*>(itemShape);
  Q_ASSERT_X(shape != nullptr, context, "Bus IO item is missing its BusIoShape");
  return shape;
}

QRectF busIoNamedBounds(QGraphicsItem* itemShape, const QString& name,
                        const QRectF& fallbackRect, const bool nameBelow)
{
  if (name.isEmpty())
    return fallbackRect;

  const auto* shape = getBusIoShape(itemShape, "busIoNamedBounds");
  if (!shape)
    return fallbackRect;

  return fallbackRect.united(busIoNameRect(shape->boundingRect(), name, nameBelow));
}

IoPortOrientation parsePortOrientation(const std::string_view orientation)
{
  if (orientation == PortOrientationUp)
    return IoPortOrientation::Up;
  if (orientation == PortOrientationLeft)
    return IoPortOrientation::Left;
  if (orientation == PortOrientationRight)
    return IoPortOrientation::Right;
  return IoPortOrientation::Down;
}

bool portNameBelongsBelowShape(const std::string_view orientation)
{
  return parsePortOrientation(orientation) == IoPortOrientation::Up;
}

std::string currentPortOrientation(const Component_ptr& component)
{
  if (!component)
    return std::string(DefaultPortOrientation);

  return component->getPropertyValue<std::string>(PortOrientationProperty)
      .value_or(std::string(DefaultPortOrientation));
}

QPoint ioPortPosition(const QRectF& shapeRect, const IoPortOrientation orientation)
{
  const int centerX = DiagramScene::snapToGrid(shapeRect.center().x());
  const int centerY = DiagramScene::snapToGrid(shapeRect.center().y());

  switch (orientation) {
    case IoPortOrientation::Up:
      return {centerX, DiagramScene::snapToGrid(shapeRect.top() - IoPortExtension)};
    case IoPortOrientation::Down:
      return {centerX, DiagramScene::snapToGrid(shapeRect.bottom() + IoPortExtension)};
    case IoPortOrientation::Left:
      return {DiagramScene::snapToGrid(shapeRect.left() - IoPortExtension), centerY};
    case IoPortOrientation::Right:
      return {DiagramScene::snapToGrid(shapeRect.right() + IoPortExtension), centerY};
  }
  std::unreachable();
}

std::string_view portOrientationName(const PortSide side)
{
  switch (side) {
    case PortSide::UP: return PortOrientationUp;
    case PortSide::DOWN: return PortOrientationDown;
    case PortSide::LEFT: return PortOrientationLeft;
    case PortSide::RIGHT: return PortOrientationRight;
  }
  std::unreachable();
}

}  // namespace

// --- Constant
// -------------------------------------------------------------------------

GraphicalConstant::GraphicalConstant(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<ConstantComponent>(std::make_shared<Wire>(),
                                                                BusValue{State::LOW}),
                            new ConstantShape(), parent)
{
  isEditable = true;
  setupCallbacks();
  updateLayout();
}

void GraphicalConstant::setupCallbacks()
{
  if (!associatedComponent)
    return;

  QPointer<GraphicalConstant> safeThis(this);
  std::weak_ptr<Component>    boundComponent = associatedComponent;
  associatedComponent->setPropertyCallback(
      "value", [safeThis, boundComponent](const PropertyValue& value) {
        auto       component = boundComponent.lock();
        auto*      constant  = dynamic_cast<ConstantComponent*>(component.get());
        const auto applied   = constant ? constant->setValue(std::get<BusValue>(value))
                                        : std::get<BusValue>(value);
        if (safeThis && safeThis->getComponent() == component)
          safeThis->updateLayout(applied);
        return applied;
      });

  associatedComponent->setPropertyCallback(
      "size", [safeThis, boundComponent](const PropertyValue& value) {
        auto      component = boundComponent.lock();
        auto*     constant  = dynamic_cast<ConstantComponent*>(component.get());
        const int applied =
            constant ? constant->setSize(std::get<int>(value)) : std::get<int>(value);
        if (safeThis && safeThis->getComponent() == component)
          safeThis->updateLayout();
        return applied;
      });
}

void GraphicalConstant::updateLayout()
{
  if (!associatedComponent)
    return;

  const auto value = associatedComponent->getPropertyValue<BusValue>("value").value_or(
      BusValue{State::UNKNOWN});
  updateLayout(value);
}

void GraphicalConstant::updateLayout(const BusValue& value)
{
  auto* shape = getConstantShape(getItemShape(), "GraphicalConstant::updateLayout");
  if (!shape)
    return;

  prepareGeometryChange();
  shape->setValue(compactValueText(value, BusValueFormat::Raw));
  const QRectF bounds = shape->boundingRect();
  setPorts({},
           {PortPair{"o", QPoint(static_cast<int>(bounds.right()) + ConstantPortExtension,
                                 static_cast<int>(bounds.center().y()))}});
}

void GraphicalConstant::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  setupCallbacks();
  updateLayout();
}

GraphicalIO::GraphicalIO(ItemCategory category, const Component_ptr& component,
                         QGraphicsItem* shape, QGraphicsItem* parent, bool scanShape)
  : GraphicalLogicComponent(category | ItemCategory::IO, component, shape, parent,
                            scanShape)
{
}

QString GraphicalIO::getComponentName() const
{
  const auto nameProperty = getComponent()->getProperty("name");
  if (nameProperty.has_value()) {
    if (const auto* name = std::get_if<std::string>(&*nameProperty)) {
      return QString::fromStdString(*name);
    }
  }
  return {};
}

void GraphicalIO::installPortOrientationCallback()
{
  if (!associatedComponent || !associatedComponent->getProperty(PortOrientationProperty))
    return;

  QPointer<GraphicalIO>    safeThis(this);
  std::weak_ptr<Component> boundComponent = associatedComponent;

  associatedComponent->setPropertyCallback(
      PortOrientationProperty, [safeThis, boundComponent](const PropertyValue& value) {
        auto component = boundComponent.lock();
        if (safeThis && safeThis->getComponent() == component) {
          safeThis->prepareGeometryChange();
          if (const auto* orientation = std::get_if<std::string>(&value))
            safeThis->updatePortOrientation(*orientation);
        }
        return value;
      });
}

void GraphicalIO::updatePortOrientation()
{
  if (!associatedComponent || !getItemShape())
    return;

  updatePortOrientation(currentPortOrientation(associatedComponent));
}

void GraphicalIO::updatePortOrientation(const std::string_view orientation)
{
  if (!associatedComponent || !getItemShape())
    return;

  const QPoint position =
      ioPortPosition(getItemShape()->boundingRect(), parsePortOrientation(orientation));

  if (!associatedComponent->getInputs().empty()) {
    const QString name =
        inputPorts.empty() ? QStringLiteral("in") : inputPorts.front()->getName();
    GraphicalLogicComponent::setPorts({PortPair{name, position}}, {});
  } else if (!associatedComponent->getOutputs().empty()) {
    const QString name =
        outputPorts.empty() ? QStringLiteral("o") : outputPorts.front()->getName();
    GraphicalLogicComponent::setPorts({}, {PortPair{name, position}});
  }
  update();
}

bool GraphicalIO::isPortOrientationUp() const
{
  if (!associatedComponent)
    return false;

  return portNameBelongsBelowShape(currentPortOrientation(associatedComponent));
}

QRectF GraphicalIO::componentNameRect(const QString& name) const
{
  if (!getItemShape() || name.isEmpty())
    return {};

  const QRectF       shapeRect = getItemShape()->boundingRect();
  const QFontMetrics metrics(UI_FONT);
  const qreal width = std::max<qreal>(shapeRect.width(), metrics.horizontalAdvance(name));
  const qreal height = metrics.height();
  const qreal x      = shapeRect.left();
  const qreal y = isPortOrientationUp() ? shapeRect.bottom() : shapeRect.top() - height;
  return {x, y, width, height};
}

QPoint GraphicalIO::portPositionFor(const PortSide side) const
{
  if (!getItemShape())
    return {};
  return ioPortPosition(getItemShape()->boundingRect(),
                        parsePortOrientation(portOrientationName(side)));
}

QRectF GraphicalIO::collisionRectForPortSide(const PortSide side) const
{
  if (!getItemShape())
    return {};
  const QPoint portPosition = portPositionFor(side);
  return getItemShape()->boundingRect().united(
      QRectF(portPosition - QPoint(1, 1), QSize(2, 2)));
}

void GraphicalIO::setPortOrientation(const PortSide side)
{
  if (!associatedComponent)
    return;
  associatedComponent->setPropertyValue(std::string(PortOrientationProperty),
                                        std::string(portOrientationName(side)));
}

void GraphicalIO::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  installPortOrientationCallback();
  updatePortOrientation();
}

// --- Graphical Input Single
// -------------------------------------------------------------

GraphicalInput::GraphicalInput(QGraphicsItem* parent)
  : GraphicalIO(ItemCategory::Input, std::make_shared<DummyInputComponent>(Bus(1), "in"),
                new QGraphicsSvgItem(":/other_components/input_off.svg"), parent)
{
  isEditable = false;
  GraphicalLogicComponent::setPorts({}, {PortPair{"o", QPoint(20, 60)}});
  installPortOrientationCallback();

  associatedComponent->setPropertyCallback("name", [this](const PropertyValue& value) {
    prepareGeometryChange();
    return value;
  });

  GraphicalInput::applyStartValue();
}

void GraphicalInput::toggle()
{
  setState(!skinState);
}

void GraphicalInput::setState(State state)
{
  skinState = state;
  setItemShape(new QGraphicsSvgItem((skinState == State::HIGH) ? getOnShapePath()
                                                               : getOffShapePath()));

  const auto targetBus = getComponent()->getOutputs()[0];
  if (!isInteractiveSimulation(this)) {
    if (auto* input = dynamic_cast<DummyInputComponent*>(getComponent().get()))
      input->setState(state);
  }

  emit inputToggled(targetBus, BusValue{state}, getComponent()->weak_from_this());
}

void GraphicalInput::handleSimulationClick()
{
  toggle();
}

void GraphicalInput::applyStartValue()
{
  const auto startValue = getComponent()
                              ->getPropertyValue<BusValue>("startValue")
                              .value_or(BusValue{State::LOW});
  skinState = startValue.empty() ? State::LOW : startValue.front();
  setItemShape(new QGraphicsSvgItem((skinState == State::HIGH) ? getOnShapePath()
                                                               : getOffShapePath()));

  if (auto* input = dynamic_cast<DummyInputComponent*>(getComponent().get()))
    input->setState(skinState);
}

void GraphicalInput::resetSimulationState()
{
  applyStartValue();
}

void GraphicalInput::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                           QWidget* widget)
{
  painter->setFont(UI_FONT);
  const QString name = getComponentName();
  if (!name.isEmpty()) {
    painter->drawText(componentNameRect(name), Qt::AlignLeft | Qt::AlignVCenter, name);
  }
  GraphicalLogicComponent::paint(painter, option, widget);
}

QRectF GraphicalInput::boundingRect() const
{
  auto rect = GraphicalLogicComponent::boundingRect();
  return rect.united(componentNameRect(getComponentName()));
}

State GraphicalInput::getState() const
{
  return getComponent()->getOutputs()[0][0]->getCurrentState();
}

// --- Graphical Bus Input ---------------------------------------------------------------

GraphicalBusInput::GraphicalBusInput(QGraphicsItem* parent)
  : GraphicalIO(ItemCategory::Input,
                std::make_shared<DummyBusInputComponent>(Bus(8), "bus_in"),
                new BusIoShape(BusIoKind::Input, 8), parent)
{
  isEditable = false;
  GraphicalLogicComponent::setPorts({},
                                    {PortPair{"bus", QPoint(BusIoPortX, BusIoPortY)}});
  installPortOrientationCallback();
  installPropertyCallbacks();
  GraphicalBusInput::applyStartValue();
  refreshFromComponent();
}

void GraphicalBusInput::installPropertyCallbacks()
{
  if (!associatedComponent)
    return;

  // safeThis guards the QGraphicsObject (this) lifetime: property callbacks may
  // fire after the graphical item has been deleted, so we must check it before use.
  QPointer<GraphicalBusInput> safeThis(this);
  // boundComponent is a weak_ptr to the very same Component returned by
  // getComponent(). It guards the component's lifetime independently and lets us
  // detect whether the item's component was replaced between install and invocation.
  std::weak_ptr<Component> boundComponent = associatedComponent;

  associatedComponent->setPropertyCallback("name",
                                           [safeThis](const PropertyValue& value) {
                                             if (safeThis)
                                               safeThis->prepareGeometryChange();
                                             return value;
                                           });

  associatedComponent->setPropertyCallback(
      "size", [safeThis, boundComponent](const PropertyValue& value) {
        const int normalizedSize = normalizedBusSize(std::get<int>(value));
        auto      component      = boundComponent.lock();
        if (auto* busInput = dynamic_cast<DummyBusInputComponent*>(component.get()))
          busInput->setSize(normalizedSize);
        // safeThis->getComponent() and component point at the same object; the
        // equality check confirms the item's component was not swapped out, and
        // safeThis ensures the item itself is still alive before we touch it.
        if (safeThis && safeThis->getComponent() == component)
          safeThis->setValue(safeThis->currentValue);
        return normalizedSize;
      });

  associatedComponent->setPropertyCallback(
      "startValue", [safeThis, boundComponent](const PropertyValue& value) {
        const auto requestedValue = std::get<BusValue>(value);
        const auto component      = boundComponent.lock();
        const auto outputs =
            component ? component->getOutputs() : decltype(component->getOutputs()){};
        const auto width = outputs.empty() ? 1 : outputs[0].size();
        if (requestedValue.empty() || std::ranges::contains(requestedValue, State::ERROR))
          throw std::invalid_argument("Bus input values must not contain ERROR");
        const State extension =
            requestedValue.size() == 1 && requestedValue.front() == State::UNKNOWN
                ? State::UNKNOWN
                : State::LOW;
        const auto newValue =
            SILICON::wireUtils::normalizeBusValue(requestedValue, width, extension);

        // See the "size" callback above: safeThis guards the item, and the
        // equality check confirms the item still owns this same component.
        if (safeThis && safeThis->getComponent() == component)
          safeThis->setValue(newValue);
        return newValue;
      });
}

void GraphicalBusInput::setComponent(const Component_ptr& component)
{
  GraphicalIO::setComponent(component);
  installPropertyCallbacks();
  refreshFromComponent();
  updatePortOrientation();
}

void GraphicalBusInput::setValue(const BusValue& value)
{
  const auto outputs = getComponent()->getOutputs();
  if (outputs.empty())
    return;

  const auto width = outputs[0].size();
  if (value.empty() || std::ranges::contains(value, State::ERROR))
    throw std::invalid_argument("Bus input values must not contain ERROR");
  const State extension =
      value.size() == 1 && value.front() == State::UNKNOWN ? State::UNKNOWN : State::LOW;
  currentValue = SILICON::wireUtils::normalizeBusValue(value, width, extension);

  if (auto* shape = getBusIoShape(getItemShape(), "GraphicalBusInput::setValue")) {
    shape->setBusWidth(static_cast<unsigned int>(width));
    shape->setValueText(compactValueText(currentValue, SILICON::core::BusValueFormat::Hex,
                                         (width + 3) / 4));
  }

  if (!isInteractiveSimulation(this))
    propagateCurrentValue();
  emit inputToggled(getComponent()->getOutputs()[0], currentValue,
                    getComponent()->weak_from_this());
}

void GraphicalBusInput::propagateCurrentValue()
{
  if (auto* busInput = dynamic_cast<DummyBusInputComponent*>(getComponent().get())) {
    busInput->setBusValue(currentValue);
  }
}

void GraphicalBusInput::editValue()
{
  const auto outputs = getComponent()->getOutputs();
  if (outputs.empty())
    return;

  const size_t  width = outputs[0].size();
  const QString prompt =
      QString("Set %1-bit bus value (decimal, 0x..., or 0b...)").arg(width);

  const QPointer<GraphicalBusInput> safeThis(this);
  SILICON::ui::inputDialog::getText(
      SILICON::ui::inputDialog::parentWidgetForGraphicsItem(this), "Bus Input", prompt,
      QString::fromStdString(SILICON::core::formatValue(
          currentValue, SILICON::core::BusValueFormat::Hex, (width + 3) / 4)),
      [safeThis, width](const QString& text) {
        if (!safeThis)
          return;

        const auto parsed = SILICON::core::valueFromStr(text.toStdString());
        if (parsed.format == SILICON::core::BusValueFormat::Unknown
            || std::ranges::contains(parsed.value, State::ERROR))
          return;

        const auto resized = SILICON::core::resizeParsedValue(parsed, width);
        if (!resized) {
          SILICON::ui::inputDialog::warning(
              SILICON::ui::inputDialog::parentWidgetForGraphicsItem(safeThis.data()),
              "Bus Input", "The value does not fit in the bus width.");
          return;
        }
        safeThis->setValue(*resized);
      });
}

void GraphicalBusInput::handleSimulationClick()
{
  editValue();
}

void GraphicalBusInput::applyStartValue()
{
  const auto startValue = getComponent()
                              ->getPropertyValue<BusValue>("startValue")
                              .value_or(BusValue{State::LOW});
  const auto outputs = getComponent()->getOutputs();
  if (outputs.empty())
    return;

  const auto width = outputs[0].size();
  currentValue     = SILICON::wireUtils::normalizeBusValue(startValue, width);

  if (auto* shape = getBusIoShape(getItemShape(), "GraphicalBusInput::applyStartValue")) {
    shape->setBusWidth(static_cast<unsigned int>(width));
    shape->setValueText(compactValueText(currentValue, SILICON::core::BusValueFormat::Hex,
                                         (width + 3) / 4));
  }

  propagateCurrentValue();
}

void GraphicalBusInput::resetSimulationState()
{
  applyStartValue();
}

void GraphicalBusInput::refreshFromComponent()
{
  if (!getComponent() || getComponent()->getOutputs().empty())
    return;

  const auto width = getComponent()->getOutputs()[0].size();
  if (auto* shape =
          getBusIoShape(getItemShape(), "GraphicalBusInput::refreshFromComponent")) {
    shape->setBusWidth(static_cast<unsigned int>(width));
    shape->setValueText(compactValueText(currentValue, SILICON::core::BusValueFormat::Hex,
                                         (width + 3) / 4));
  }
}

void GraphicalBusInput::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                              QWidget* widget)
{
  painter->setFont(UI_FONT);
  const QString name = getComponentName();
  if (!name.isEmpty()) {
    if (const auto* shape = getBusIoShape(getItemShape(), "GraphicalBusInput::paint")) {
      painter->drawText(busIoNameRect(shape->boundingRect(), name, isPortOrientationUp()),
                        Qt::AlignRight | Qt::AlignVCenter, name);
    }
  }
  GraphicalLogicComponent::paint(painter, option, widget);
}

QRectF GraphicalBusInput::boundingRect() const
{
  return busIoNamedBounds(getItemShape(), getComponentName(),
                          GraphicalLogicComponent::boundingRect(), isPortOrientationUp());
}

// --- Graphical Output
// ------------------------------------------------------------------

GraphicalOutputSingle::GraphicalOutputSingle(QGraphicsItem* parent)
  : GraphicalIO(ItemCategory::Output,
                std::make_shared<DummyOutputComponent>(Bus(1), "out"),
                new QGraphicsSvgItem(":/other_components/output_unknown.svg"), parent)
{
  isEditable = false;
  GraphicalLogicComponent::setPorts({PortPair{"in", QPoint(20, 60)}}, {});
  installPortOrientationCallback();

  associatedComponent->setPropertyCallback("name", [this](const PropertyValue& value) {
    prepareGeometryChange();
    return value;
  });
}

void GraphicalOutputSingle::setState(State state)
{
  QString shapePath = getOffShapePath();
  if (state == State::HIGH)
    shapePath = getOnShapePath();
  else if (state == State::UNKNOWN)
    shapePath = getUnknownShapePath();

  setItemShape(new QGraphicsSvgItem(shapePath));
}

void GraphicalOutputSingle::applyStartValue()
{
  resetSimulationState();
}

void GraphicalOutputSingle::resetSimulationState()
{
  setState(State::UNKNOWN);
}

void GraphicalOutputSingle::paint(QPainter*                       painter,
                                  const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  painter->setFont(UI_FONT);
  const QString name = getComponentName();
  if (!name.isEmpty()) {
    painter->drawText(componentNameRect(name), Qt::AlignLeft | Qt::AlignVCenter, name);
  }
  GraphicalLogicComponent::paint(painter, option, widget);
}

QRectF GraphicalOutputSingle::boundingRect() const
{
  auto rect = GraphicalLogicComponent::boundingRect();
  return rect.united(componentNameRect(getComponentName()));
}

// --- Graphical Bus Output --------------------------------------------------------------

GraphicalBusOutput::GraphicalBusOutput(QGraphicsItem* parent)
  : GraphicalIO(ItemCategory::Output,
                std::make_shared<DummyBusOutputComponent>(Bus(8), "bus_out"),
                new BusIoShape(BusIoKind::Output, 8), parent)
{
  isEditable = false;
  GraphicalLogicComponent::setPorts({PortPair{"bus", QPoint(BusIoPortX, BusIoPortY)}},
                                    {});
  installPortOrientationCallback();
  installPropertyCallbacks();
  refreshFromComponent();
}

void GraphicalBusOutput::installPropertyCallbacks()
{
  if (!associatedComponent)
    return;

  QPointer<GraphicalBusOutput> safeThis(this);
  std::weak_ptr<Component>     boundComponent = associatedComponent;

  associatedComponent->setPropertyCallback("name",
                                           [safeThis](const PropertyValue& value) {
                                             if (safeThis)
                                               safeThis->prepareGeometryChange();
                                             return value;
                                           });

  associatedComponent->setPropertyCallback(
      "size", [safeThis, boundComponent](const PropertyValue& value) {
        const int normalizedSize = normalizedBusSize(std::get<int>(value));
        auto      component      = boundComponent.lock();
        if (auto* busOutput = dynamic_cast<DummyBusOutputComponent*>(component.get()))
          busOutput->setSize(normalizedSize);
        if (safeThis && safeThis->getComponent() == component)
          safeThis->refreshFromComponent();
        return normalizedSize;
      });
}

void GraphicalBusOutput::setComponent(const Component_ptr& component)
{
  GraphicalIO::setComponent(component);
  installPropertyCallbacks();
  refreshFromComponent();
  updatePortOrientation();
}

void GraphicalBusOutput::setBusState(const Bus& bus)
{
  if (auto* shape = getBusIoShape(getItemShape(), "GraphicalBusOutput::setBusState")) {
    shape->setBusWidth(static_cast<unsigned int>(bus.size()));

    if (bus.isInErrorState()) {
      shape->setDisplayState(State::ERROR);
      shape->setValueText("ERR");
    } else if (bus.hasUnknowns()) {
      shape->setDisplayState(State::UNKNOWN);
      shape->setValueText(
          compactValueText(bus.getCurrentValue(), SILICON::core::BusValueFormat::Raw));
    } else {
      const auto value   = bus.getCurrentValue();
      const bool anyHigh = std::ranges::contains(value, State::HIGH);
      shape->setDisplayState(anyHigh ? State::HIGH : State::LOW);
      shape->setValueText(compactValueText(value, SILICON::core::BusValueFormat::Hex,
                                           (bus.size() + 3) / 4));
    }
  }
}

void GraphicalBusOutput::refreshFromComponent()
{
  if (!getComponent() || getComponent()->getInputs().empty())
    return;
  setBusState(getComponent()->getInputs()[0]);
}

void GraphicalBusOutput::applyStartValue()
{
  resetSimulationState();
}

void GraphicalBusOutput::resetSimulationState()
{
  if (auto* shape =
          getBusIoShape(getItemShape(), "GraphicalBusOutput::resetSimulationState")) {
    const auto width =
        (!getComponent() || getComponent()->getInputs().empty())
            ? 1U
            : static_cast<unsigned int>(getComponent()->getInputs()[0].size());
    shape->setBusWidth(width);
    shape->setDisplayState(State::UNKNOWN);
    shape->setValueText(
        compactValueText(QString(width, QChar(std::to_underlying(State::UNKNOWN)))));
  }
}

void GraphicalBusOutput::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                               QWidget* widget)
{
  painter->setFont(UI_FONT);
  const QString name = getComponentName();
  if (!name.isEmpty()) {
    if (const auto* shape = getBusIoShape(getItemShape(), "GraphicalBusOutput::paint")) {
      painter->drawText(busIoNameRect(shape->boundingRect(), name, isPortOrientationUp()),
                        Qt::AlignRight | Qt::AlignVCenter, name);
    }
  }
  GraphicalLogicComponent::paint(painter, option, widget);
}

QRectF GraphicalBusOutput::boundingRect() const
{
  return busIoNamedBounds(getItemShape(), getComponentName(),
                          GraphicalLogicComponent::boundingRect(), isPortOrientationUp());
}

}  // namespace ui
}  // namespace SILICON
