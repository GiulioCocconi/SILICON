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

#include <ui/documents/documentEditor.hpp>

class QAction;
class QUndoStack;
class QWidget;

namespace SILICON::ui {

inline constexpr char CIRCUIT_SELECTION_MIME_TYPE[] =
    "application/vnd.silicon.circuit-selection+bson";

class ComponentCatalogOverlay;
class CircuitEditor;
struct ProjectSession;

/** Main-window actions driven by the diagram editor. */
struct CircuitActions {
  QAction* rotate                  = nullptr;
  QAction* autoPlace               = nullptr;
  QAction* setNormalMode           = nullptr;
  QAction* setPanMode              = nullptr;
  QAction* setWireCreationMode     = nullptr;
  QAction* setSimulationMode       = nullptr;
  QAction* setComponentPlacingMode = nullptr;
  QAction* cancelInteraction       = nullptr;
  QAction* openComponentCatalog    = nullptr;
  QAction* editSubcircuitShape     = nullptr;
};

/** Diagram editing commands and interaction mode changes. */
class DiagramInteractionController : public QObject {
public:
  DiagramInteractionController(ProjectSession& session, CircuitEditor& circuit,
                               ComponentCatalogOverlay& catalog, QUndoStack& undoStack,
                               QWidget& window);

  /** @brief Connects the main-window diagram actions to their diagram commands. */
  void bindActions(const CircuitActions& actions);

  void copy();
  void cut();
  void paste();
  void rotate();
  void autoPlace();
  void del();

  /** @brief Opens the graphical shape editor for the active circuit document. */
  void editActiveSubcircuitShape();

  void setNormalMode();
  void setPanMode();
  void setWireCreationMode();
  void setSimulationMode();
  void setComponentPlacingMode();
  void showComponentCatalog();
  void cancelCurrentInteraction();

  /** @brief Reports which selection commands the active circuit currently accepts. */
  [[nodiscard]] EditorEditState editState() const;

private:
  bool copySelectionToClipboard();

  ProjectSession&          session;
  CircuitEditor&           circuit;
  ComponentCatalogOverlay& catalog;
  QUndoStack&              undoStack;
  QWidget&                 window;
};

}  // namespace SILICON::ui
