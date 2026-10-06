/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

class QAction;

namespace SILICON::ui {

/** Project-wide commands: files, application settings, and the about box. */
struct ProjectActions {
  QAction* newProject  = nullptr;
  QAction* open        = nullptr;
  QAction* save        = nullptr;
  QAction* exportImage = nullptr;
  QAction* exit        = nullptr;
  QAction* settings    = nullptr;
  QAction* about       = nullptr;
};

/** Commands adding to, or converting, the documents of a project. */
struct DocumentActions {
  QAction* newCircuit      = nullptr;
  QAction* newCodeFile     = nullptr;
  QAction* newArchitecture = nullptr;
  QAction* newBinaryFile   = nullptr;
  QAction* codeConversion  = nullptr;
};

/** Clipboard, history, and deletion commands of the active editor. */
struct EditActions {
  QAction* undo   = nullptr;
  QAction* redo   = nullptr;
  QAction* cut    = nullptr;
  QAction* copy   = nullptr;
  QAction* paste  = nullptr;
  QAction* remove = nullptr;
};

/** Main-window actions driven by the circuit editor. */
struct CircuitActions {
  QAction* rotate                  = nullptr;
  QAction* autoPlace               = nullptr;
  QAction* centerView              = nullptr;
  QAction* setNormalMode           = nullptr;
  QAction* setPanMode              = nullptr;
  QAction* setWireCreationMode     = nullptr;
  QAction* setSimulationMode       = nullptr;
  QAction* setComponentPlacingMode = nullptr;
  QAction* cancelInteraction       = nullptr;
  QAction* openComponentCatalog    = nullptr;
  QAction* editSubcircuitShape     = nullptr;
};

/** Main-window actions driven by the architecture subsystem. */
struct ArchitectureActions {
  QAction* build     = nullptr;
  QAction* visualize = nullptr;
};

/** Main-window actions driven by the waveform viewer. */
struct WaveformActions {
  QAction* toggleTrace = nullptr;
};

}  // namespace SILICON::ui
