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

namespace SILICON::ui {

class EditorWorkspace;
class ProjectDocumentController;
struct ProjectSession;

/** Coordinates the architecture editor tabs, builds, and the ISA visualizer. */
class ArchitectureController : public QObject {
  Q_OBJECT

public:
  ArchitectureController(ProjectSession& session, EditorWorkspace& workspace,
                         ProjectDocumentController& documents, QObject* parent = nullptr);

  /** @brief Compiles the active architecture document, reporting build diagnostics. */
  void buildActiveArchitecture();

  /** @brief Opens the visualizer tab for the active architecture document. */
  void visualizeActiveArchitecture();

signals:
  /**
   * @brief Emitted when the architecture view changed without a document switch.
   *
   * Selecting the visualizer tab changes which editor owns the workspace without
   * activating another document, so action state has to be refreshed explicitly.
   */
  void visualizerTabSelected();

private:
  /** @brief Activates the document owned by a newly selected architecture tab. */
  void handleTabChanged(int index);

  ProjectSession&            session_;
  EditorWorkspace&           workspace_;
  ProjectDocumentController& documents_;
};

}  // namespace SILICON::ui
