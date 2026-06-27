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

#include <core/flipflops.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

class GraphicalFlipFlop : public GraphicalLogicComponent {
  Q_OBJECT
protected:
  GraphicalFlipFlop(const Component_ptr& component, QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::UNKNOWN; }
};

class GraphicalDFlipFlop : public GraphicalFlipFlop {
  Q_OBJECT
public:
  explicit GraphicalDFlipFlop(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::D_FLIP_FLOP; }
};

class GraphicalEFlipFlop : public GraphicalFlipFlop {
  Q_OBJECT
public:
  explicit GraphicalEFlipFlop(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::E_FLIP_FLOP; }
};

class GraphicalJKFlipFlop : public GraphicalFlipFlop {
  Q_OBJECT
public:
  explicit GraphicalJKFlipFlop(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::JK_FLIP_FLOP; }
};
