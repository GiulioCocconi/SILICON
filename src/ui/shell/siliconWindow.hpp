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

#include <QMainWindow>
#include <QString>

class QAction;
class QByteArray;
class QDialog;
class QDockWidget;
class QEvent;
class QCloseEvent;
class QMenu;
class QObject;
class QPoint;
class QResizeEvent;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QUndoStack;

#include <ui/circuit/editor/diagramInteractionController.hpp>
#include <ui/documents/architecture/architectureController.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectFileController.hpp>
#include <ui/project/projectSession.hpp>
#include <ui/shell/uiUtils.hpp>
#include <ui/shell/windowActions.hpp>
#include <ui/waveform/waveformController.hpp>

#ifdef __EMSCRIPTEN__
  #include <emscripten/html5.h>
#endif

#ifndef QT_NO_CONTEXTMENU
class QContextMenuEvent;
#endif

namespace SILICON::core {
class Circuit;
}

namespace SILICON::ui {
class AboutDialog;
class ComponentCatalogOverlay;
class CircuitEditor;
class GraphicalLogStream;
class LogSideView;
class ProjectTree;
class PropertyPanel;

struct ShortcutSetting;

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

  /**
   * @brief Disconnects window-owned callbacks and scene signal connections.
   */
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

  void editActiveSubcircuitShape();

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
  /** Connects actions to their command handlers. */
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

  /** @brief Dock containing the project circuit tree. */
  QDockWidget* componentsDock = nullptr;

  /** @brief Dock containing the current property editor. */
  QDockWidget* propertyDock = nullptr;

  /** @brief Dock containing application log output. */
  QDockWidget* logDock = nullptr;

  /** @brief Tree widget listing project metadata and documents. */
  ProjectTree*                  projectTree            = nullptr;
  ProjectDocumentController*    documentController     = nullptr;
  ProjectFileController*        fileController         = nullptr;
  DiagramInteractionController* interactionController  = nullptr;
  ArchitectureController*       architectureController = nullptr;
  WindowActions*                actionSet              = nullptr;
  WaveformController*           waveformController     = nullptr;
  PropertyPanel*                propertyPanel          = nullptr;

  /** @brief Widget that displays captured application log lines. */
  LogSideView* logSideView = nullptr;

  /** @brief Adapter that forwards Boost.Log output into the Qt log side view. */
  GraphicalLogStream* graphicalLogStream = nullptr;

  EditorWorkspace* workspace = nullptr;
  CircuitEditor*   circuitEditor = nullptr;

  /** @brief Floating searchable component catalog, shown over the diagram viewport. */
  ComponentCatalogOverlay* componentCatalogOverlay = nullptr;

  /** @brief Diagram viewport whose resize events are filtered, cleared on teardown. */
  QObject* watchedViewport = nullptr;

  /** @brief Undo stack shared by diagram and project operations. */
  QUndoStack* undoStack = nullptr;

  /** @brief Allows the close event triggered after an accepted dirty-file prompt. */
  bool closeAfterSaveConfirmation = false;

  ProjectSession projectSession;

  /** @brief Lazily shown application about dialog. */
  AboutDialog* aboutDialog = nullptr;
};

}  // namespace SILICON::ui
