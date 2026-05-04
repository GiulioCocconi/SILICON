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
#include <QVariant>
#include <utility>

// 1. Define safe roles and categories
enum class ItemDataRole { Category = Qt::UserRole + 1 };

enum class ItemCategory : uint32_t {
  None           = 0,
  GraphicalItem  = 1 << 0,
  WireSegment    = 1 << 1,
  Component      = 1 << 2,
  LogicComponent = 1 << 3,
  Input          = 1 << 4,
  Output         = 1 << 5
};

constexpr ItemCategory operator|(ItemCategory a, ItemCategory b)
{
  return static_cast<ItemCategory>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr ItemCategory operator&(ItemCategory a, ItemCategory b)
{
  return static_cast<ItemCategory>(std::to_underlying(a) & std::to_underlying(b));
}

inline bool hasCategory(const QGraphicsItem* item, ItemCategory cat)
{
  if (!item)
    return false;
  QVariant val = item->data(std::to_underlying(ItemDataRole::Category));
  if (!val.isValid())
    return false;
  return (val.toUInt() & std::to_underlying(cat)) == std::to_underlying(cat);
}

template <typename TargetType>
TargetType* category_cast(QGraphicsItem* item, ItemCategory expectedCategory)
{
  if (hasCategory(item, expectedCategory)) {
    return static_cast<TargetType*>(item);
  }
  return nullptr;
}

enum SiliconTypes {
  UNKNOWN = QGraphicsItem::UserType,
  PORT,
  WIRE_SEGMENT,

  /* LogiFlow */
  COMPONENT = WIRE_SEGMENT + 10,
  GENERIC_IO,
  SINGLE_INPUT, /* Logiflow start */
  SINGLE_OUTPUT,

  /* Components */
  WIRE_SPLITTER,
  WIRE_MERGER,

  AND_GATE,
  NAND_GATE,
  OR_GATE,
  NOR_GATE,
  NOT_GATE,
  XOR_GATE,
  HALF_ADDER,
  FULL_ADDER,

  LOGIFLOW_END,
};
