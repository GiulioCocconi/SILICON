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
                                       QAction& toggleAction, QWidget* window)
  : QObject(window),
    scene_(scene),
    toggleAction_(toggleAction),
    activeIsDiagram_(isDiagramDocument(session))
{
  activeDocumentPath_ = QString::fromStdString(session.activeDocumentPath);

  window_ = new QDialog(window);
  window_->setWindowTitle(tr("Waveform"));
  window_->setModal(false);
  window_->resize(900, 420);

  auto* layout = new QVBoxLayout(window_);
  layout->setContentsMargins(0, 0, 0, 0);
  viewer_ = new waveform::Viewer(window_);
  layout->addWidget(viewer_);

  connect(window_, &QDialog::finished, this, [this] {
    const QSignalBlocker blocker(&toggleAction_);
    toggleAction_.setChecked(false);
    viewer_->setEditMode(false);
  });
  connect(&scene_, &DiagramScene::waveformTraceReset, viewer_,
          &waveform::Viewer::resetTrace);
  connect(&scene_, &DiagramScene::waveformTraceSnapshots, viewer_,
          &waveform::Viewer::appendSnapshots);
  connect(viewer_, &waveform::Viewer::editModeChanged, this,
          [this](bool enabled) { scene_.setIoInteractionsEnabled(!enabled); });
  connect(viewer_, &waveform::Viewer::editTraceCommitted, &scene_,
          &DiagramScene::simulateEditedWaveform);

  // Traces belong to the circuit that produced them, so the viewer follows the document
  // controller instead of inspecting the project session on its own.
  connect(&documents, &ProjectDocumentController::activeDocumentChanged, this,
          &WaveformController::handleActiveDocumentChanged);
}

WaveformController::~WaveformController()
{
  delete window_;
}

void WaveformController::toggle(const bool enabled)
{
  if (enabled && !activeIsDiagram_) {
    const QSignalBlocker blocker(&toggleAction_);
    toggleAction_.setChecked(false);
    return;
  }

  window_->setVisible(enabled);
  if (!enabled)
    return;

  scene_.setInteractionMode(DiagramScene::InteractionMode::SIMULATION_MODE);
  window_->raise();
  window_->activateWindow();
}

void WaveformController::handleActiveDocumentChanged(
    const QString& path, const SILICON::project::DocumentCategory category)
{
  const bool switchedDocument = path != activeDocumentPath_;
  activeDocumentPath_         = path;
  activeIsDiagram_            = category == SILICON::project::DocumentCategory::Diagram;

  if (!activeIsDiagram_) {
    // Waveforms only exist for diagram documents: a code, binary, or architecture
    // document has no scene to trace.
    closeViewer();
    return;
  }

  // Another circuit is now shown, so the previous circuit's samples would be misleading.
  if (switchedDocument)
    viewer_->resetTrace({}, 0, {});
}

void WaveformController::closeViewer()
{
  // Closing emits finished(), which already unchecks the action and leaves edit mode.
  if (window_->isVisible())
    window_->close();

  const QSignalBlocker blocker(&toggleAction_);
  toggleAction_.setChecked(false);
  viewer_->setEditMode(false);
  scene_.setInteractionMode(DiagramScene::InteractionMode::NORMAL_MODE);
}

}  // namespace SILICON::ui
