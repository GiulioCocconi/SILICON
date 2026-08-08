/*
  Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include "projectTree.hpp"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QIcon>
#include <QSignalBlocker>

#include <nlohmann/json.hpp>

#include <ui/common/icons.hpp>


namespace SILICON {
namespace ui {

namespace {

constexpr int ItemKindRole = Qt::UserRole;
constexpr int PathRole     = Qt::UserRole + 1;

QString documentDisplayName(const SILICON::project::Document& document)
{
  try {
    const auto scene = nlohmann::json::parse(document.getSceneJson());
    if (scene.contains("circuit") && scene["circuit"].is_object()) {
      const auto name = scene["circuit"].value("name", "");
      if (!name.empty())
        return QString::fromStdString(name);
    }
  } catch (const nlohmann::json::exception&) {
  }

  const QString fileName = QFileInfo(QString::fromStdString(document.getPath())).baseName();
  return fileName.isEmpty() ? QString::fromStdString(document.getPath()) : fileName;
}

ProjectTreeItemKind sectionKind(const SILICON::project::DocumentKind kind)
{
  return kind == SILICON::project::DocumentKind::Circuit
             ? ProjectTreeItemKind::CircuitSection
             : ProjectTreeItemKind::SubcircuitSection;
}

ProjectTreeItemKind documentKind(const SILICON::project::DocumentKind kind)
{
  return kind == SILICON::project::DocumentKind::Circuit
             ? ProjectTreeItemKind::Circuit
             : ProjectTreeItemKind::Subcircuit;
}

}  // namespace

ProjectTree::ProjectTree(QWidget* parent) : QTreeWidget(parent)
{
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setContextMenuPolicy(Qt::CustomContextMenu);
}

void ProjectTree::rebuild(const SILICON::project::ProjectInfo&           project,
                          const std::vector<SILICON::project::Document>& circuits,
                          const std::vector<SILICON::project::Document>& subcircuits,
                          const std::string& activeDocumentPath)
{
  const QSignalBlocker blocker(this);
  clear();

  auto* projectItem = new QTreeWidgetItem(this);
  projectItem->setText(0, QString::fromStdString(project.name));
  projectItem->setData(0, ItemKindRole, static_cast<int>(ProjectTreeItemKind::Project));
  projectItem->setExpanded(true);
  addSection(projectItem, SILICON::project::DocumentKind::Circuit, circuits);
  addSection(projectItem, SILICON::project::DocumentKind::Subcircuit, subcircuits);
  expandAll();

  if (!activeDocumentPath.empty())
    selectDocument(activeDocumentPath);
}

void ProjectTree::updateLabels(const SILICON::project::ProjectInfo&           project,
                               const std::vector<SILICON::project::Document>& circuits,
                               const std::vector<SILICON::project::Document>& subcircuits)
{
  const QSignalBlocker blocker(this);
  if (topLevelItemCount() == 0)
    return;

  topLevelItem(0)->setText(0, QString::fromStdString(project.name));
  for (const auto& [kind, documents] :
       {std::pair{SILICON::project::DocumentKind::Circuit, &circuits},
        std::pair{SILICON::project::DocumentKind::Subcircuit, &subcircuits}}) {
    auto* section = sectionFor(kind);
    if (!section)
      continue;
    for (int i = 0; i < section->childCount() && i < static_cast<int>(documents->size());
         ++i) {
      const auto& document = documents->at(static_cast<std::size_t>(i));
      section->child(i)->setText(
          0, kind == SILICON::project::DocumentKind::Circuit
                 ? documentDisplayName(document)
                 : QString::fromStdString(
                       document.subcircuitSlug().value_or(document.getPath())));
    }
  }
}

void ProjectTree::selectDocument(const std::string& path)
{
  const QSignalBlocker blocker(this);
  clearSelection();
  const auto kind    = SILICON::project::classifyDocumentPath(path);
  auto*      section = kind ? sectionFor(*kind) : nullptr;
  if (!section)
    return;

  const auto targetPath = QString::fromStdString(path);
  for (int i = 0; i < section->childCount(); ++i) {
    auto* item = section->child(i);
    if (item->data(0, PathRole).toString() == targetPath) {
      item->setSelected(true);
      setCurrentItem(item);
      return;
    }
  }
}

void ProjectTree::clearDocumentSelection()
{
  const QSignalBlocker blocker(this);
  clearSelection();
}

QTreeWidgetItem* ProjectTree::selectedProjectItem() const
{
  const auto items = selectedItems();
  return items.empty() ? nullptr : items.front();
}

QTreeWidgetItem* ProjectTree::sectionFor(const SILICON::project::DocumentKind kind) const
{
  if (topLevelItemCount() == 0)
    return nullptr;
  auto* projectItem = topLevelItem(0);
  for (int i = 0; i < projectItem->childCount(); ++i) {
    auto* child = projectItem->child(i);
    if (itemKind(child) == sectionKind(kind))
      return child;
  }
  return nullptr;
}

ProjectTreeItemKind ProjectTree::itemKind(const QTreeWidgetItem* item)
{
  return static_cast<ProjectTreeItemKind>(item->data(0, ItemKindRole).toInt());
}

std::string ProjectTree::documentPath(const QTreeWidgetItem* item)
{
  return item->data(0, PathRole).toString().toStdString();
}

void ProjectTree::addSection(QTreeWidgetItem*                               projectItem,
                             const SILICON::project::DocumentKind           kind,
                             const std::vector<SILICON::project::Document>& documents)
{
  const bool circuits = kind == SILICON::project::DocumentKind::Circuit;
  auto*      section  = new QTreeWidgetItem(projectItem);
  section->setText(0, circuits ? tr("Circuits") : tr("Subcircuits"));
  section->setData(0, ItemKindRole, static_cast<int>(sectionKind(kind)));
  section->setExpanded(true);

  for (const auto& document : documents) {
    auto* item = new QTreeWidgetItem(section);
    item->setText(0, circuits ? documentDisplayName(document)
                              : QString::fromStdString(
                                    document.subcircuitSlug().value_or(document.getPath())));
    item->setIcon(0, Icon("circuit-board"));
    item->setData(0, ItemKindRole, static_cast<int>(documentKind(kind)));
    item->setData(0, PathRole, QString::fromStdString(document.getPath()));
  }
}

}  // namespace ui
}  // namespace SILICON
