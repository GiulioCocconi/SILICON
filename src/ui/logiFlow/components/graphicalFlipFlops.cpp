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

#include "graphicalFlipFlops.hpp"

#include <ui/common/theme.hpp>

#include <QGraphicsRectItem>
#include <QPainter>
#include <QTextOption>

namespace {

constexpr QRectF FlipFlopBodyRect(0.0, 0.0, 80.0, 80.0);

std::shared_ptr<DFlipFlop> makeDFlipFlop()
{
  return std::make_shared<DFlipFlop>(Wire_ptr{}, Wire_ptr{}, Wire_ptr{}, Wire_ptr{},
                                     Wire_ptr{}, Wire_ptr{});
}

std::shared_ptr<EFlipFlop> makeEFlipFlop()
{
  return std::make_shared<EFlipFlop>(Wire_ptr{}, Wire_ptr{}, Wire_ptr{}, Wire_ptr{},
                                     Wire_ptr{}, Wire_ptr{}, Wire_ptr{});
}

std::shared_ptr<JKFlipFlop> makeJKFlipFlop()
{
  return std::make_shared<JKFlipFlop>(Wire_ptr{}, Wire_ptr{}, Wire_ptr{}, Wire_ptr{},
                                      Wire_ptr{}, Wire_ptr{}, Wire_ptr{});
}

class FlipFlopShape : public QGraphicsRectItem {
public:
  explicit FlipFlopShape(QGraphicsItem* parent = nullptr)
    : QGraphicsRectItem(FlipFlopBodyRect, parent)
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
  }
};

}  // namespace

GraphicalFlipFlop::GraphicalFlipFlop(const Component_ptr& component,
                                     QGraphicsItem* parent)
  : GraphicalLogicComponent(component, new FlipFlopShape(parent), parent)
{
  isEditable     = false;
  printPortNames = true;
}

GraphicalDFlipFlop::GraphicalDFlipFlop(QGraphicsItem* parent)
  : GraphicalFlipFlop(makeDFlipFlop(), parent)
{
  setPorts({PortPair{"d", QPoint(-20, 20)}, PortPair{"clk", QPoint(-20, 60)},
            PortPair{"clr", QPoint(40, -20)}, PortPair{"pre", QPoint(40, 100)}},
           {PortPair{"q", QPoint(100, 20)}, PortPair{"!q", QPoint(100, 60)}});
}

GraphicalEFlipFlop::GraphicalEFlipFlop(QGraphicsItem* parent)
  : GraphicalFlipFlop(makeEFlipFlop(), parent)
{
  setPorts({PortPair{"d", QPoint(-20, 20)}, PortPair{"en", QPoint(-20, 40)},
            PortPair{"clk", QPoint(-20, 60)}, PortPair{"clr", QPoint(40, -20)},
            PortPair{"pre", QPoint(40, 100)}},
           {PortPair{"q", QPoint(100, 20)}, PortPair{"!q", QPoint(100, 60)}});
}

GraphicalJKFlipFlop::GraphicalJKFlipFlop(QGraphicsItem* parent)
  : GraphicalFlipFlop(makeJKFlipFlop(), parent)
{
  setPorts({PortPair{"j", QPoint(-20, 20)}, PortPair{"k", QPoint(-20, 40)},
	    PortPair{"clk", QPoint(-20, 60)}, PortPair{"clr", QPoint(40, -20)},
            PortPair{"pre", QPoint(40, 100)}},
           {PortPair{"q", QPoint(100, 20)}, PortPair{"!q", QPoint(100, 60)}});
}
