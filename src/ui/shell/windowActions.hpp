/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <initializer_list>
#include <optional>

#include <QObject>
#include <QVector>

#include <core/projectDocument.hpp>
#include <ui/shell/uiUtils.hpp>

class QAction;
class QMainWindow;
class QMenu;
class QToolBar;
class QUndoStack;

namespace SILICON::ui {
class EditorWorkspace;
struct ProjectSession;
struct ShortcutSetting;

/** Owns main-window actions, menus, shortcuts, and their enabled state. */
class WindowActions : public QObject {
public:
  WindowActions(QMainWindow& window, ProjectSession& session, EditorWorkspace& workspace,
                QUndoStack& history);

  void createActions();
  void createMenus();
  void createToolBar();
  void updateHistoryActions();
  void updateEditActions();
  void updateSubcircuitShapeAction();
  void updateCodeAction();
  void updateDocumentActionVisibility();
  void applyStoredSettings();
  void openSettings();
  [[nodiscard]] std::optional<SILICON::project::DocumentType>
              activeDocumentType() const noexcept;
  static void setActionsEnabled(std::initializer_list<QAction*> actions, bool enabled);

  /** @brief Main toolbar containing edit, mode, and simulation actions. */
  QToolBar* toolBar                = nullptr;
  QAction*  diagramToolsSeparator  = nullptr;
  QAction*  documentToolsSeparator = nullptr;

  /** @brief File menu. */
  QMenu* fileMenu = nullptr;

  /** @brief Edit menu. */
  QMenu* editMenu = nullptr;

  /** @brief Help menu. */
  QMenu* helpMenu = nullptr;

  /** @brief Creates a new project. */
  QAction* newAct             = nullptr;
  QAction* newCircuitAct      = nullptr;
  QAction* newCodeFileAct     = nullptr;
  QAction* newArchitectureAct = nullptr;
  QAction* newBinaryFileAct   = nullptr;

  /** @brief Opens an existing project. */
  QAction* openAct = nullptr;

  /** @brief Saves the current project. */
  QAction* saveAct = nullptr;

  /** @brief Exports the current diagram image. */
  QAction* exportImageAct = nullptr;

  /** @brief Closes the application window. */
  QAction* exitAct = nullptr;

  /** @brief Cuts the current selection. */
  QAction* cutAct = nullptr;

  /** @brief Copies the current selection. */
  QAction* copyAct = nullptr;

  /** @brief Pastes a copied selection. */
  QAction* pasteAct = nullptr;

  /** @brief Rotates selected components. */
  QAction* rotateAct = nullptr;

  /** @brief Automatically places components and reroutes wires. */
  QAction* autoPlaceAct = nullptr;

  /** @brief Deletes the current selection. */
  QAction* deleteAct = nullptr;

  /** @brief Opens the about dialog. */
  QAction* aboutAct = nullptr;

  /** @brief Opens the settings dialog. */
  QAction* settingsAct = nullptr;

  /** @brief Activates normal editing mode. */
  QAction* setNormalModeAct = nullptr;

  /** @brief Activates panning mode. */
  QAction* setPanModeAct = nullptr;

  /** @brief Activates wire creation mode. */
  QAction* setWireCreationModeAct = nullptr;

  /** @brief Activates simulation mode. */
  QAction* setSimulationModeAct = nullptr;

  /** @brief Toggles the waveform viewer window for the active scene. */
  QAction* toggleWaveformViewerAct = nullptr;

  /** @brief Cancels the active diagram interaction. */
  QAction* cancelInteractionAct = nullptr;

  /** @brief Opens the component catalog overlay. */
  QAction* openComponentCatalogAct  = nullptr;
  QAction* editSubcircuitShapeAct   = nullptr;
  QAction* codeConversionAct        = nullptr;
  QAction* buildArchitectureAct     = nullptr;
  QAction* visualizeArchitectureAct = nullptr;

  /** @brief Activates component placing mode. */
  QAction* setComponentPlacingModeAct = nullptr;

  /** @brief Undo action created from the shared undo stack. */
  QAction* undoAct = nullptr;

  /** @brief Redo action created from the shared undo stack. */
  QAction* redoAct = nullptr;

private:
  [[nodiscard]] QVector<ShortcutSetting> shortcutSettings() const;
  void                                   syncWasmShortcutCapture();

  QMainWindow&     window_;
  ProjectSession&  session_;
  EditorWorkspace& workspace_;
  QUndoStack&      undoStack_;
};

}  // namespace SILICON::ui
