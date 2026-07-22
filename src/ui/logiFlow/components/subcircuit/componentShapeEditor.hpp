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

#pragma once

#include <string>

class QUndoStack;
class QWidget;

/**
 * @brief Opens the modal editor for a project subcircuit's graphical shape.
 * @param slug Project-local subcircuit slug to edit.
 * @param undoStack Optional undo stack used to route metadata changes.
 * @param parent Optional parent widget for the dialog and warnings.
 */
void editGraphicalSubcircuitShape(const std::string& slug, QUndoStack* undoStack,
                                  QWidget* parent = nullptr);
