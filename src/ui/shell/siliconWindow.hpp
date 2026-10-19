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

#include <memory>

#include <QMainWindow>
#include <ui/lifetime.hpp>
#include <ui/project/projectSession.hpp>

#ifdef __EMSCRIPTEN__
  #include <emscripten/html5.h>
#endif

#ifndef QT_NO_CONTEXTMENU
class QContextMenuEvent;
#endif

class QDockWidget;
class QEvent;
class QCloseEvent;
class QObject;
class QPoint;
class QResizeEvent;
class QUndoStack;

namespace SILICON::ui {
class AboutDialog;
class ComponentCatalogOverlay;
class CircuitEditor;
class GraphicalLogStream;
class LogSideView;
class ProjectTree;
class PropertyPanel;
class ArchitectureController;
class DiagramInteractionController;
class EditorWorkspace;
class ProjectDocumentController;
class ProjectFileController;
class WaveformController;
class WindowActions;
class WindowLifetime;
#ifdef __EMSCRIPTEN__
class BrowserKeyDownRegistration;
#endif

/**
 * @brief Main window for the SILICON graphical circuit editor.
 *
 * Composes the editor, project, action, and dock controllers.
 */
class SiliconWindow : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Constructs and initializes the SILICON editor window.
   */
  SiliconWindow();

  /** @brief Releases scoped callbacks and UI owners in dependency order. */
  ~SiliconWindow() override;

protected:
#ifndef QT_NO_CONTEXTMENU
  /**
   * @brief Opens the diagram context menu for mode and editing actions.
   * @param event Qt context-menu event delivered to the main window
   */
  void contextMenuEvent(QContextMenuEvent* event) override;
#endif  // QT_NO_CONTEXTMENU

  /**
   * @brief Handles viewport events needed by floating child widgets.
   * @param watched Object that received the event
   * @param event Event being filtered
   * @return True when the event was consumed
   */
  bool eventFilter(QObject* watched, QEvent* event) override;

  /**
   * @brief Keeps floating overlays aligned when the main window is resized.
   * @param event Qt resize event
   */
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

private slots:
  /** @brief Placeholder slot for exporting the diagram as an image. */
  void exportImage() {}

  /** @brief Shows the application about dialog. */
  void about() const;

  /** @brief Refreshes the status bar text for the current scene mode. */
  void updateStatus() const;

  /** @brief Updates action enabled states and property UI after selection changes. */
  void selectionChanged();

  /** @brief Handles user selection changes in the project circuit tree. */
  void projectTreeSelectionChanged();

  /**
   * @brief Shows the context menu for project-tree circuit actions.
   * @param position Position within the project tree viewport
   */
  void showProjectTreeContextMenu(const QPoint& position);

  /** @brief Rebuilds the property dock for the current selection or active circuit. */
  void updatePropertyDock();

private:
  /** Connects the application-level actions to their command handlers. */
  void wireActions();

  /** @brief Repositions and resizes the component catalog overlay. */
  void updateComponentCatalogGeometry();

  /** @brief Creates and wires the project tree widget shown in the project dock. */
  void initializeProjectTree();

  /** @brief Refreshes actions and docks that depend on the active document. */
  void refreshActiveDocumentUi();

  /** @brief Refreshes the property dock after a project-level change. */
  void refreshProjectUi();

#ifdef __EMSCRIPTEN__
  /**
   * @brief Browser keydown callback used to catch Escape outside Qt focus handling.
   * @param eventType Emscripten event type
   * @param keyEvent Browser keyboard event data
   * @param userData Pointer to the SiliconWindow instance
   * @return EM_TRUE when the event was handled
   */
  static EM_BOOL wasmKeyDownCallback(int                            eventType,
                                     const EmscriptenKeyboardEvent* keyEvent,
                                     void*                          userData);

  /**
   * @brief Handles a browser Escape key press for overlays and active interactions.
   * @return True when Escape was consumed
   */
  bool handleWasmEscapeKey();
#endif

  /** @brief Dock containing the project document tree. */
  QDockWidget* documentDock = nullptr;

  /** @brief Dock containing the current property editor. */
  QDockWidget* propertyDock = nullptr;

  /** @brief Dock containing application log output. */
  QDockWidget* logDock = nullptr;

  /** @brief Borrowed handles into the workspace and Qt widget hierarchy. */
  ProjectTree*                  projectTree            = nullptr;
  LogSideView* logSideView = nullptr;
  CircuitEditor*   circuitEditor = nullptr;
  ComponentCatalogOverlay* componentCatalogOverlay = nullptr;
  AboutDialog* aboutDialog = nullptr;

  /** @brief Allows the close event triggered after an accepted dirty-file prompt. */
  bool closeAfterSaveConfirmation = false;

  // Declaration order is lifetime order: the session and history outlive the
  // editors, and the document navigator remains available during scene teardown.
  ProjectSession projectSession;
  std::unique_ptr<QUndoStack>                   undoStack;
  std::unique_ptr<ProjectDocumentController>    documentController;
  std::unique_ptr<EditorWorkspace>              workspace;
  std::unique_ptr<DiagramInteractionController> interactionController;
  std::unique_ptr<ProjectFileController>        fileController;
  std::unique_ptr<WindowActions>                actionSet;
  std::unique_ptr<ArchitectureController>       architectureController;
  std::unique_ptr<WaveformController>           waveformController;
  std::unique_ptr<PropertyPanel>                propertyPanel;
  std::unique_ptr<GraphicalLogStream>           graphicalLogStream;

  ScopedEventFilter               viewportFilter;
  std::unique_ptr<WindowLifetime> windowLifetime;
#ifdef __EMSCRIPTEN__
  std::unique_ptr<BrowserKeyDownRegistration> browserKeyDown;
#endif
  // Disconnect and discard queued callbacks before any teardown emits signals.
  CallbackScope callbacks;
};

}  // namespace SILICON::ui
