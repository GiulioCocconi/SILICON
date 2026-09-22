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
#include <QFont>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>

#include <ui/common/icons.hpp>

namespace SILICON::ui {
namespace {

  constexpr int ItemKindRole         = Qt::UserRole;
  constexpr int DocumentTypeRole     = Qt::UserRole + 1;
  constexpr int PathRole             = Qt::UserRole + 2;
  constexpr int DocumentCategoryRole = Qt::UserRole + 3;

  [[nodiscard]] QString circuitDisplayName(const project::Document& document)
  {
    return QString::fromStdString(
        project::documentSlugForPath(document.getPath()).value_or(document.getPath()));
  }

  [[nodiscard]] QString sectionTitle(const project::DocumentCategory category)
  {
    switch (category) {
      case project::DocumentCategory::Diagram: return ProjectTree::tr("Circuits");
      case project::DocumentCategory::Code: return ProjectTree::tr("Code");
      case project::DocumentCategory::Architecture:
        return ProjectTree::tr("ISA Architectures");
      case project::DocumentCategory::Binary: return ProjectTree::tr("Binaries");
    }
    return {};
  }

  [[nodiscard]] QString documentLabel(const project::Document& document)
  {
    const auto category = project::categoryOf(document.getType());
    if (category == project::DocumentCategory::Code
        || category == project::DocumentCategory::Architecture)
      return QFileInfo(QString::fromStdString(document.getPath())).fileName();
    switch (document.getType()) {
      case project::DocumentType::Circuit: return circuitDisplayName(document);
      case project::DocumentType::RawBinary:
        return QString::fromStdString(project::documentSlugForPath(document.getPath())
                                          .value_or(document.getPath()));
      default: break;
    }
    return {};
  }

  [[nodiscard]] const char* documentIcon(const project::DocumentType type)
  {
    if (project::categoryOf(type) == project::DocumentCategory::Code)
      return "code";
    if (type == project::DocumentType::RawBinary)
      return "file";
    return "circuit-board";
  }

  void setKind(QTreeWidgetItem* item, const ProjectTreeItemKind kind)
  {
    item->setData(0, ItemKindRole, static_cast<int>(kind));
  }

  void setDocumentType(QTreeWidgetItem* item, const project::DocumentType type)
  {
    item->setData(0, DocumentTypeRole, static_cast<int>(type));
  }

  void setDocumentCategory(QTreeWidgetItem* item,
                           const project::DocumentCategory category)
  {
    item->setData(0, DocumentCategoryRole, static_cast<int>(category));
  }

}  // namespace

ProjectTree::ProjectTree(QWidget* parent) : QTreeWidget(parent)
{
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setContextMenuPolicy(Qt::CustomContextMenu);
}

void ProjectTree::rebuild(const project::ProjectInfo&              projectInfo,
                          const std::span<const project::Document> documents,
                          const std::string_view                   activeDocumentPath)
{
  const QSignalBlocker blocker(this);
  clear();

  auto* projectItem = new QTreeWidgetItem(this);
  projectItem->setText(0, QString::fromStdString(projectInfo.name));
  setKind(projectItem, ProjectTreeItemKind::Project);
  projectItem->setExpanded(true);

  for (const auto category : {project::DocumentCategory::Diagram,
                              project::DocumentCategory::Code,
                              project::DocumentCategory::Architecture,
                              project::DocumentCategory::Binary})
    addSection(projectItem, category, documents);

  expandAll();

  if (!activeDocumentPath.empty())
    selectDocument(activeDocumentPath);
}

void ProjectTree::selectDocument(const std::string_view path)
{
  const QSignalBlocker blocker(this);
  clearSelection();

  const auto targetPath =
      QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
  for (QTreeWidgetItemIterator it(this); *it; ++it) {
    if ((*it)->data(0, PathRole).toString() != targetPath)
      continue;

    (*it)->setSelected(true);
    setCurrentItem(*it);
    return;
  }

  if (path.starts_with("isa/")) {
    const auto separator = path.find('/', 4);
    if (separator != std::string_view::npos) {
      const auto name = QString::fromUtf8(path.substr(4, separator - 4));
      for (QTreeWidgetItemIterator it(this); *it; ++it) {
        if (itemKind(*it) != ProjectTreeItemKind::Architecture
            || (*it)->text(0) != name)
          continue;
        (*it)->setSelected(true);
        setCurrentItem(*it);
        return;
      }
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

std::optional<ProjectTreeDocumentSelection> ProjectTree::selectedDocument() const
{
  const auto* item = selectedProjectItem();
  if (!item || (itemKind(item) != ProjectTreeItemKind::Document
                && itemKind(item) != ProjectTreeItemKind::Architecture))
    return std::nullopt;

  const auto type = itemDocumentType(item);
  if (!type)
    return std::nullopt;

  return ProjectTreeDocumentSelection{
      .type = *type,
      .path = documentPath(item),
  };
}

ProjectTreeItemKind ProjectTree::itemKind(const QTreeWidgetItem* item)
{
  return static_cast<ProjectTreeItemKind>(item->data(0, ItemKindRole).toInt());
}

std::optional<project::DocumentType>
ProjectTree::itemDocumentType(const QTreeWidgetItem* item)
{
  const auto value = item->data(0, DocumentTypeRole);
  if (!value.isValid())
    return std::nullopt;
  return static_cast<project::DocumentType>(value.toInt());
}

std::optional<project::DocumentCategory>
ProjectTree::itemDocumentCategory(const QTreeWidgetItem* item)
{
  const auto value = item->data(0, DocumentCategoryRole);
  if (!value.isValid())
    return std::nullopt;
  return static_cast<project::DocumentCategory>(value.toInt());
}

std::string ProjectTree::documentPath(const QTreeWidgetItem* item)
{
  return item->data(0, PathRole).toString().toStdString();
}

std::string ProjectTree::architectureName(const QTreeWidgetItem* item)
{
  if (!item || itemKind(item) != ProjectTreeItemKind::Architecture)
    return {};
  return item->text(0).toStdString();
}

void ProjectTree::addSection(QTreeWidgetItem*                         projectItem,
                             const project::DocumentCategory          category,
                             const std::span<const project::Document> documents)
{
  bool hasDocuments = false;
  for (const auto& document : documents) {
    if (project::categoryOf(document.getType()) == category) {
      hasDocuments = true;
      break;
    }
  }

  if (!hasDocuments)
    return;

  auto* section = new QTreeWidgetItem(projectItem);
  section->setText(0, sectionTitle(category));
  setKind(section, ProjectTreeItemKind::Section);
  setDocumentCategory(section, category);
  section->setExpanded(true);

  for (const auto& document : documents) {
    if (project::categoryOf(document.getType()) != category)
      continue;
    if (category == project::DocumentCategory::Architecture) {
      const auto name = project::documentSlugForPath(document.getPath());
      if (!name)
        continue;
      bool found = false;
      for (int i = 0; i < section->childCount(); ++i)
        found |= section->child(i)->text(0) == QString::fromStdString(*name);
      if (found)
        continue;
      auto* item = new QTreeWidgetItem(section);
      item->setText(0, QString::fromStdString(*name));
      item->setIcon(0, Icon("cpu"));
      setKind(item, ProjectTreeItemKind::Architecture);
      setDocumentType(item, document.getType());
      item->setData(0, PathRole, QString::fromStdString(document.getPath()));
    } else {
      addDocument(section, document);
    }
  }
}

void ProjectTree::addDocument(QTreeWidgetItem* parent, const project::Document& document)
{
  auto* item = new QTreeWidgetItem(parent);
  item->setText(0, documentLabel(document));
  item->setIcon(0, Icon(documentIcon(document.getType())));
  setKind(item, ProjectTreeItemKind::Document);
  setDocumentType(item, document.getType());
  item->setData(0, PathRole, QString::fromStdString(document.getPath()));
}

}  // namespace SILICON::ui
