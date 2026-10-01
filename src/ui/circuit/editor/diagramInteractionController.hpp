/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <QObject>

class QUndoStack;

namespace SILICON::ui {

inline constexpr char CIRCUIT_SELECTION_MIME_TYPE[] =
    "application/vnd.silicon.circuit-selection+bson";

class ComponentCatalogOverlay;
class CircuitEditor;
struct ProjectSession;

/** Diagram editing commands and interaction mode changes. */
class DiagramInteractionController : public QObject {
public:
  DiagramInteractionController(ProjectSession& session, CircuitEditor& circuit,
                               ComponentCatalogOverlay& catalog, QUndoStack& undoStack,
                               QObject* parent = nullptr);

  void copy();
  void cut();
  void paste();
  void rotate();
  void autoPlace();
  void del();
  void setNormalMode();
  void setPanMode();
  void setWireCreationMode();
  void setSimulationMode();
  void setComponentPlacingMode();
  void showComponentCatalog();
  void cancelCurrentInteraction();

private:
  bool copySelectionToClipboard();

  ProjectSession&          session;
  CircuitEditor&           circuit;
  ComponentCatalogOverlay& catalog;
  QUndoStack&              undoStack;
};

}  // namespace SILICON::ui
