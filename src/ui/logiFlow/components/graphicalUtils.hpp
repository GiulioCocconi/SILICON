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

#pragma once

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>

#include <extraComponents/utils.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

class GraphicalWireSplitter : public GraphicalLogicComponent {
public:
  explicit GraphicalWireSplitter(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::WIRE_SPLITTER; }

  int setSize(int newSize);
  void setComponent(const Component_ptr& component) override;

private:
  void installSizeCallback();

  unsigned int size = 1;
};

class GraphicalWireMerger : public GraphicalLogicComponent {
public:
  explicit GraphicalWireMerger(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::WIRE_MERGER; }

  int setSize(int newSize);
  void setComponent(const Component_ptr& component) override;

private:
  void installSizeCallback();

  unsigned int size = 1;
};
