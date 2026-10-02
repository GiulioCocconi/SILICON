/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "waveformController.hpp"

#include <QAction>
#include <QDialog>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/waveform/waveformViewer.hpp>

namespace SILICON::ui {

namespace {

  [[nodiscard]] bool isDiagramDocument(const ProjectSession& session)
  {
    const auto type = SILICON::project::documentTypeForPath(session.activeDocumentPath);
    return type
           && SILICON::project::categoryOf(*type)
                  == SILICON::project::DocumentCategory::Diagram;
  }

}  // namespace

WaveformController::WaveformController(ProjectSession& session, DiagramScene& scene,
                                       ProjectDocumentController& documents,
                                       QWidget*                   window)
  : QObject(window), scene(scene), activeIsDiagram(isDiagramDocument(session))
{
  activeDocumentPath = QString::fromStdString(session.activeDocumentPath);

  this->window = new QDialog(window);
  this->window->setWindowTitle(tr("Waveform"));
  this->window->setModal(false);
  this->window->resize(900, 420);

  auto* layout = new QVBoxLayout(this->window);
  layout->setContentsMargins(0, 0, 0, 0);
  this->viewer = new waveform::Viewer(this->window);
  layout->addWidget(this->viewer);

  connect(this->window, &QDialog::finished, this, [this] {
    setTraceActionChecked(false);
    this->viewer->setEditMode(false);
  });
  connect(&this->scene, &DiagramScene::waveformTraceReset, this->viewer,
          &waveform::Viewer::resetTrace);
  connect(&this->scene, &DiagramScene::waveformTraceSnapshots, this->viewer,
          &waveform::Viewer::appendSnapshots);
  connect(this->viewer, &waveform::Viewer::editModeChanged, this,
          [this](bool enabled) { this->scene.setIoInteractionsEnabled(!enabled); });
  connect(this->viewer, &waveform::Viewer::editTraceCommitted, &this->scene,
          &DiagramScene::simulateEditedWaveform);

  // Traces belong to the circuit that produced them, so the viewer follows the document
  // controller instead of inspecting the project session on its own.
  connect(&documents, &ProjectDocumentController::activeDocumentChanged, this,
          &WaveformController::handleActiveDocumentChanged);
}

void WaveformController::bindActions(const WaveformActions& actions)
{
  toggleAction = actions.toggleTrace;
  connect(toggleAction, &QAction::toggled, this, &WaveformController::toggle);
}

WaveformController::~WaveformController()
{
  delete window;
}

void WaveformController::toggle(const bool enabled)
{
  if (enabled && !activeIsDiagram) {
    setTraceActionChecked(false);
    return;
  }

  window->setVisible(enabled);
  if (!enabled)
    return;

  scene.setInteractionMode(DiagramScene::InteractionMode::SIMULATION_MODE);
  window->raise();
  window->activateWindow();
}

void WaveformController::handleActiveDocumentChanged(
    const QString& path, const SILICON::project::DocumentCategory category)
{
  const bool switchedDocument = path != activeDocumentPath;
  activeDocumentPath          = path;
  activeIsDiagram             = category == SILICON::project::DocumentCategory::Diagram;

  if (!activeIsDiagram) {
    // Waveforms only exist for diagram documents: a code, binary, or architecture
    // document has no scene to trace.
    closeViewer();
    return;
  }

  // Another circuit is now shown, so the previous circuit's samples would be misleading.
  if (switchedDocument)
    viewer->resetTrace({}, 0, {});
}

void WaveformController::setTraceActionChecked(const bool checked)
{
  if (!toggleAction)
    return;
  const QSignalBlocker blocker(toggleAction);
  toggleAction->setChecked(checked);
}

void WaveformController::closeViewer()
{
  // Closing emits finished(), which already unchecks the action and leaves edit mode.
  if (window->isVisible())
    window->close();

  setTraceActionChecked(false);
  viewer->setEditMode(false);
  scene.setInteractionMode(DiagramScene::InteractionMode::NORMAL_MODE);
}

}  // namespace SILICON::ui
