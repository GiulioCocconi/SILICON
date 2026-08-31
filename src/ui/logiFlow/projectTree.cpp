/*
Copyright (c) 2026. Giulio Cocconi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include "projectTree.hpp"

#include <ranges>

#include <QAbstractItemView>
#include <QFileInfo>
#include <QFont>
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
      const auto scene = nlohmann::json::parse(document.getContents());
      if (scene.contains("circuit") && scene["circuit"].is_object()) {
        const auto name = scene["circuit"].value("name", "");
        if (!name.empty())
          return QString::fromStdString(name);
      }
    } catch (const nlohmann::json::exception&) {
    }

    const QString fileName =
        QFileInfo(QString::fromStdString(document.getPath())).baseName();
    return fileName.isEmpty() ? QString::fromStdString(document.getPath()) : fileName;
  }

  ProjectTreeItemKind sectionKind(const SILICON::project::DocumentType type)
  {
    if (type == SILICON::project::DocumentType::Circuit)
      return ProjectTreeItemKind::CircuitSection;
    if (type == SILICON::project::DocumentType::Subcircuit)
      return ProjectTreeItemKind::SubcircuitSection;
    return type == SILICON::project::DocumentType::Code
               ? ProjectTreeItemKind::CodeSection
               : ProjectTreeItemKind::BinarySection;
  }

  ProjectTreeItemKind documentKind(const SILICON::project::DocumentType type)
  {
    if (type == SILICON::project::DocumentType::Circuit)
      return ProjectTreeItemKind::Circuit;
    if (type == SILICON::project::DocumentType::Subcircuit)
      return ProjectTreeItemKind::Subcircuit;
    return type == SILICON::project::DocumentType::Code ? ProjectTreeItemKind::CodeFile
                                                        : ProjectTreeItemKind::BinaryFile;
  }

}  // namespace

ProjectTree::ProjectTree(QWidget* parent) : QTreeWidget(parent)
{
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setContextMenuPolicy(Qt::CustomContextMenu);
}

void ProjectTree::rebuild(
    const SILICON::project::ProjectInfo&                       project,
    const SILICON::project::DocumentStore::DocumentReferences& circuits,
    const SILICON::project::DocumentStore::DocumentReferences& subcircuits,
    const SILICON::project::DocumentStore::DocumentReferences& codeFiles,
    const SILICON::project::DocumentStore::DocumentReferences& binaryFiles,
    const std::string&                                         activeDocumentPath)
{
  const QSignalBlocker blocker(this);
  clear();

  auto* projectItem = new QTreeWidgetItem(this);
  projectItem->setText(0, QString::fromStdString(project.name));
  projectItem->setData(0, ItemKindRole, static_cast<int>(ProjectTreeItemKind::Project));
  projectItem->setExpanded(true);
  addSection(projectItem, SILICON::project::DocumentType::Circuit, circuits);
  addSection(projectItem, SILICON::project::DocumentType::Subcircuit, subcircuits);
  addCodeSection(projectItem, codeFiles);
  addSection(projectItem, SILICON::project::DocumentType::Binary, binaryFiles);
  expandAll();

  if (!activeDocumentPath.empty())
    selectDocument(activeDocumentPath);
}

void ProjectTree::updateLabels(
    const SILICON::project::ProjectInfo&                       project,
    const SILICON::project::DocumentStore::DocumentReferences& circuits,
    const SILICON::project::DocumentStore::DocumentReferences& subcircuits,
    const SILICON::project::DocumentStore::DocumentReferences& codeFiles,
    const SILICON::project::DocumentStore::DocumentReferences& binaryFiles)
{
  const QSignalBlocker blocker(this);
  if (topLevelItemCount() == 0)
    return;

  topLevelItem(0)->setText(0, QString::fromStdString(project.name));
  for (const auto& [type, documents] :
       {std::pair{SILICON::project::DocumentType::Circuit, &circuits},
        std::pair{SILICON::project::DocumentType::Subcircuit, &subcircuits},
        std::pair{SILICON::project::DocumentType::Binary, &binaryFiles}}) {
    auto* section = sectionFor(type);
    if (!section)
      continue;
    for (int i = 0; i < section->childCount() && i < static_cast<int>(documents->size());
         ++i) {
      const auto& document = documents->at(static_cast<std::size_t>(i)).get();
      const auto  label    = type == SILICON::project::DocumentType::Circuit
                                 ? documentDisplayName(document)
                             : type == SILICON::project::DocumentType::Subcircuit
                                 ? QString::fromStdString(
                                   document.subcircuitSlug().value_or(document.getPath()))
                                 : QString::fromStdString(
                                   SILICON::project::binarySlugForPath(document.getPath())
                                       .value_or(document.getPath()));
      section->child(i)->setText(0, label);
    }
  }
  if (auto* section = sectionFor(SILICON::project::DocumentType::Code)) {
    for (int group = 0; group < section->childCount(); ++group) {
      for (int child = 0; child < section->child(group)->childCount(); ++child) {
        auto*      item = section->child(group)->child(child);
        const auto path = item->data(0, PathRole).toString().toStdString();
        if (std::ranges::any_of(codeFiles, [&path](const auto document) {
              return document.get().getPath() == path;
            }))
          item->setText(0, QFileInfo(QString::fromStdString(path)).fileName());
      }
    }
  }
}

void ProjectTree::selectDocument(const std::string& path)
{
  const QSignalBlocker blocker(this);
  clearSelection();
  const auto type    = SILICON::project::documentTypeForPath(path);
  auto*      section = type ? sectionFor(*type) : nullptr;
  if (!section)
    return;

  const auto              targetPath = QString::fromStdString(path);
  QList<QTreeWidgetItem*> pending{section};
  while (!pending.empty()) {
    auto* item = pending.takeFirst();
    if (item->data(0, PathRole).toString() == targetPath) {
      item->setSelected(true);
      setCurrentItem(item);
      return;
    }
    for (int i = 0; i < item->childCount(); ++i)
      pending.push_back(item->child(i));
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

QTreeWidgetItem* ProjectTree::sectionFor(const SILICON::project::DocumentType type) const
{
  if (topLevelItemCount() == 0)
    return nullptr;
  auto* projectItem = topLevelItem(0);
  for (int i = 0; i < projectItem->childCount(); ++i) {
    auto* child = projectItem->child(i);
    if (itemKind(child) == sectionKind(type))
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

void ProjectTree::addSection(
    QTreeWidgetItem* projectItem, const SILICON::project::DocumentType type,
    const SILICON::project::DocumentStore::DocumentReferences& documents)
{
  const bool circuits = type == SILICON::project::DocumentType::Circuit;
  const bool binaries = type == SILICON::project::DocumentType::Binary;
  auto*      section  = new QTreeWidgetItem(projectItem);
  section->setText(0, circuits ? tr("Circuits")
                               : (binaries ? tr("Binaries") : tr("Subcircuits")));
  section->setData(0, ItemKindRole, static_cast<int>(sectionKind(type)));
  section->setExpanded(true);

  for (const auto documentReference : documents) {
    const auto& document = documentReference.get();
    auto* item = new QTreeWidgetItem(section);
    item->setText(
        0, circuits
               ? documentDisplayName(document)
               : QString::fromStdString(
                     binaries ? SILICON::project::binarySlugForPath(document.getPath())
                                    .value_or(document.getPath())
                              : document.subcircuitSlug().value_or(document.getPath())));
    item->setIcon(0, Icon(binaries ? "file" : "circuit-board"));
    item->setData(0, ItemKindRole, static_cast<int>(documentKind(type)));
    item->setData(0, PathRole, QString::fromStdString(document.getPath()));
  }
}

void ProjectTree::addCodeSection(
    QTreeWidgetItem*                                           projectItem,
    const SILICON::project::DocumentStore::DocumentReferences& documents)
{
  auto* section = new QTreeWidgetItem(projectItem);
  section->setText(0, tr("Code"));
  section->setData(0, ItemKindRole, static_cast<int>(ProjectTreeItemKind::CodeSection));
  section->setExpanded(true);

  for (const auto& typeInfo : SILICON::project::codeFileTypeRegistry()) {
    const bool hasDocuments =
        std::ranges::any_of(documents, [&typeInfo](const auto document) {
          return SILICON::project::codeFileTypeForPath(document.get().getPath())
                 == typeInfo.type;
        });
    if (!hasDocuments)
      continue;

    auto* language = new QTreeWidgetItem(section);
    language->setText(0, QString::fromUtf8(typeInfo.displayName));
    auto font = language->font(0);
    font.setBold(true);
    language->setFont(0, font);
    language->setData(0, ItemKindRole,
                      static_cast<int>(ProjectTreeItemKind::CodeLanguage));
    language->setExpanded(true);
    for (const auto documentReference : documents) {
      const auto& document = documentReference.get();
      if (SILICON::project::codeFileTypeForPath(document.getPath()) != typeInfo.type)
        continue;
      auto* item = new QTreeWidgetItem(language);
      item->setText(0, QFileInfo(QString::fromStdString(document.getPath())).fileName());
      item->setIcon(0, Icon("code"));
      item->setData(0, ItemKindRole, static_cast<int>(ProjectTreeItemKind::CodeFile));
      item->setData(0, PathRole, QString::fromStdString(document.getPath()));
    }
  }
}

}  // namespace ui
}  // namespace SILICON
