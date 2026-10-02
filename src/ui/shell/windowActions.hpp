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
#include <ui/circuit/editor/diagramInteractionController.hpp>
#include <ui/documents/architecture/architectureController.hpp>
#include <ui/shell/uiUtils.hpp>
#include <ui/waveform/waveformController.hpp>

class QAction;
class QMainWindow;
class QMenu;
class QToolBar;

namespace SILICON::ui {
class EditorWorkspace;
struct ProjectSession;
struct ShortcutSetting;

/** Owns main-window actions, menus, shortcuts, and their enabled state. */
class WindowActions : public QObject {
public:
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

  WindowActions(QMainWindow& window, ProjectSession& session, EditorWorkspace& workspace);

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

  /** @brief Commands owned by the project and application shell. */
  ProjectActions project;

  /** @brief Commands owned by the project document workflow. */
  DocumentActions documents;

  /** @brief Commands owned by the active editor, whatever its kind. */
  EditActions edit;

  /** @brief Commands owned by the circuit editor. */
  CircuitActions circuit;

  /** @brief Commands owned by the architecture subsystem. */
  ArchitectureActions architecture;

  /** @brief Commands owned by the waveform viewer. */
  WaveformActions waveform;

private:
  [[nodiscard]] QVector<ShortcutSetting> shortcutSettings() const;
  void                                   bindEditorActions();
  void                                   refreshActiveEditorActions();
  void                                   syncWasmShortcutCapture();

  QMainWindow&     window;
  ProjectSession&  session;
  EditorWorkspace& workspace;
};

}  // namespace SILICON::ui
