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

namespace AppColors {
  inline const QColor ORANGE   = {"#ff9955"};
  inline const QColor LIGHT_ORANGE = {"#ffc999"};
inline const QColor BLUE     = {"#4997d0"};
inline const QColor GREEN    = {"#9acd32"};
inline const QColor VIOLET   = {"#c4459a"};
inline const QColor INTERNAL = {"#ffe6d5"};
}  // namespace AppColors
