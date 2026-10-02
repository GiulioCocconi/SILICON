/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <QObject>

#include <core/projectDocument.hpp>

class QAction;

namespace SILICON::ui {

class EditorWorkspace;
class ArchitectureDocumentEditor;
class DocumentEditor;
class ProjectDocumentController;
struct ProjectSession;

/** Main-window actions driven by the architecture subsystem. */
struct ArchitectureActions {
  QAction* build     = nullptr;
  QAction* visualize = nullptr;
};

/** Coordinates the architecture editor tabs, builds, and the ISA visualizer. */
class ArchitectureController : public QObject {
  Q_OBJECT

public:
  ArchitectureController(ProjectSession& session, EditorWorkspace& workspace,
                         ProjectDocumentController& documents, QObject* parent = nullptr);

  /** @brief Connects the main-window architecture actions to their commands. */
  void bindActions(const ArchitectureActions& actions);

  /** @brief Compiles the active architecture document, reporting build diagnostics. */
  void buildActiveArchitecture();

  /** @brief Opens the visualizer tab for the active architecture document. */
  void visualizeActiveArchitecture();

private:
  void configureWorkspace(DocumentEditor* editor);
  /** @brief Activates the document owned by a newly selected architecture tab. */
  void handleTabChanged(int index);

  ProjectSession&             session;
  EditorWorkspace&            workspace;
  ArchitectureDocumentEditor* architectureEditor = nullptr;
  ProjectDocumentController&  documents;
};

}  // namespace SILICON::ui
