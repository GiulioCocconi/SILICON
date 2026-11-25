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
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <utils/num_formatting.hpp>

#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/project/projectDocumentPolicy.hpp>
#include <ui/project/projectTree.hpp>
#include <ui/shell/inputDialogUtils.hpp>

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
  callbacks.connect(diagramScene, &DiagramScene::componentResized, [this] {
    QTimer::singleShot(0, callbacks.context(), [this] { refresh(); });
  });
  callbacks.connect(undoStack, &QUndoStack::indexChanged, [this] {
    if (!diagramScene->selectedItems().empty())
      QTimer::singleShot(0, callbacks.context(), [this] { refresh(); });
  });
}

void PropertyPanel::refresh()
{
  for (const auto& connection : propertyConnections)
    QObject::disconnect(connection);
  propertyConnections.clear();

  auto* selectedProjectItem = projectTree ? projectTree->selectedProjectItem() : nullptr;

  const auto itemKind     = selectedProjectItem
                                ? std::optional{ProjectTree::itemKind(selectedProjectItem)}
                                : std::nullopt;
  const auto itemCategory = selectedProjectItem
                                ? ProjectTree::itemDocumentCategory(selectedProjectItem)
                                : std::nullopt;

  std::vector<GraphicalComponent*> selectedNodes;
  for (QGraphicsItem* item : diagramScene->selectedItems()) {
    if (auto* component =
            category_cast<GraphicalComponent>(item, ItemCategory::Component))
      selectedNodes.push_back(component);
  }

  const bool hasProperties = !selectedNodes.empty()
                             || (itemKind != ProjectTreeItemKind::Document
                                 && itemKind != ProjectTreeItemKind::Architecture);
  propertyDock->setVisible(hasProperties);
  if (!hasProperties)
    return;

  // Defer deletion because a property editor may still be handling its signal.
  if (auto* previous = propertyDock->widget()) {
    previous->hide();
    previous->deleteLater();
  }
  auto* container = new QWidget();
  // This dock shares its width with the document dock; its form must not raise the
  // configured minimum width of the entire dock column.
  container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* layout    = new QFormLayout(container);
  layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
  propertyDock->setWidget(container);

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
          new MetadataEditCommand(label, oldValue, newValue,
                                              callbacks.guard(std::move(apply))));
    };

    auto schedulePropertyDockRefresh = [this] {
      QTimer::singleShot(0, callbacks.context(), [this] { refresh(); });
    };

    if (itemKind == ProjectTreeItemKind::Project) {
      if (!currentProjectInfo)
        currentProjectInfo = projectDocumentPolicy::defaultProjectInfo(currentFileName);

      nameEdit->setText(QString::fromStdString(currentProjectInfo->name));
      descriptionEdit->setPlainText(
          QString::fromStdString(currentProjectInfo->description));
      descriptionEdit->document()->setModified(false);

      callbacks.connect(nameEdit, &QLineEdit::editingFinished,
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
      descriptionEdit->commit = callbacks.guard([this, descriptionEdit, pushMetadataEdit,
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
      });
    }

    layout->addRow(wrappingLabel(tr("Name"), container), nameEdit);
    layout->addRow(wrappingLabel(tr("Description"), container), descriptionEdit);
    return;
  }

  // Components supply presentation and visibility; only common fields are edited.
  std::vector<std::vector<PropertyInfo>> descriptions;
  for (auto* node : selectedNodes) {
    descriptions.push_back(node->propertyInfo());
    propertyConnections.push_back(QObject::connect(
        node, &GraphicalComponent::propertiesChanged, container, callbacks.guard([this] {
          QTimer::singleShot(0, callbacks.context(), [this] { refresh(); });
        })));
  }
  auto commonFields = descriptions.front();
  for (std::size_t index = 0; index < selectedNodes.size(); ++index) {
    const auto& fields = descriptions[index];
    std::erase_if(commonFields, [&](const PropertyInfo& field) {
      const auto it = std::ranges::find(fields, field.key, &PropertyInfo::key);
      return selectedNodes[index]->isPropertyHidden(field.key) || it == fields.end()
             || it->value.index() != field.value.index()
             || it->readOnly != field.readOnly;
    });
    for (auto& field : commonFields)
      field.advanced &=
          std::ranges::find(fields, field.key, &PropertyInfo::key)->advanced;
  }
  if (commonFields.empty()) {
    layout->addRow(wrappingLabel(tr("No common configurable properties among selection."),
                                 container));
    return;
  }

  // Helper lambda to apply the property to all components and handle validation
  // exceptions
  auto applyProperty = [this, selectedNodes](const std::string&   key,
                                             const PropertyValue& newVal) {
    try {
      auto command = std::make_unique<ModifyPropertyCommand>(key);

      for (GraphicalComponent* node : selectedNodes) {
        const auto oldValue = node->getProperty(key);
        if (oldValue)
          command->addPropertyChange(node, *oldValue, newVal);
      }

      if (command->isEmpty())
        return;

      // Keep ownership if redo() throws before QUndoStack accepts the command.
      undoStack->push(command.get());
      static_cast<void>(command.release());
      if (key == "portOrientation") {
        for (const auto* node : selectedNodes) {
          if (const auto* logic = category_cast<GraphicalLogicComponent>(
                  node, ItemCategory::LogicComponent);
              logic && logic->getComponent())
            diagramScene->rememberIoPlacementOrientation(*logic->getComponent());
        }
      }
    } catch (const std::exception& e) {
      const QString message = e.what();
      QTimer::singleShot(0, callbacks.context(), [this, message] {
        SILICON::ui::inputDialog::warning(propertyDock, tr("Invalid Property"), message);
        refresh();
      });
    }
  };

  QFormLayout* advancedLayout = nullptr;
  for (const auto& field : commonFields) {
    const auto& key          = field.key;
    const auto& initialValue = field.value;
    const bool  isMixed      = std::ranges::any_of(descriptions, [&](const auto& fields) {
      return std::ranges::find(fields, key, &PropertyInfo::key)->value != initialValue;
    });
    auto*       propertyLayout = layout;
    if (field.advanced) {
      if (!advancedLayout) {
        auto* toggle = new QToolButton(container);
        toggle->setText(tr("Advanced properties"));
        toggle->setCheckable(true);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* advanced = new QWidget(container);
        advancedLayout = new QFormLayout(advanced);
        advancedLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
        advanced->hide();
        callbacks.connect(toggle, &QToolButton::toggled, [=](bool open) {
          advanced->setVisible(open);
          toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        layout->addRow(toggle);
        layout->addRow(advanced);
      }
      propertyLayout = advancedLayout;
    }
    auto fieldLabel = [&] {
      auto* label = wrappingLabel(
          QString::fromStdString(field.label.empty() ? key : field.label), container);
      label->setToolTip(QString::fromStdString(field.help));
      return label;
    };
    if (field.readOnly) {
      const auto display = std::visit(
          [](const auto& value) -> QString {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>)
              return QString::fromStdString(value);
            else if constexpr (std::is_same_v<T, BusValue>)
              return QString::fromStdString(formatValue(value, BusValueFormat::Raw));
            else if constexpr (std::is_same_v<T, bool>)
              return value ? tr("Yes") : tr("No");
            else
              return QString::number(value);
          },
          initialValue);
      auto* value = wrappingLabel(isMixed ? tr("Mixed values") : display, container);
      value->setObjectName(QString::fromStdString(key));
      propertyLayout->addRow(fieldLabel(), value);
      continue;
    }

    auto createPropertyWidget = [&]<typename T>(const T& arg) {
      if constexpr (std::is_same_v<T, bool>) {
        auto* checkBox = new QCheckBox(container);
        checkBox->setObjectName(QString::fromStdString(key));

        if (isMixed) {
          checkBox->setTristate(true);
          checkBox->setCheckState(Qt::PartiallyChecked);
        } else {
          checkBox->setChecked(arg);
        }

        callbacks.connect(checkBox, &QCheckBox::checkStateChanged, [=](Qt::CheckState state) {
          if (state == Qt::PartiallyChecked)
            return;
          checkBox->setTristate(false);
          applyProperty(key, state == Qt::Checked);
        });

        propertyLayout->addRow(fieldLabel(), checkBox);
      } else if constexpr (std::is_same_v<T, int>) {
        auto*         spinBox = new PropertySpinBox(container);
        spinBox->setObjectName(QString::fromStdString(key));
        constexpr int MIN_VAL = std::numeric_limits<int>::min();
        constexpr int MAX_VAL = std::numeric_limits<int>::max();

        spinBox->setRange(MIN_VAL, MAX_VAL);
        spinBox->setKeyboardTracking(false);

        if (isMixed) {
          spinBox->setMixed(true, tr("Mixed values"));
        } else {
          spinBox->setValue(arg);
        }

        callbacks.connect(spinBox, &QSpinBox::valueChanged, [=](int val) {
          if (val == MIN_VAL && spinBox->isMixed())
            return;
          spinBox->setMixed(false);
          applyProperty(key, val);
        });

        propertyLayout->addRow(fieldLabel(), spinBox);
      } else if constexpr (std::is_same_v<T, std::string>) {
        const auto stringOptions = selectedNodes.front()->getStringPropertyOptions(key);
        if (stringOptions) {
          auto* comboBox = new QComboBox(container);
          comboBox->setObjectName(QString::fromStdString(key));

          for (const std::string& option : stringOptions->get()) {
            comboBox->addItem(QString::fromStdString(option));
          }

          if (isMixed) {
            comboBox->setPlaceholderText(tr("Mixed values"));
            comboBox->setCurrentIndex(-1);
          } else {
            comboBox->setCurrentText(QString::fromStdString(arg));
          }

          callbacks.connect(comboBox, &QComboBox::currentTextChanged,
                  [=](const QString& text) {
                    if (text.isEmpty())
                      return;
                    applyProperty(key, text.toStdString());
                  });

          propertyLayout->addRow(fieldLabel(), comboBox);
          return;
        }

        if (std::ranges::all_of(selectedNodes, [&key](const auto* node) {
              return node->isMultilineProperty(key);
            })) {
          auto* textEdit = new DescriptionEdit(container);
          textEdit->setObjectName(QString::fromStdString(key));
          textEdit->setMinimumHeight(90);
          if (isMixed)
            textEdit->setPlaceholderText(tr("Mixed values..."));
          else
            textEdit->setPlainText(QString::fromStdString(arg));
          textEdit->document()->setModified(false);
          textEdit->commit = callbacks.guard([textEdit, applyProperty, key] {
            if (!textEdit->document()->isModified())
              return;
            textEdit->document()->setModified(false);
            applyProperty(key, textEdit->toPlainText().toStdString());
          });
          propertyLayout->addRow(fieldLabel(), textEdit);
          return;
        }

        auto* lineEdit = new QLineEdit(container);
        lineEdit->setObjectName(QString::fromStdString(key));

        if (isMixed) {
          lineEdit->setPlaceholderText(tr("Mixed values..."));
        } else {
          lineEdit->setText(QString::fromStdString(arg));
        }

        callbacks.connect(lineEdit, &QLineEdit::editingFinished, [=]() {
          if (!lineEdit->isModified())
            return;

          const auto value = lineEdit->text().toStdString();
          lineEdit->setModified(false);
          applyProperty(key, value);
        });

        propertyLayout->addRow(fieldLabel(), lineEdit);
      } else if constexpr (std::is_same_v<T, BusValue>) {
        auto* lineEdit = new QLineEdit(container);
        lineEdit->setObjectName(QString::fromStdString(key));
        if (isMixed)
          lineEdit->setPlaceholderText(tr("Mixed values..."));
        else
          lineEdit->setText(QString::fromStdString(
              SILICON::core::formatValue(arg, BusValueFormat::Raw)));

        callbacks.connect(lineEdit, &QLineEdit::editingFinished, [=, this]() {
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

        propertyLayout->addRow(fieldLabel(), lineEdit);
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
