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

#pragma once

#include <extraComponents/arithmetic.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

class GraphicalHalfAdder : public GraphicalLogicComponent {
  Q_OBJECT
public:
  explicit GraphicalHalfAdder(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::HALF_ADDER; }
};

class GraphicalFullAdder : public GraphicalLogicComponent {
  Q_OBJECT
public:
  explicit GraphicalFullAdder(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::FULL_ADDER; }
};

class GraphicalAdderNBits : public GraphicalLogicComponent {
  Q_OBJECT
public:
  explicit GraphicalAdderNBits(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::ADDER_N_BITS; }
};
