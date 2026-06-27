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

#include <QGraphicsSvgItem>

namespace {

std::shared_ptr<HalfAdder> makeHalfAdder()
{
  return std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{Wire_ptr{}, Wire_ptr{}}, Wire_ptr{}, Wire_ptr{});
}

std::shared_ptr<FullAdder> makeFullAdder()
{
  return std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{Wire_ptr{}, Wire_ptr{}}, Wire_ptr{}, Wire_ptr{},
      Wire_ptr{});
}

std::shared_ptr<AdderNBits> makeAdderNBits()
{
  constexpr unsigned short defaultSize = 4;
  return std::make_shared<AdderNBits>(
      std::array<Bus, 2>{Bus(defaultSize), Bus(defaultSize)}, Bus(defaultSize),
      Wire_ptr{});
}

}  // namespace

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
                            new QGraphicsSvgItem(":/other_components/ADDER.svg"),
                            parent, true)
{
  printPortNames = true;
  setPorts({PortPair{"a", QPoint(20, -20)}, PortPair{"b", QPoint(80, -20)}},
           {PortPair{"sum", QPoint(50, 120)}, PortPair{"of", QPoint(110, 40)}});
}
