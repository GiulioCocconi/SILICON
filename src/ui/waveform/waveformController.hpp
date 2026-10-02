/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <QObject>
#include <QString>

#include <core/projectDocument.hpp>

class QAction;
class QDialog;
class QWidget;

namespace SILICON::ui {

class DiagramScene;
class ProjectDocumentController;
struct ProjectSession;
namespace waveform {
  class Viewer;
}

/** Main-window actions driven by the waveform viewer. */
struct WaveformActions {
  QAction* toggleTrace = nullptr;
};

/** Owns the waveform window and its scene connections. */
class WaveformController : public QObject {
public:
  WaveformController(ProjectSession& session, DiagramScene& scene,
                     ProjectDocumentController& documents, WaveformActions actions,
                     QWidget* window);
  ~WaveformController() override;

  void toggle(bool enabled);

private:
  /** @brief Keeps the viewer bound to the diagram that is now active. */
  void handleActiveDocumentChanged(const QString&                     path,
                                   SILICON::project::DocumentCategory category);
  /** @brief Hides the viewer and leaves the scene in a non-simulating state. */
  void closeViewer();
  void setTraceActionChecked(bool checked);

  DiagramScene&     scene;
  QAction*          toggleAction = nullptr;
  QDialog*          window       = nullptr;
  waveform::Viewer* viewer       = nullptr;
  QString           activeDocumentPath;
  bool              activeIsDiagram = false;
};

}  // namespace SILICON::ui
