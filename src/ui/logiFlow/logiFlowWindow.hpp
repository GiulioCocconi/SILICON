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

#include <vector>

#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QToolBar>
#include <QUndoStack>

#include <ui/common/aboutDialog.hpp>
#include <ui/common/componentSearchBox.hpp>
#include <ui/common/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/common/graphicalWire.hpp>
#include <ui/common/icons.hpp>

#include <ui/logiFlow/components/graphicalGates.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>

#ifndef QT_NO_CONTEXTMENU
#  include <QContextMenuEvent>
#endif

class LogiFlowWindow : public QMainWindow {
  Q_OBJECT

public:
  LogiFlowWindow();
  ~LogiFlowWindow() override;

protected:
#ifndef QT_NO_CONTEXTMENU
  void contextMenuEvent(QContextMenuEvent* event) override;
#endif  // QT_NO_CONTEXTMENU
  void resizeEvent(QResizeEvent* event) override;

private slots:
  void newFile() {}
  void open() {}
  void save() {}
  void exportImage() {}
  void cut()
  {
    copy();
    del();
  }
  void copy() {}
  void paste() {}
  void rotate();
  void del();  // Delete is a CPP keyword
  void about() const;

  void setNormalMode();
  void setPanMode();
  void setWireCreationMode();
  void setSimulationMode();
  void setComponentPlacingMode();

  void updateStatus() const;
  void selectionChanged();
  void updatePropertyDock();

private:
  void createActions();
  void createMenus();
  void createToolBar();

  QToolBar* toolBar;

  QDockWidget* componentsDock;
  QDockWidget* propertyDock;

  DiagramScene* diagramScene;
  DiagramView*  diagramView;

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

  QAction* setNormalModeAct;
  QAction* setPanModeAct;
  QAction* setWireCreationModeAct;
  QAction* setSimulationModeAct;

  QAction* setComponentPlacingModeAct;

  QAction*    undoAct;
  QAction*    redoAct;
  QUndoStack* undoStack;

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