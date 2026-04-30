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

#include "logiFlowWindow.hpp"

#include <limits>
#include <ranges>
#include <stdexcept>
#include <variant>

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QTimer>

#include <ui/common/diagramScene.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

void PropertySpinBox::setMixed(bool mixed, const QString& placeholder)
{
  m_isMixed                = mixed;
  const auto buttonSymbols = mixed ? NoButtons : UpDownArrows;

  setButtonSymbols(buttonSymbols);

  if (mixed) {
    lineEdit()->setPlaceholderText(placeholder);
    setValue(minimum());
  } else {
    lineEdit()->setPlaceholderText("");
  }
}

bool PropertySpinBox::isMixed() const
{
  return m_isMixed;
}

QString PropertySpinBox::textFromValue(int val) const
{
  if (m_isMixed && val == minimum()) {
    return "";
  }
  return QSpinBox::textFromValue(val);
}

LogiFlowWindow::~LogiFlowWindow()
{
  if (diagramScene) {
    disconnect(diagramScene, nullptr, this, nullptr);
  }
}

LogiFlowWindow::LogiFlowWindow()
{
  const auto centralWidget = new QWidget();
  setCentralWidget(centralWidget);

  const auto layout = new QHBoxLayout();
  layout->setContentsMargins(5, 5, 5, 5);
  centralWidget->setLayout(layout);

  componentsDock = new QDockWidget(this);
  propertyDock   = new QDockWidget(this);

  addDockWidget(Qt::LeftDockWidgetArea, componentsDock);
  addDockWidget(Qt::LeftDockWidgetArea, propertyDock);

  propertyDock->setFeatures(QDockWidget::DockWidgetMovable);
  componentsDock->setFeatures(QDockWidget::DockWidgetMovable);

  propertyDock->setWindowTitle("Properties");
  componentsDock->setWindowTitle("Components");

  splitDockWidget(componentsDock, propertyDock, Qt::Vertical);

  diagramScene = new DiagramScene(this);
  diagramView  = new DiagramView(this);
  diagramView->setScene(diagramScene);

  connect(diagramScene, &DiagramScene::modeChanged, this, &LogiFlowWindow::updateStatus);
  updateStatus();

  connect(diagramScene, &DiagramScene::selectionChanged, this,
          &LogiFlowWindow::selectionChanged);

  layout->addWidget(diagramView);

  aboutDialog = new AboutDialog("SILICON", this);

  undoStack = new QUndoStack(this);

  createActions();
  createMenus();
  createToolBar();

  setWindowTitle(tr("SILICON LogiFlow"));
  setMinimumSize(160, 160);
}

void LogiFlowWindow::createActions()
{
  newAct         = new QAction(Icon("file"), tr("&New"), this);
  openAct        = new QAction(Icon("open"), tr("&Open..."), this);
  saveAct        = new QAction(Icon("save"), tr("&Save"), this);
  exportImageAct = new QAction(Icon("export"), tr("&Export..."), this);
  exitAct        = new QAction(Icon("xmark"), tr("E&xit"), this);
  cutAct         = new QAction(Icon("cut"), tr("Cu&t"), this);
  copyAct        = new QAction(Icon("copy"), tr("&Copy"), this);
  pasteAct       = new QAction(Icon("paste"), tr("&Paste"), this);
  rotateAct      = new QAction(Icon("rotate"), tr("&Rotate"), this);
  deleteAct      = new QAction(Icon("delete"), tr("&Delete"), this);
  aboutAct       = new QAction(Icon("info"), tr("&About"), this);

  undoAct = undoStack->createUndoAction(this, tr("&Undo"));
  undoAct->setIcon(Icon("undo"));

  redoAct = undoStack->createRedoAction(this, tr("&Redo"));
  redoAct->setIcon(Icon("redo"));

  // The rotate, cut, copy and delete actions should be disabled when no component is
  // selected
  rotateAct->setEnabled(false);
  cutAct->setEnabled(false);
  copyAct->setEnabled(false);
  deleteAct->setEnabled(false);

  setNormalModeAct       = new QAction(Icon("mouse-pointer"), "", this);
  setPanModeAct          = new QAction(Icon("pan"), "", this);
  setWireCreationModeAct = new QAction(Icon("link"), "", this);
  setSimulationModeAct   = new QAction(Icon("play"), "", this);

  setComponentPlacingModeAct = new QAction(Icon("plus"), "", this);

  newAct->setShortcuts(QKeySequence::New);
  openAct->setShortcuts(QKeySequence::Open);
  saveAct->setShortcuts(QKeySequence::Save);
  exportImageAct->setShortcuts(QKeySequence::Print);
  exitAct->setShortcuts(QKeySequence::Quit);
  undoAct->setShortcuts(QKeySequence::Undo);
  redoAct->setShortcuts(QKeySequence::Redo);
  cutAct->setShortcuts(QKeySequence::Cut);
  copyAct->setShortcuts(QKeySequence::Copy);
  rotateAct->setShortcut(Qt::AltModifier | Qt::Key_R);
  deleteAct->setShortcuts(QKeySequence::Delete);
  pasteAct->setShortcuts(QKeySequence::Paste);

  setWireCreationModeAct->setShortcut(Qt::AltModifier | Qt::Key_W);
  setSimulationModeAct->setShortcut(Qt::AltModifier | Qt::ControlModifier | Qt::Key_S);
  setComponentPlacingModeAct->setShortcut(Qt::AltModifier | Qt::Key_A);

  newAct->setStatusTip(tr("Create a new file"));
  openAct->setStatusTip(tr("Open an existing logiFlow file"));
  saveAct->setStatusTip(tr("Save the circuit to disk"));
  exportImageAct->setStatusTip(tr("Export the circuit as an image"));
  exitAct->setStatusTip(tr("Exit the application"));
  undoAct->setStatusTip(tr("Undo the last operation"));
  redoAct->setStatusTip(tr("Redo the last operation"));
  cutAct->setStatusTip(tr("Cut the current selection's contents to the clipboard"));
  pasteAct->setStatusTip(tr("Paste the clipboard's contents into the current selection"));
  deleteAct->setStatusTip(tr("Delete selected components"));
  aboutAct->setStatusTip(tr("Show the application's about box"));

  connect(newAct, &QAction::triggered, this, &LogiFlowWindow::newFile);
  connect(openAct, &QAction::triggered, this, &LogiFlowWindow::open);
  connect(saveAct, &QAction::triggered, this, &LogiFlowWindow::save);
  connect(exportImageAct, &QAction::triggered, this, &LogiFlowWindow::exportImage);
  connect(exitAct, &QAction::triggered, this, &QWidget::close);
  connect(cutAct, &QAction::triggered, this, &LogiFlowWindow::cut);
  connect(copyAct, &QAction::triggered, this, &LogiFlowWindow::copy);
  connect(pasteAct, &QAction::triggered, this, &LogiFlowWindow::paste);
  connect(rotateAct, &QAction::triggered, this, &LogiFlowWindow::rotate);
  connect(deleteAct, &QAction::triggered, this, &LogiFlowWindow::del);
  connect(aboutAct, &QAction::triggered, this, &LogiFlowWindow::about);

  connect(setNormalModeAct, &QAction::triggered, this, &LogiFlowWindow::setNormalMode);
  connect(setPanModeAct, &QAction::triggered, this, &LogiFlowWindow::setPanMode);
  connect(setWireCreationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setWireCreationMode);
  connect(setSimulationModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setSimulationMode);
  connect(setComponentPlacingModeAct, &QAction::triggered, this,
          &LogiFlowWindow::setComponentPlacingMode);
}

void LogiFlowWindow::createMenus()
{
  fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(newAct);
  fileMenu->addAction(openAct);
  fileMenu->addAction(saveAct);
  fileMenu->addAction(exportImageAct);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAct);

  editMenu = menuBar()->addMenu(tr("&Edit"));
  editMenu->addAction(undoAct);
  editMenu->addAction(redoAct);
  editMenu->addSeparator();
  editMenu->addAction(cutAct);
  editMenu->addAction(copyAct);
  editMenu->addAction(pasteAct);
  editMenu->addAction(rotateAct);
  editMenu->addAction(deleteAct);
  editMenu->addSeparator();

  helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(aboutAct);
}

void LogiFlowWindow::createToolBar()
{
  toolBar = new QToolBar(this);
  toolBar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
  toolBar->setFloatable(false);

  toolBar->addAction(newAct);
  toolBar->addAction(openAct);
  toolBar->addAction(saveAct);

  toolBar->addSeparator();

  toolBar->addAction(setNormalModeAct);
  toolBar->addAction(setPanModeAct);
  toolBar->addAction(setWireCreationModeAct);
  toolBar->addAction(setSimulationModeAct);

  toolBar->addSeparator();
  toolBar->addAction(setComponentPlacingModeAct);

  addToolBar(toolBar);
}

#ifndef QT_NO_CONTEXTMENU
void LogiFlowWindow::contextMenuEvent(QContextMenuEvent* event)
{
  QMenu menu(this);
  menu.addAction(cutAct);
  menu.addAction(copyAct);
  menu.addAction(pasteAct);
  menu.addAction(rotateAct);
  menu.addAction(deleteAct);
  menu.exec(event->globalPos());
}
#endif  // QT_NO_CONTEXTMENU

/* ACTIONS IMPLEMENTATION */

void LogiFlowWindow::rotate()
{
  auto selectedComponents =
      std::ranges::views::filter(diagramScene->selectedItems(),
                                 [](auto el) { return el->type() >= COMPONENT; })
      | std::ranges::to<std::vector>();

  switch (diagramScene->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: {
      if (selectedComponents.size() != 1)
        return;

      auto component = qgraphicsitem_cast<GraphicalComponent*>(selectedComponents[0]);
      component->rotate();
      break;
    }
    case InteractionMode::COMPONENT_PLACING_MODE: {
      diagramScene->getComponentToBeDrawn()->rotate();
      break;
    }

    default: return;
  }
}

void LogiFlowWindow::del()
{
  // Collect items to delete first, then remove all from the scene before deleting
  // any. This avoids crashes where removeItem() on one item triggers scene callbacks
  // (e.g. collision detection via virtual getCollisionRect()) that reach items that
  // have already been deleted in a prior iteration — which can corrupt vtable lookups
  // and produce "pure virtual method called" errors.

  auto itemsToDelete = diagramScene->selectedItems() | std::views::filter([](auto el) {
                         // Trying to remove non user-defined components leads to crash
                         return el->type() > UNKNOWN;
                       })
                       | std::ranges::to<std::vector>();

  for (auto* item : itemsToDelete)
    diagramScene->removeItem(item);

  for (const auto* item : itemsToDelete)
    delete item;
}

void LogiFlowWindow::about() const
{
  aboutDialog->show();
}

void LogiFlowWindow::setNormalMode()
{
  diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);
}

void LogiFlowWindow::setPanMode()
{
  diagramScene->setInteractionMode(InteractionMode::PAN_MODE);
}

void LogiFlowWindow::setWireCreationMode()
{
  diagramScene->setInteractionMode(InteractionMode::WIRE_CREATION_MODE);
}

void LogiFlowWindow::setSimulationMode()
{
  diagramScene->setInteractionMode(InteractionMode::SIMULATION_MODE);
}

void LogiFlowWindow::setComponentPlacingMode()
{
  diagramScene->setInteractionMode(InteractionMode::COMPONENT_PLACING_MODE);
}

void LogiFlowWindow::updateStatus() const
{
  QString modeMsg = "Interaction Mode: ";

  switch (diagramScene->getInteractionMode()) {
    case InteractionMode::NORMAL_MODE: modeMsg += "NORMAL"; break;
    case InteractionMode::COMPONENT_PLACING_MODE: modeMsg += "COMPONENT PLACING"; break;
    case InteractionMode::WIRE_CREATION_MODE: modeMsg += "WIRE CREATION"; break;
    case InteractionMode::PAN_MODE: modeMsg += "PAN"; break;
    case InteractionMode::SIMULATION_MODE: modeMsg += "SIMULATION"; break;
    default: throw std::logic_error("Unhandled InteractionMode in modeToString");
  }

  statusBar()->showMessage(modeMsg);
}
void LogiFlowWindow::selectionChanged()
{
  auto interactionMode = diagramScene->getInteractionMode();
  // Enable rotation only when a single component is selected or when in component placing
  // mode

  rotateAct->setEnabled((interactionMode == InteractionMode::NORMAL_MODE
                         && diagramScene->selectedItems().size() == 1)
                        || interactionMode == InteractionMode::COMPONENT_PLACING_MODE);

  // Enable cut, copy, paste and delete only when in normal mode and some items are
  // selected
  const bool cutCopyDelete = interactionMode == InteractionMode::NORMAL_MODE
                             && !diagramScene->selectedItems().empty();

  cutAct->setEnabled(cutCopyDelete);
  copyAct->setEnabled(cutCopyDelete);
  deleteAct->setEnabled(cutCopyDelete);

  updatePropertyDock();
}

void LogiFlowWindow::updatePropertyDock()
{
  // 1. Assign the container immediately.
  // QDockWidget::setWidget automatically deletes the previous widget.
  auto* container = new QWidget();
  auto* layout    = new QFormLayout(container);
  propertyDock->setWidget(container);

  // 2. Gather selected logic components cleanly
  std::vector<GraphicalLogicComponent*> selectedNodes;
  for (QGraphicsItem* item : diagramScene->selectedItems()) {
    if (item->type() >= COMPONENT) {
      if (auto* logicComp = dynamic_cast<GraphicalLogicComponent*>(item)) {
        if (logicComp->getComponent() != nullptr) {
          selectedNodes.push_back(logicComp);
        }
      }
    }
  }

  if (selectedNodes.empty()) {
    layout->addRow(
        new QLabel(tr("Select one or more components\nto view their properties.")));
    return;
  }

  // 3. Intersect properties to find common configurable keys
  auto commonProps = selectedNodes.front()->getComponent()->getProperties();

  for (const GraphicalLogicComponent* node : selectedNodes | std::views::drop(1)) {
    const auto& props = node->getComponent()->getProperties();

    std::erase_if(commonProps, [&](const auto& pair) {
      const auto& [key, val] = pair;
      auto it                = props.find(key);
      return it == props.end() || it->second.index() != val.index();
    });
  }

  if (commonProps.empty()) {
    layout->addRow(new QLabel(tr("No common configurable\nproperties among selection.")));
    return;
  }

  // Helper lambda to apply the property to all components and handle validation
  // exceptions
  auto applyProperty = [this, selectedNodes](const std::string&   key,
                                             const PropertyValue& newVal) {
    try {
      for (GraphicalLogicComponent* node : selectedNodes) {
        node->getComponent()->setProperty(key, newVal);
        node->update();
      }
    } catch (const std::exception& e) {
      QMessageBox::warning(this, tr("Invalid Property"), e.what());
      QTimer::singleShot(0, this, &LogiFlowWindow::updatePropertyDock);
    }
  };

  // 4. Build UI for common properties
  for (const auto& [key, initialValue] : commonProps) {
    // Check if the value differs across the selection
    const bool isMixed = std::ranges::any_of(
        selectedNodes | std::views::drop(1), [&](const GraphicalLogicComponent* node) {
          return node->getComponent()->getProperty(key) != initialValue;
        });

    auto createPropertyWidget = [&]<typename T>(const T& arg) {
      if constexpr (std::is_same_v<T, bool>) {
        auto* checkBox = new QCheckBox(container);

        if (isMixed) {
          checkBox->setTristate(true);
          checkBox->setCheckState(Qt::PartiallyChecked);
        } else {
          checkBox->setChecked(arg);
        }

        connect(checkBox, &QCheckBox::checkStateChanged, this, [=](Qt::CheckState state) {
          if (state == Qt::PartiallyChecked)
            return;
          checkBox->setTristate(false);
          applyProperty(key, state == Qt::Checked);
        });

        layout->addRow(QString::fromStdString(key), checkBox);
      } else if constexpr (std::is_same_v<T, int>) {
        auto*         spinBox = new PropertySpinBox(container);
        constexpr int MIN_VAL = std::numeric_limits<int>::min();
        constexpr int MAX_VAL = std::numeric_limits<int>::max();

        spinBox->setRange(MIN_VAL, MAX_VAL);

        if (isMixed) {
          spinBox->setMixed(true, tr("Mixed values"));
        } else {
          spinBox->setValue(arg);
        }

        connect(spinBox, &QSpinBox::valueChanged, this, [=](int val) {
          if (val == MIN_VAL && spinBox->isMixed())
            return;
          spinBox->setMixed(false);
          applyProperty(key, val);
        });

        layout->addRow(QString::fromStdString(key), spinBox);
      } else if constexpr (std::is_same_v<T, std::string>) {
        auto* lineEdit = new QLineEdit(container);

        if (isMixed) {
          lineEdit->setPlaceholderText(tr("Mixed values..."));
        } else {
          lineEdit->setText(QString::fromStdString(arg));
        }

        connect(lineEdit, &QLineEdit::editingFinished, this, [=]() {
          if (!lineEdit->isModified())
            return;

          applyProperty(key, lineEdit->text().toStdString());
          lineEdit->setModified(false);
        });

        layout->addRow(QString::fromStdString(key), lineEdit);
      }
    };

    std::visit(createPropertyWidget, initialValue);
  }
}