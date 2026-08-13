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

#include <QGraphicsSvgItem>

#include <utility>

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

std::shared_ptr<Extender> makeExtender()
{ return std::make_shared<Extender>(Bus(4), Bus(8)); }

std::shared_ptr<Complementer> makeComplementer()
{ return std::make_shared<Complementer>(Bus(4), Bus(4)); }

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

std::shared_ptr<AdderNBits> makeAdderNBits()
{
  constexpr unsigned short defaultSize = 4;
  return std::make_shared<AdderNBits>(
      std::array<Bus, 2>{Bus(defaultSize), Bus(defaultSize)}, Bus(defaultSize),
      Wire_ptr{});
}

}  // namespace

GraphicalExtender::GraphicalExtender(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeExtender(), new UnaryArithmeticShape("expand", parent),
                            parent)
{ setPorts({PortPair{"n", QPoint(10, -20)}}, {PortPair{"o", QPoint(10, 110)}}); }

GraphicalComplementer::GraphicalComplementer(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeComplementer(), new UnaryArithmeticShape("minus", parent),
                            parent)
{ setPorts({PortPair{"n", QPoint(10, -20)}}, {PortPair{"o", QPoint(10, 110)}}); }

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

}  // namespace ui
}  // namespace SILICON
