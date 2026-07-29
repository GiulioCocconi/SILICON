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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>
#include <core/serialization/yosys.hpp>
#include <core/simulator.hpp>
#include <core/subcircuitDefinition.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/graphicalLogStream.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/logSideView.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/waveformViewer.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/componentShapeEditor.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/hdlCodeEditor.hpp>
#include <ui/logiFlow/metadataDescriptionEdit.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

const SILICON::logging::Logger uiLog("ui");

std::pair<std::string, std::string>
circuitMetadata(const SILICON::project::Document& document)
{
  try {
    const auto scene = nlohmann::json::parse(document.sceneJson());
    if (scene.contains("circuit") && scene["circuit"].is_object())
      return {scene["circuit"].value("name", ""),
              scene["circuit"].value("description", "")};
  } catch (const nlohmann::json::exception&) {
  }
  return {};
}

}  // namespace

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
    if (auto* logicComp =
            category_cast<GraphicalLogicComponent>(item, ItemCategory::LogicComponent);
        logicComp && logicComp->getComponent()) {
      selectedNodes.push_back(logicComp);
    }
  }

  if (selectedNodes.empty()) {
    QTreeWidgetItem* selectedProjectItem =
        projectTree ? projectTree->selectedProjectItem() : nullptr;

    if (!selectedProjectItem) {
      layout->addRow(new QLabel(tr(
          "Select a project, circuit, or one or more components\nto view properties.")));
      return;
    }

    const auto itemKind = ProjectTree::itemKind(selectedProjectItem);

    if (itemKind == ProjectTreeItemKind::CircuitSection
        || itemKind == ProjectTreeItemKind::SubcircuitSection) {
      layout->addRow(new QLabel(itemKind == ProjectTreeItemKind::CircuitSection
                                    ? tr("Select a circuit to view its properties.")
                                    : tr("Select a subcircuit to view its properties.")));
      return;
    }

    auto* nameEdit        = new QLineEdit(container);
    auto* descriptionEdit = new MetadataDescriptionEdit(container);
    descriptionEdit->setMinimumHeight(90);

    auto pushMetadataEdit = [this](const QString& label, const std::string& oldValue,
                                   const std::string&           newValue,
                                   MetadataEditCommand::ApplyFn apply) {
      if (oldValue == newValue)
        return;

      undoStack->push(
          new MetadataEditCommand(label, oldValue, newValue, std::move(apply)));
    };

    auto schedulePropertyDockRefresh = [this] {
      QTimer::singleShot(0, this, &LogiFlowWindow::updatePropertyDock);
    };

    if (itemKind == ProjectTreeItemKind::Project) {
      if (!currentProjectInfo)
        currentProjectInfo = defaultProjectInfo(currentFileName);

      nameEdit->setText(QString::fromStdString(currentProjectInfo->name));
      descriptionEdit->setPlainText(
          QString::fromStdString(currentProjectInfo->description));
      descriptionEdit->document()->setModified(false);

      connect(nameEdit, &QLineEdit::editingFinished, this,
              [this, nameEdit, pushMetadataEdit, schedulePropertyDockRefresh] {
                if (!currentProjectInfo || !nameEdit->isModified())
                  return;

                const auto oldValue = currentProjectInfo->name;
                const auto newValue = nameEdit->text().toStdString();
                nameEdit->setModified(false);
                pushMetadataEdit(
                    tr("Modify Project Name"), oldValue, newValue,
                    [this, schedulePropertyDockRefresh](const std::string& value) {
                      if (!currentProjectInfo)
                        return;
                      currentProjectInfo->name = value;
                      updateProjectTreeLabels();
                      schedulePropertyDockRefresh();
                    });
              });
      descriptionEdit->commit = [this, descriptionEdit, pushMetadataEdit,
                                 schedulePropertyDockRefresh] {
        if (!currentProjectInfo || !descriptionEdit->document()->isModified())
          return;

        const auto oldValue = currentProjectInfo->description;
        const auto newValue = descriptionEdit->toPlainText().toStdString();
        descriptionEdit->document()->setModified(false);
        pushMetadataEdit(tr("Modify Project Description"), oldValue, newValue,
                         [this, schedulePropertyDockRefresh](const std::string& value) {
                           if (!currentProjectInfo)
                             return;
                           currentProjectInfo->description = value;
                           schedulePropertyDockRefresh();
                         });
      };
    } else {
      const auto  circuitPath = ProjectTree::documentPath(selectedProjectItem);
      std::string name;
      std::string description;
      if (activeProjectCircuitPath() == circuitPath) {
        const auto circuit = activeCircuit();
        if (circuit) {
          name        = circuit->getName();
          description = circuit->getDescription();
        }
      } else if (const auto* document =
                     SILICON::project::DocumentStore::active().find(circuitPath)) {
        std::tie(name, description) = circuitMetadata(*document);
      }
      nameEdit->setText(QString::fromStdString(name));
      descriptionEdit->setPlainText(QString::fromStdString(description));
      descriptionEdit->document()->setModified(false);

      connect(
          nameEdit, &QLineEdit::editingFinished, this,
          [this, nameEdit, circuitPath, pushMetadataEdit, schedulePropertyDockRefresh] {
            if (!nameEdit->isModified())
              return;

            const auto oldValue =
                activeCircuit() ? activeCircuit()->getName() : std::string{};
            const auto newValue = nameEdit->text().toStdString();
            nameEdit->setModified(false);
            pushMetadataEdit(tr("Modify Circuit Name"), oldValue, newValue,
                             [this, circuitPath,
                              schedulePropertyDockRefresh](const std::string& value) {
                               if (!activateProjectCircuit(circuitPath))
                                 return;
                               if (const auto circuit = activeCircuit()) {
                                 circuit->setName(value);
                                 saveActiveDocumentPayload();
                               }
                               updateProjectTreeLabels();
                               schedulePropertyDockRefresh();
                             });
          });
      descriptionEdit->commit = [this, circuitPath, descriptionEdit, pushMetadataEdit,
                                 schedulePropertyDockRefresh] {
        if (!descriptionEdit->document()->isModified())
          return;

        const auto oldValue =
            activeCircuit() ? activeCircuit()->getDescription() : std::string{};
        const auto newValue = descriptionEdit->toPlainText().toStdString();
        descriptionEdit->document()->setModified(false);
        pushMetadataEdit(
            tr("Modify Circuit Description"), oldValue, newValue,
            [this, circuitPath, schedulePropertyDockRefresh](const std::string& value) {
              if (!activateProjectCircuit(circuitPath))
                return;
              if (const auto circuit = activeCircuit()) {
                circuit->setDescription(value);
                saveActiveDocumentPayload();
              }
              schedulePropertyDockRefresh();
            });
      };
    }

    layout->addRow(tr("Name"), nameEdit);
    layout->addRow(tr("Description"), descriptionEdit);
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
      auto* command = new ModifyPropertyCommand(key);

      for (GraphicalLogicComponent* node : selectedNodes) {
        const auto oldValue = node->getComponent()->getProperty(key);
        if (oldValue)
          command->addPropertyChange(node, *oldValue, newVal);
      }

      if (command->isEmpty()) {
        delete command;
      } else {
        undoStack->push(command);
      }
    } catch (const std::exception& e) {
      SILICON::ui::inputDialog::warning(this, tr("Invalid Property"), e.what());
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
        const auto stringOptions =
            selectedNodes.front()->getComponent()->getStringPropertyOptions(key);
        if (stringOptions) {
          auto* comboBox = new QComboBox(container);

          for (const std::string& option : stringOptions->get()) {
            comboBox->addItem(QString::fromStdString(option));
          }

          if (isMixed) {
            comboBox->setPlaceholderText(tr("Mixed values"));
            comboBox->setCurrentIndex(-1);
          } else {
            comboBox->setCurrentText(QString::fromStdString(arg));
          }

          connect(comboBox, &QComboBox::currentTextChanged, this,
                  [=](const QString& text) {
                    if (text.isEmpty())
                      return;
                    applyProperty(key, text.toStdString());
                  });

          layout->addRow(QString::fromStdString(key), comboBox);
          return;
        }

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

// --- Property SpinBox ------------------------------------------------------------------

PropertySpinBox::PropertySpinBox(QWidget* parent) : QSpinBox(parent)
{
  setButtonSymbols(NoButtons);
}

void PropertySpinBox::setMixed(const bool mixed, const QString& placeholder)
{
  m_isMixed = mixed;

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

}  // namespace ui
}  // namespace SILICON
