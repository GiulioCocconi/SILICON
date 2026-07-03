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
#include <QSpinBox>
#include <QUndoStack>
#include <QVector>

class ComponentCatalogOverlay;
class QByteArray;
class QDialog;
class GraphicalLogStream;
class LogSideView;
struct ShortcutSetting;
class WaveformViewer;

#include <ui/common/aboutDialog.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>

#ifdef __EMSCRIPTEN__
  #include <emscripten/html5.h>
#endif

#ifndef QT_NO_CONTEXTMENU
  #include <QContextMenuEvent>
#endif

class LogiFlowWindow : public QMainWindow {
  Q_OBJECT

public:
  LogiFlowWindow();
  ~LogiFlowWindow() override;

  [[nodiscard]] QUndoStack*  getUndoStack() const { return this->undoStack; }
  [[nodiscard]] LogSideView* getLogSideView() const { return this->logSideView; }

protected:
#ifndef QT_NO_CONTEXTMENU
  void contextMenuEvent(QContextMenuEvent* event) override;
#endif  // QT_NO_CONTEXTMENU
  bool eventFilter(QObject* watched, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private slots:
  void newFile();
  void open();
  void save();
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

  void rotate();
  void del();  // Delete is a CPP keyword
  void about() const;
  void openSettings();

  void setNormalMode();
  void setPanMode();
  void setWireCreationMode();
  void setSimulationMode();
  void setComponentPlacingMode();
  void showComponentCatalog();
  void cancelCurrentInteraction();
  void toggleFstTracing(bool enabled);

  void updateStatus() const;
  void selectionChanged();
  void updatePropertyDock();

private:
  void createActions();
  void createMenus();
  void createToolBar();
  void createWaveformWindow();
  void applyStoredSettings();
  void updateComponentCatalogGeometry();

  void setFileName(const QString& fn);
  void loadCircuitContent(const QString& fileName, const QByteArray& fileContent);

#ifdef __EMSCRIPTEN__
  static EM_BOOL wasmKeyDownCallback(int                            eventType,
                                     const EmscriptenKeyboardEvent* keyEvent,
                                     void*                          userData);
  bool           handleWasmEscapeKey();
#endif

  /**
   * @brief Serializes the current selection and stores it on the system clipboard.
   * @return True when a non-empty selection was copied successfully
   */
  bool                     copySelectionToClipboard();
  QVector<ShortcutSetting> shortcutSettings() const;

  QToolBar* toolBar;

  QDockWidget* componentsDock;
  QDockWidget* propertyDock;
  QDockWidget* logDock;

  QDialog*            waveformWindow;
  WaveformViewer*     waveformViewer;
  LogSideView*        logSideView;
  GraphicalLogStream* graphicalLogStream;

  DiagramScene* diagramScene;
  DiagramView*  diagramView;
  ComponentCatalogOverlay* componentCatalogOverlay = nullptr;

  QMenu* fileMenu;
  QMenu* editMenu;
  QMenu* helpMenu;

  QAction* newAct;
  QAction* openAct;
  QAction* saveAct;
  QAction* exportImageAct;
  QAction* exitAct;
  QAction* cutAct;
  QAction* copyAct;
  QAction* pasteAct;
  QAction* rotateAct;
  QAction* deleteAct;
  QAction* aboutAct;
  QAction* settingsAct;

  QAction* setNormalModeAct;
  QAction* setPanModeAct;
  QAction* setWireCreationModeAct;
  QAction* setSimulationModeAct;
  QAction* toggleFstTraceAct;
  QAction* cancelInteractionAct;

  QAction* openComponentCatalogAct;
  QAction* setComponentPlacingModeAct;

  QAction*    undoAct;
  QAction*    redoAct;
  QUndoStack* undoStack;

  QString currentFileName;

  AboutDialog* aboutDialog;
};

class PropertySpinBox : public QSpinBox {
  Q_OBJECT

public:
  explicit PropertySpinBox(QWidget* parent = nullptr);

  void               setMixed(bool mixed, const QString& placeholder = QString());
  [[nodiscard]] bool isMixed() const;

protected:
  [[nodiscard]] QString textFromValue(int val) const override;

private:
  bool m_isMixed = false;
};
