/*
  Copyright (C) 2026 Giulio Cocconi

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

#include "graphicalArithmetic.hpp"

#include <ui/common/icons.hpp>
#include <ui/common/theme.hpp>

#include <QFont>
#include <QGraphicsSvgItem>
#include <QTextOption>

#include <stdexcept>
#include <utility>
#include <variant>

namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::extra;

namespace {

  class UnaryArithmeticShape : public QGraphicsRectItem {
  public:
    explicit UnaryArithmeticShape(QString iconName, QGraphicsItem* parent = nullptr)
      : QGraphicsRectItem(0, 0, 20, 90, parent), iconName(std::move(iconName))
    {
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      Q_UNUSED(option);
      Q_UNUSED(widget);

      const QColor ink = ThemeEngine::getColor("SILICON_INK");

      painter->setRenderHint(QPainter::Antialiasing, false);
      painter->setPen(QPen(ink, 3));
      painter->setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
      painter->drawRect(rect());

      const int    iconWidth = rect().width() - 5;
      const QPoint leftPoint =
          rect().center().toPoint() - QPoint(iconWidth / 2, iconWidth / 2);
      painter->setPen(QPen(ink));
      Icon(iconName, {QSize(iconWidth, iconWidth)})
          .paint(painter, leftPoint.x(), leftPoint.y(), iconWidth, iconWidth);
    }

  private:
    QString iconName;
  };

  class ComparatorShape : public QGraphicsSvgItem {
  public:
    explicit ComparatorShape(QString mode, QGraphicsItem* parent = nullptr)
      : QGraphicsSvgItem(":/other_components/COMPARATOR.svg", parent), mode(std::move(mode))
    {
    }

    void setMode(QString mode)
    {
      this->mode = std::move(mode);
      update();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      QGraphicsSvgItem::paint(painter, option, widget);

      painter->save();
      painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK")));
      painter->setFont(QFont("Quicksand", 12, QFont::Bold));
      painter->drawText(QRect(QPoint(40, 30), QPoint(60, 40)), mode, QTextOption(Qt::AlignCenter));
      painter->restore();
    }

  private:
    QString mode;
  };

  std::shared_ptr<Extender> makeExtender()
  {
    return std::make_shared<Extender>(Bus(4), Bus(8));
  }

  std::shared_ptr<Complementer> makeComplementer()
  {
    return std::make_shared<Complementer>(Bus(4), Bus(4));
  }

std::shared_ptr<HalfAdder> makeHalfAdder()
{
  return std::make_shared<HalfAdder>(std::array<Wire_ptr, 2>{Wire_ptr{}, Wire_ptr{}},
                                     Wire_ptr{}, Wire_ptr{});
}

std::shared_ptr<FullAdder> makeFullAdder()
{
  return std::make_shared<FullAdder>(std::array<Wire_ptr, 2>{Wire_ptr{}, Wire_ptr{}},
                                     Wire_ptr{}, Wire_ptr{}, Wire_ptr{});
}

std::shared_ptr<Comparator> makeComparator()
{
  return std::make_shared<Comparator>(std::array<Bus, 2>{Bus(4), Bus(4)}, Wire_ptr{});
}

std::shared_ptr<AdderNBits> makeAdderNBits()
{
  constexpr unsigned short defaultSize = 4;
  return std::make_shared<AdderNBits>(
      std::array<Bus, 2>{Bus(defaultSize), Bus(defaultSize)}, Bus(defaultSize),
      Wire_ptr{});
}

std::shared_ptr<Shifter> makeShifter()
{
  constexpr unsigned short defaultSize = 4;
  return std::make_shared<Shifter>(Bus(defaultSize), Bus(defaultSize),
                                   Bus(defaultSize));
}

}  // namespace

GraphicalExtender::GraphicalExtender(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeExtender(), new UnaryArithmeticShape("expand", parent),
                            parent)
{
  setPorts({PortPair{"n", QPoint(10, -20)}}, {PortPair{"o", QPoint(10, 110)}});
}

GraphicalComplementer::GraphicalComplementer(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeComplementer(), new UnaryArithmeticShape("minus", parent),
                            parent)
{
  setPorts({PortPair{"n", QPoint(10, -20)}}, {PortPair{"o", QPoint(10, 110)}});
}

GraphicalHalfAdder::GraphicalHalfAdder(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeHalfAdder(),
                            new QGraphicsSvgItem(":/other_components/H_ADDER.svg"),
                            parent, true)
{
  printPortNames = true;
  setPorts({PortPair{"a", QPoint(20, -20)}, PortPair{"b", QPoint(80, -20)}},
           {PortPair{"sum", QPoint(50, 120)}, PortPair{"co", QPoint(-10, 40)}});
}

GraphicalFullAdder::GraphicalFullAdder(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeFullAdder(),
                            new QGraphicsSvgItem(":/other_components/F_ADDER.svg"),
                            parent, true)
{
  printPortNames = true;
  setPorts({PortPair{"a", QPoint(20, -20)}, PortPair{"b", QPoint(80, -20)},
            PortPair{"ci", QPoint(110, 40)}},
           {PortPair{"sum", QPoint(50, 120)}, PortPair{"co", QPoint(-10, 40)}});
}

GraphicalAdderNBits::GraphicalAdderNBits(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeAdderNBits(),
                            new QGraphicsSvgItem(":/other_components/ADDER.svg"), parent,
                            true)
{
  printPortNames = true;
  setPorts({PortPair{"a", QPoint(20, -20)}, PortPair{"b", QPoint(80, -20)}},
           {PortPair{"sum", QPoint(50, 120)}, PortPair{"of", QPoint(110, 40)}});
}

GraphicalShifter::GraphicalShifter(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeShifter(),
                            new QGraphicsSvgItem(":/other_components/SHIFTR.svg"),
                            parent, true)
{
  setupCallbacks();
  setPorts({PortPair{"value", QPoint(-20, 20)}, PortPair{"amount", QPoint(40, -20)}},
           {PortPair{"result", QPoint(90, 20)}});
}

void GraphicalShifter::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("mode", [this](const PropertyValue& value) {
    return applyMode(std::get<std::string>(value));
  });
}

std::string GraphicalShifter::applyMode(std::string mode)
{
  if (mode != Shifter::LeftMode && mode != Shifter::RightMode)
    throw std::invalid_argument("Shifter mode must be 'left' or 'right'");

  setItemShape(new QGraphicsSvgItem(mode == Shifter::LeftMode
                                        ? ":/other_components/SHIFTL.svg"
                                        : ":/other_components/SHIFTR.svg"));
  return mode;
}

void GraphicalShifter::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  applyMode(component->getPropertyValue<std::string>("mode")
                .value_or(std::string(Shifter::RightMode)));
}

GraphicalComparator::GraphicalComparator(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeComparator(), new ComparatorShape("==", parent), parent,
                            true)
{
  printPortNames = true;
  setupCallbacks();
  setPorts({PortPair{"a", QPoint(20, -20)}, PortPair{"b", QPoint(80, -20)}},
           {PortPair{"", QPoint(50, 70)}});
}

void GraphicalComparator::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("mode", [this](const PropertyValue& value) {
    return applyMode(std::get<std::string>(value));
  });
}

std::string GraphicalComparator::applyMode(std::string mode)
{
  if (auto* shape = dynamic_cast<ComparatorShape*>(getItemShape()))
    shape->setMode(QString::fromStdString(mode));
  return mode;
}

void GraphicalComparator::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  applyMode(component->getPropertyValue<std::string>("mode").value_or("=="));
}

}  // namespace ui
}  // namespace SILICON
