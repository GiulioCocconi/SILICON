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

#include <QObject>

#include <ui/actionGroups.hpp>

class QAction;
class QUndoStack;
class QWidget;

namespace SILICON::ui {

class ComponentCatalogOverlay;
class CircuitEditor;
struct ProjectSession;

/** Diagram editing commands and interaction mode changes. */
class DiagramInteractionController : public QObject {
public:
  DiagramInteractionController(ProjectSession& session, CircuitEditor& circuit,
                               ComponentCatalogOverlay& catalog, QUndoStack& undoStack,
                               QWidget& window);

  /** @brief Connects the main-window diagram actions to their diagram commands. */
  void bindActions(const CircuitActions& actions);

  void rotate();
  void autoPlace();

  /** @brief Opens the graphical shape editor for the active circuit document. */
  void editActiveSubcircuitShape();

  void setNormalMode();
  void setPanMode();
  void setWireCreationMode();
  void setSimulationMode();
  void setComponentPlacingMode();
  void showComponentCatalog();
  void cancelCurrentInteraction();

private:
  ProjectSession&          session;
  CircuitEditor&           circuit;
  ComponentCatalogOverlay& catalog;
  QUndoStack&              undoStack;
  QWidget&                 window;
};

}  // namespace SILICON::ui
