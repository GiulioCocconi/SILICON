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
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <core/serialization/component_registry.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/serialization/document_conversion.hpp>

namespace SILICON::ui {
namespace {
  [[nodiscard]] std::optional<std::vector<std::string>>
  selectConversionChoices(QWidget*                                           parent,
                          std::vector<SILICON::conversion::ConversionChoice> choices)
  {
    if (choices.size() <= 1) {
      std::vector<std::string> selected;
      for (const auto& choice : choices)
        selected.push_back(choice.id);
      return selected;
    }

    std::ranges::sort(choices, [](const auto& lhs, const auto& rhs) {
      if (lhs.dependencies.size() != rhs.dependencies.size())
        return lhs.dependencies.size() > rhs.dependencies.size();
      return lhs.label < rhs.label;
    });

    std::unordered_map<std::string, const SILICON::conversion::ConversionChoice*>
        choicesById;
    for (const auto& choice : choices)
      choicesById.emplace(choice.id, &choice);

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Select Conversion Items"));
    dialog.setModal(true);
    dialog.resize(560, 420);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QObject::tr("Select one or more items. Their dependencies are converted "
                    "automatically."),
        &dialog));

    auto* tree = new QTreeWidget(&dialog);
    tree->setColumnCount(1);
    tree->setHeaderLabels({QObject::tr("Item")});
    tree->setRootIsDecorated(true);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    constexpr int explicitlySelectedRole = Qt::UserRole;
    constexpr int choiceIdRole           = Qt::UserRole + 1;

    const auto addDependencies = [&choicesById](this auto&&        addDependencies,
                                                QTreeWidgetItem*   parentItem,
                                                const std::string& choiceId) -> void {
      const auto choice = choicesById.find(choiceId);
      if (choice == choicesById.end())
        return;
      for (const auto& dependency : choice->second->dependencies) {
        auto*      dependencyItem   = new QTreeWidgetItem(parentItem);
        const auto dependencyChoice = choicesById.find(dependency);
        dependencyItem->setText(
            0, QString::fromStdString(dependencyChoice == choicesById.end()
                                          ? dependency
                                          : dependencyChoice->second->label));
        dependencyItem->setFlags(dependencyItem->flags() & ~Qt::ItemIsUserCheckable);
        addDependencies(dependencyItem, dependency);
      }
    };

    for (const auto& choice : choices) {
      auto* item = new QTreeWidgetItem(tree);
      item->setText(0, QString::fromStdString(choice.label));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      item->setData(0, explicitlySelectedRole, false);
      item->setData(0, choiceIdRole, QString::fromStdString(choice.id));
      addDependencies(item, choice.id);
    }
    tree->collapseAll();
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(tree);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* importButton = buttons->button(QDialogButtonBox::Ok);
    importButton->setText(QObject::tr("Import"));
    importButton->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(
        tree, &QTreeWidget::itemChanged, &dialog,
        [tree, importButton, &choicesById](QTreeWidgetItem* changedItem, int) {
          const QSignalBlocker blocker(tree);
          if (!changedItem->parent()
              && changedItem->flags().testFlag(Qt::ItemIsEnabled)) {
            changedItem->setData(0, explicitlySelectedRole,
                                 changedItem->checkState(0) == Qt::Checked);
          }

          std::vector<std::string> roots;
          for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            const auto* item = tree->topLevelItem(index);
            if (item->data(0, explicitlySelectedRole).toBool())
              roots.push_back(item->data(0, choiceIdRole).toString().toStdString());
          }

          importButton->setEnabled(!roots.empty());
          std::vector<std::string> dependencies;
          const auto               collectDependencies =
              [&choicesById, &dependencies](this auto&&        collectDependencies,
                                            const std::string& id) -> void {
            const auto choice = choicesById.find(id);
            if (choice == choicesById.end())
              return;
            for (const auto& dependency : choice->second->dependencies) {
              if (std::ranges::contains(dependencies, dependency))
                continue;
              dependencies.push_back(dependency);
              collectDependencies(dependency);
            }
          };
          for (const auto& root : roots)
            collectDependencies(root);

          for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            auto*      item       = tree->topLevelItem(index);
            const auto name       = item->data(0, choiceIdRole).toString().toStdString();
            const bool selected   = item->data(0, explicitlySelectedRole).toBool();
            const bool dependency = std::ranges::contains(dependencies, name);

            auto flags = item->flags() | Qt::ItemIsUserCheckable;
            flags.setFlag(Qt::ItemIsEnabled, !dependency);
            item->setFlags(flags);
            item->setCheckState(0, selected || dependency ? Qt::Checked : Qt::Unchecked);

            auto font = item->font(0);
            font.setBold(selected && !dependency);
            font.setItalic(dependency);
            item->setFont(0, font);
          }
        });

    if (dialog.exec() != QDialog::Accepted)
      return std::nullopt;

    std::vector<std::string> roots;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      const auto* item = tree->topLevelItem(index);
      if (item->data(0, explicitlySelectedRole).toBool())
        roots.push_back(item->data(0, choiceIdRole).toString().toStdString());
    }
    return roots;
  }

}  // namespace

void LogiFlowWindow::convertActiveDocument()
{
  const auto* source = projectContext.documents().find(activeDocumentPath);
  if (!source)
    return;

  const auto converters = documentConvertersFor(source->getType());
  std::vector<SILICON::project::DocumentType> targets;
  QStringList                                 labels;
  for (const auto* converter : converters) {
    if (!converter->available)
      continue;
    targets.push_back(converter->target);
    labels.push_back(documentTypeName(converter->target));
  }

  if (targets.empty())
    return;
  if (targets.size() == 1) {
    convertActiveDocumentTo(targets.front());
    return;
  }

  SILICON::ui::inputDialog::getItem(
      this, tr("Convert Document"), tr("Target format"), labels, 0, false,
      [this, labels = std::move(labels),
       targets = std::move(targets)](const QString& selected) {
        const auto index = labels.indexOf(selected);
        if (index >= 0 && index < static_cast<qsizetype>(targets.size()))
          convertActiveDocumentTo(targets[static_cast<std::size_t>(index)]);
      });
}

void LogiFlowWindow::convertActiveDocumentTo(const SILICON::project::DocumentType target)
{
  try {
    saveActiveDocumentPayload();
    const auto  sourcePath = activeDocumentPath;
    const auto& store      = projectContext.documents();
    const auto* source     = store.find(sourcePath);
    if (!source)
      throw std::runtime_error("The active document no longer exists");

    auto prepared = prepareDocumentConversion(
        *source, target, store.getDocuments(),
        SILICON::core::ComponentRegistry::instance(), circuitResolver);
    auto selected = selectConversionChoices(this, prepared.choices);
    if (!selected)
      return;

    auto       result      = prepared.execute(*selected);
    const auto commandText = tr("Convert to %1").arg(documentTypeName(target));

    commitDocumentChanges(std::move(result.documents), sourcePath, result.activatePath,
                          commandText, tr("Code Conversion Error"));
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::critical(
        this, tr("Code Conversion Error"),
        tr("Failed to convert the active document:\n%1").arg(error.what()));
  }
}

}  // namespace SILICON::ui
