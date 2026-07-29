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

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QMainWindow>
#include <QSpinBox>
#include <QString>
#include <QVector>

#include <core/circuit.hpp>

class AboutDialog;
class QAction;
class ComponentCatalogOverlay;
class DiagramScene;
class DiagramView;
class GraphicalLogStream;
class QByteArray;
class QDialog;
class QDockWidget;
class QEvent;
class QCloseEvent;
class QMenu;
class QObject;
class QPoint;
class QPlainTextEdit;
class QResizeEvent;
class QStackedWidget;
class QToolBar;
class QTreeWidgetItem;
class QUndoStack;
class LogSideView;
struct ShortcutSetting;
class ProjectTree;

#include <core/projectDependencyGraph.hpp>
#include <core/serialization/projectFile.hpp>

#ifdef __EMSCRIPTEN__
  #include <emscripten/html5.h>
#endif

#ifndef QT_NO_CONTEXTMENU
class QContextMenuEvent;
#endif


namespace SILICON {
namespace ui {
using namespace SILICON::core;

class AboutDialog;
class ComponentCatalogOverlay;
class DiagramScene;
class DiagramView;
class GraphicalLogStream;
class LogSideView;
class ProjectTree;
namespace waveform {
class Viewer;
}
struct ShortcutSetting;

/**
 * @brief Main window for the LogiFlow graphical circuit editor.
 *
 * Owns the diagram scene/view, project tree, action/menu/toolbar wiring, property
 * editor, waveform viewer, and log dock used by the LogiFlow application.
 */
class LogiFlowWindow : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Constructs and initializes the LogiFlow editor window.
   */
  LogiFlowWindow();

  /**
   * @brief Disconnects window-owned callbacks and scene signal connections.
   */
  ~LogiFlowWindow() override;

  /** @brief Returns the undo stack used by diagram and project commands. */
  [[nodiscard]] QUndoStack* getUndoStack() const { return this->undoStack; }

  /** @brief Returns the dock widget view that displays application log output. */
  [[nodiscard]] LogSideView* getLogSideView() const { return this->logSideView; }

  /**
   * @brief Returns the project path of the currently active circuit.
   *
   * Falls back to the project's configured main circuit path when no explicit
   * active circuit has been recorded yet.
   */
  [[nodiscard]] std::string activeProjectCircuitPath() const;
  [[nodiscard]] std::string activeProjectSubcircuitSlug() const;
  bool                      activateProjectDocument(const std::string& documentPath);

  /**
   * @brief Switches the editor to a circuit in the active project.
   * @param circuitPath Project-relative path of the circuit JSON entry
   * @return True when the circuit exists and is active after the call
   */
  bool activateProjectCircuit(const std::string& circuitPath);

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
  /** @brief Resets the editor to a new unsaved project. */
  void newFile();

  /** @brief Opens a saved LogiFlow project file from disk. */
  void open();

  /** @brief Saves the active project to its current filename or prompts for one. */
  void save();

  /** @brief Placeholder slot for exporting the diagram as an image. */
  void exportImage() {}

  /**
   * @brief Copies the current selection to the clipboard, then deletes it.
   *
   * Deletion only happens after the selection is successfully serialized into the
   * application clipboard MIME format.
   */
  void cut()
  {
    if (copySelectionToClipboard())
      del();
  }

  /**
   * @brief Copies the current diagram selection to the clipboard.
   *
   * The clipboard payload uses the Silicon LogiFlow BSON MIME format.
   */
  void copy();

  /**
   * @brief Pastes a Silicon LogiFlow selection from the clipboard.
   *
   * Invalid, empty, or unrelated clipboard data is ignored.
   */
  void paste();

  /** @brief Rotates all selected graphical components. */
  void rotate();

  /** @brief Deletes the current selection from the diagram. */
  void del();  // Delete is a CPP keyword

  /** @brief Shows the application about dialog. */
  void about() const;

  /** @brief Opens the LogiFlow settings dialog. */
  void openSettings();

  /** @brief Returns the diagram scene to normal editing mode. */
  void setNormalMode();

  /** @brief Switches the diagram scene to panning mode. */
  void setPanMode();

  /** @brief Switches the diagram scene to wire creation mode. */
  void setWireCreationMode();

  /** @brief Switches the diagram scene to simulation mode. */
  void setSimulationMode();

  /** @brief Switches the diagram scene to component placing mode. */
  void setComponentPlacingMode();

  /** @brief Opens the component catalog overlay above the diagram view. */
  void showComponentCatalog();
  void editActiveSubcircuitShape();
  /**
   * @brief Enters editable HDL mode or compiles the active HDL-backed subcircuit.
   *
   * Converting a graphical subcircuit to HDL is irreversible. Once it is HDL-backed,
   * enabling this action makes its source editable and disables simulation; disabling
   * it asks Yosys to rebuild the cached core circuit before simulation is re-enabled.
   */
  void toggleHdlCodeMode(bool enabled);

  /** @brief Cancels any active scene interaction and returns to normal editing. */
  void cancelCurrentInteraction();

  /**
   * @brief Enables or disables FST waveform tracing for the current scene.
   * @param enabled True to write FST traces during simulation
   */
  void toggleFstTracing(bool enabled);

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

  /** @brief Prompts for and inserts a new circuit into the current project. */
  void createCircuit();

  /** @brief Deletes the selected circuit from the current project when allowed. */
  void deleteSelectedCircuit();
  void createSubcircuit();
  void deleteSelectedSubcircuit();

  /** @brief Rebuilds the property dock for the current selection or active circuit. */
  void updatePropertyDock();

private:
  /** @brief Creates QAction instances, shortcuts, and action signal connections. */
  void createActions();

  /** @brief Builds the main menu bar from the configured actions. */
  void createMenus();

  /** @brief Builds the main toolbar from the configured actions. */
  void createToolBar();

  /** @brief Creates the waveform viewer dialog used by simulations. */
  void createWaveformWindow();

  /** @brief Applies persisted window, dock, and shortcut settings. */
  void applyStoredSettings();

  /** @brief Repositions and resizes the component catalog overlay. */
  void updateComponentCatalogGeometry();
  void updateSubcircuitShapeAction();
  /** @brief Synchronizes toolbar and simulation actions with the active HDL state. */
  void updateHdlActions();
  /** @brief Loads the active subcircuit's project asset into the HDL editor. */
  void showActiveHdlDocument();
  /**
   * @brief Compiles edited Verilog, replaces the prepared core circuit, and makes the
   * source read-only. The editor remains writable when compilation fails.
   */
  void compileActiveHdl();
  /**
   * @brief Irreversibly replaces graphical implementation data with an HDL descriptor
   * and a generated `hdl/<slug>.v` project asset.
   */
  void convertActiveSubcircuitToHdl();
  /** @brief Returns whether the active subcircuit scene contains an HDL descriptor. */
  [[nodiscard]] bool                            activeDocumentHasHdl() const;
  [[nodiscard]] SILICON::project::ProjectAsset* projectAsset(std::string_view path);
  [[nodiscard]] const SILICON::project::ProjectAsset*
  projectAsset(std::string_view path) const;

  /**
   * @brief Updates the current project filename and window title.
   * @param fn New project filename
   */
  void setFileName(const QString& fn);

  /**
   * @brief Loads project or legacy circuit content into the window.
   * @param fileName Source filename used for metadata and error messages
   * @param fileContent Serialized file bytes to parse
   */
  void loadCircuitContent(const QString& fileName, const QByteArray& fileContent);

  /** @brief Creates and wires the project tree widget shown in the project dock. */
  void initializeProjectTree();

  /** @brief Rebuilds the project tree from the in-memory project circuit list. */
  void rebuildProjectTree();

  /** @brief Refreshes displayed project and circuit names in the project tree. */
  void updateProjectTreeLabels();

  /** @brief Clears project-tree selection without switching circuits. */
  void clearProjectTreeSelection();

  /** @brief Serializes the active scene into the shared project document store. */
  void saveActiveDocumentPayload();

  /**
   * @brief Switches the diagram scene to a project circuit.
   * @param circuitPath Project-relative path of the target circuit
   * @param selectInTree Whether to select the circuit in the project tree
   * @return True when the target circuit was loaded or already active
   */
  bool switchToDocument(const std::string& path, bool selectInTree);

  /**
   * @brief Selects a circuit item in the project tree without emitting selection changes.
   * @param circuitPath Project-relative path to select
   */
  void selectProjectTreeDocument(const std::string& path);

  /** @brief Resets the window to a fresh, single-circuit project state. */
  void resetProjectState();

  /**
   * @brief Removes a circuit from every in-memory project container.
   * @param path Project-relative path of the circuit to remove
   */
  void removeDocument(const std::string& path);

  /**
   * @brief Inserts or appends a circuit entry and optionally switches to it.
   * @param file Project file entry to insert
   * @param sceneJson Serialized scene JSON for the circuit
   * @param name Display name to cache for the circuit
   * @param description Description to cache for the circuit
   * @param insertAt Optional insertion index in the circuit list
   * @param switchToPath Circuit path to activate after insertion, or empty to keep
   *                     the current one
   */
  void insertDocument(SILICON::project::Document    document,
                      std::optional<std::ptrdiff_t> insertAt, bool activate);

  /**
   * @brief Generates a unique project-relative circuit path from a requested name.
   * @param requestedName Human-readable circuit name entered by the user
   * @return A non-conflicting path under the project circuits directory
   */
  [[nodiscard]] std::string uniqueDocumentPath(SILICON::project::DocumentKind kind,
                                               const QString& requestedName) const;

  /**
   * @brief Creates an empty serialized scene for a new circuit.
   * @param name Circuit name to store in the payload
   * @return Pretty-printed JSON scene document
   */
  [[nodiscard]] std::string emptyCircuitSceneJson(const std::string& name) const;
  [[nodiscard]] std::string emptySubcircuitSceneJson(const std::string& name) const;
  void                      createDocument(SILICON::project::DocumentKind kind);
  void                      deleteSelectedDocument();
  [[nodiscard]] QTreeWidgetItem*
  projectDocumentSectionItem(SILICON::project::DocumentKind kind) const;

  /**
   * @brief Checks whether the current project contains a circuit path.
   * @param circuitPath Project-relative circuit path
   * @return True when the path exists in the project circuit list
   */
  [[nodiscard]] bool hasDocument(const std::string& path) const;

  /** @brief Returns the logical circuit currently owned by the diagram scene. */
  [[nodiscard]] std::shared_ptr<Circuit> activeCircuit();

#ifdef __EMSCRIPTEN__
  /**
   * @brief Browser keydown callback used to catch Escape outside Qt focus handling.
   * @param eventType Emscripten event type
   * @param keyEvent Browser keyboard event data
   * @param userData Pointer to the LogiFlowWindow instance
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

  /**
   * @brief Serializes the current selection and stores it on the system clipboard.
   * @return True when a non-empty selection was copied successfully
   */
  bool copySelectionToClipboard();

  /** @brief Builds the editable shortcut table shown in the settings dialog. */
  QVector<ShortcutSetting> shortcutSettings() const;

  [[nodiscard]] static std::string defaultMainCircuitPath();
  [[nodiscard]] static SILICON::project::Document defaultCircuitDocument();
  [[nodiscard]] static SILICON::project::ProjectInfo
  defaultProjectInfo(const QString& currentFileName);
  [[nodiscard]] std::string projectMainCircuitPath() const;
  static void               ensureProjectDocuments();
  static void setActionsEnabled(std::initializer_list<QAction*> actions, bool enabled);
  void        syncWasmShortcutCapture();

  /** @brief Main toolbar containing edit, mode, and simulation actions. */
  QToolBar* toolBar = nullptr;

  /** @brief Dock containing the project circuit tree. */
  QDockWidget* componentsDock = nullptr;

  /** @brief Dock containing the current property editor. */
  QDockWidget* propertyDock = nullptr;

  /** @brief Dock containing application log output. */
  QDockWidget* logDock = nullptr;

  /** @brief Tree widget listing project metadata and circuit files. */
  ProjectTree* projectTree = nullptr;

  /** @brief Floating window that hosts the waveform viewer. */
  QDialog* waveformWindow = nullptr;

  /** @brief Widget used to inspect simulation waveforms. */
  waveform::Viewer* waveformViewer = nullptr;

  /** @brief Widget that displays captured application log lines. */
  LogSideView* logSideView = nullptr;

  /** @brief Adapter that forwards Boost.Log output into the Qt log side view. */
  GraphicalLogStream* graphicalLogStream = nullptr;

  /** @brief Graphics scene that owns the editable circuit diagram. */
  DiagramScene* diagramScene = nullptr;

  /** @brief Graphics view used to render and navigate the diagram scene. */
  DiagramView* diagramView = nullptr;
  /** @brief Selects between the graphical circuit view and the HDL source editor. */
  QStackedWidget* editorStack = nullptr;
  /** @brief Line-numbered source editor; writable only while HDL code mode is active. */
  QPlainTextEdit* hdlEditor = nullptr;

  /** @brief Floating searchable component catalog, shown over the diagram viewport. */
  ComponentCatalogOverlay* componentCatalogOverlay = nullptr;

  /** @brief File menu. */
  QMenu* fileMenu = nullptr;

  /** @brief Edit menu. */
  QMenu* editMenu = nullptr;

  /** @brief Help menu. */
  QMenu* helpMenu = nullptr;

  /** @brief Creates a new project. */
  QAction* newAct = nullptr;

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

  /** @brief Toggles FST trace generation for simulations. */
  QAction* toggleFstTraceAct = nullptr;

  /** @brief Cancels the active diagram interaction. */
  QAction* cancelInteractionAct = nullptr;

  /** @brief Opens the component catalog overlay. */
  QAction* openComponentCatalogAct = nullptr;
  QAction* editSubcircuitShapeAct  = nullptr;
  QAction* toggleHdlCodeModeAct    = nullptr;

  /** @brief Activates component placing mode. */
  QAction* setComponentPlacingModeAct = nullptr;

  /** @brief Undo action created from the shared undo stack. */
  QAction* undoAct = nullptr;

  /** @brief Redo action created from the shared undo stack. */
  QAction* redoAct = nullptr;

  /** @brief Undo stack shared by diagram and project operations. */
  QUndoStack* undoStack = nullptr;

  /** @brief Current project file path, or an empty string for unsaved projects. */
  QString currentFileName;

  /** @brief Optional persisted metadata for the current project. */
  std::optional<SILICON::project::ProjectMetadata> currentProjectMetadata;

  /** @brief Optional persisted project information for the current project. */
  std::optional<SILICON::project::ProjectInfo> currentProjectInfo;

  /** @brief Project-relative path of the circuit loaded in the diagram scene. */
  std::string                                 activeDocumentPath;
  std::vector<SILICON::project::ProjectAsset> projectAssets;
  /** @brief True only while an HDL-backed subcircuit source is editable. */
  bool                                     hdlCodeMode = false;
  SILICON::project::ProjectDependencyGraph dependencyGraph;

  /** @brief Lazily shown application about dialog. */
  AboutDialog* aboutDialog = nullptr;
};

/**
 * @brief Spin box that can display an empty mixed-value state in property editors.
 *
 * Used when multiple selected items have different numeric values for the same
 * editable property.
 */
class PropertySpinBox : public QSpinBox {
  Q_OBJECT

public:
  /**
   * @brief Constructs a property spin box without visible step buttons.
   * @param parent Optional Qt parent widget
   */
  explicit PropertySpinBox(QWidget* parent = nullptr);

  /**
   * @brief Sets whether the spin box should display a mixed-value placeholder.
   * @param mixed True when selected items do not share a single value
   * @param placeholder Placeholder text to show while mixed
   */
  void setMixed(bool mixed, const QString& placeholder = QString());

  /** @brief Returns whether the spin box is currently in mixed-value mode. */
  [[nodiscard]] bool isMixed() const;

protected:
  /**
   * @brief Converts the current value to display text, hiding the sentinel mixed value.
   * @param val Numeric value to format
   * @return Display text for the spin box line edit
   */
  [[nodiscard]] QString textFromValue(int val) const override;

private:
  /** @brief Tracks whether the widget is displaying a mixed-value placeholder. */
  bool m_isMixed = false;
};

}  // namespace ui
}  // namespace SILICON
