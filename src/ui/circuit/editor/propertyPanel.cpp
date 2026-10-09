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

#include "propertyPanel.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFocusEvent>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSizePolicy>
#include <QTimer>
#include <QWidget>

#include <utils/num_formatting.hpp>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/shell/inputDialogUtils.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/project/projectDocumentPolicy.hpp>
#include <ui/project/projectTree.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {
QLabel* wrappingLabel(QString text, QWidget* parent)
{
  auto* label = new QLabel(std::move(text), parent);
  label->setWordWrap(true);
  // QFormLayout needs the label's width hint to reserve space or wrap the row.
  label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  return label;
}

class DescriptionEdit : public QPlainTextEdit {
public:
  using QPlainTextEdit::QPlainTextEdit;
  std::function<void()> commit;

protected:
  void focusOutEvent(QFocusEvent* event) override
  {
    if (commit)
      commit();
    QPlainTextEdit::focusOutEvent(event);
  }
};
}  // namespace


PropertyPanel::PropertyPanel(QDockWidget* dock, DiagramScene* scene, ProjectTree* tree,
                             QUndoStack*                                   history,
                             std::optional<SILICON::project::ProjectInfo>& projectInfo,
                             const QString& fileName, std::function<void()> rebuildTree)
  : QObject(dock),
    propertyDock(dock),
    diagramScene(scene),
    projectTree(tree),
    undoStack(history),
    currentProjectInfo(projectInfo),
    currentFileName(fileName),
    rebuildProjectTree(std::move(rebuildTree))
{
}

void PropertyPanel::refresh()
{
  auto* selectedProjectItem = projectTree ? projectTree->selectedProjectItem() : nullptr;

  const auto itemKind     = selectedProjectItem
                                ? std::optional{ProjectTree::itemKind(selectedProjectItem)}
                                : std::nullopt;
  const auto itemCategory = selectedProjectItem
                                ? ProjectTree::itemDocumentCategory(selectedProjectItem)
                                : std::nullopt;

  const bool hasProperties = itemKind != ProjectTreeItemKind::Document
                             && itemKind != ProjectTreeItemKind::Architecture;
  propertyDock->setVisible(hasProperties);
  if (!hasProperties)
    return;

  // 1. Assign the container immediately.
  // QDockWidget::setWidget automatically deletes the previous widget.
  auto* container = new QWidget();
  // This dock shares its width with the document dock; its form must not raise the
  // configured minimum width of the entire dock column.
  container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* layout    = new QFormLayout(container);
  layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
    if (!selectedProjectItem) {
      layout->addRow(wrappingLabel(tr("Select a project, circuit, or one or more "
                                         "components to view properties."),
                                   container));
      return;
    }

    if (itemKind == ProjectTreeItemKind::Section && itemCategory) {
      const auto noun =
          *itemCategory == SILICON::project::DocumentCategory::Diagram ? tr("circuit")
          : *itemCategory == SILICON::project::DocumentCategory::Code  ? tr("code file")
          : *itemCategory == SILICON::project::DocumentCategory::Architecture
              ? tr("architecture")
              : tr("binary file");
      layout->addRow(
          wrappingLabel(tr("Select a %1 to view its properties.").arg(noun), container));
      return;
    }

    auto* nameEdit        = new QLineEdit(container);
    auto* descriptionEdit = new DescriptionEdit(container);
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
      QTimer::singleShot(0, this, &PropertyPanel::refresh);
    };

    if (itemKind == ProjectTreeItemKind::Project) {
      if (!currentProjectInfo)
        currentProjectInfo = projectDocumentPolicy::defaultProjectInfo(currentFileName);

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
                      rebuildProjectTree();
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
    }

    layout->addRow(wrappingLabel(tr("Name"), container), nameEdit);
    layout->addRow(wrappingLabel(tr("Description"), container), descriptionEdit);
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
    layout->addRow(wrappingLabel(
        tr("No common configurable properties among selection."), container));
    return;
  }

  // Helper lambda to apply the property to all components and handle validation
  // exceptions
  auto applyProperty = [this, selectedNodes](const std::string&   key,
                                             const PropertyValue& newVal) {
    try {
      auto command = std::make_unique<ModifyPropertyCommand>(key);

      for (GraphicalLogicComponent* node : selectedNodes) {
        const auto oldValue = node->getComponent()->getProperty(key);
        if (oldValue)
          command->addPropertyChange(node, *oldValue, newVal);
      }

      if (command->isEmpty())
        return;

      ModifyPropertyCommand* const submitted = command.release();
      try {
        // push() runs redo() before taking ownership of the command, so a
        // validation exception from applyProperty would otherwise leak it.
        undoStack->push(submitted);
      } catch (...) {
        delete submitted;
        throw;
      }
    } catch (const std::exception& e) {
      SILICON::ui::inputDialog::warning(propertyDock, tr("Invalid Property"), e.what());
      QTimer::singleShot(0, this, &PropertyPanel::refresh);
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

        layout->addRow(wrappingLabel(QString::fromStdString(key), container), checkBox);
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

        layout->addRow(wrappingLabel(QString::fromStdString(key), container), spinBox);
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

          layout->addRow(wrappingLabel(QString::fromStdString(key), container), comboBox);
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

        layout->addRow(wrappingLabel(QString::fromStdString(key), container), lineEdit);
      } else if constexpr (std::is_same_v<T, BusValue>) {
        auto* lineEdit = new QLineEdit(container);
        if (isMixed)
          lineEdit->setPlaceholderText(tr("Mixed values..."));
        else
          lineEdit->setText(QString::fromStdString(
              SILICON::core::formatValue(arg, BusValueFormat::Raw)));

        connect(lineEdit, &QLineEdit::editingFinished, this, [=, this]() {
          if (!lineEdit->isModified())
            return;
          try {
            applyProperty(
                key, SILICON::core::busValueFromBits(lineEdit->text().toStdString()));
            lineEdit->setModified(false);
          } catch (const std::exception& error) {
            SILICON::ui::inputDialog::warning(propertyDock, tr("Invalid Property"),
                                              error.what());
          }
        });

        layout->addRow(wrappingLabel(QString::fromStdString(key), container), lineEdit);
      }
    };

    std::visit(createPropertyWidget, initialValue);
  }
}

// --- Property SpinBox
// ------------------------------------------------------------------

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

QString PropertySpinBox::textFromValue(const int val) const
{
  return m_isMixed && val == minimum() ? QString{} : QSpinBox::textFromValue(val);
}

}  // namespace SILICON::ui
