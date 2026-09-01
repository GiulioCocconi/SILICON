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

#include <nlohmann/json.hpp>

#include <ui/common/icons.hpp>

namespace SILICON::ui {
namespace {

  constexpr int ItemKindRole     = Qt::UserRole;
  constexpr int DocumentTypeRole = Qt::UserRole + 1;
  constexpr int PathRole         = Qt::UserRole + 2;

  [[nodiscard]] QString circuitDisplayName(const project::Document& document)
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

    return QFileInfo(QString::fromStdString(document.getPath())).baseName();
  }

  [[nodiscard]] QString sectionTitle(const project::DocumentType type)
  {
    switch (type) {
      case project::DocumentType::Circuit: return ProjectTree::tr("Circuits");
      case project::DocumentType::Verilog: return ProjectTree::tr("Verilog");
      case project::DocumentType::RawBinary: return ProjectTree::tr("Binaries");
    }
    return {};
  }

  [[nodiscard]] QString documentLabel(const project::Document& document)
  {
    switch (document.getType()) {
      case project::DocumentType::Circuit: return circuitDisplayName(document);
      case project::DocumentType::Verilog:
        return QFileInfo(QString::fromStdString(document.getPath())).fileName();
      case project::DocumentType::RawBinary:
        return QString::fromStdString(project::documentSlugForPath(document.getPath())
                                          .value_or(document.getPath()));
    }
    return {};
  }

  void setKind(QTreeWidgetItem* item, const ProjectTreeItemKind kind)
  {
    item->setData(0, ItemKindRole, static_cast<int>(kind));
  }

  void setDocumentType(QTreeWidgetItem* item, const project::DocumentType type)
  {
    item->setData(0, DocumentTypeRole, static_cast<int>(type));
  }

}  // namespace

ProjectTree::ProjectTree(QWidget* parent) : QTreeWidget(parent)
{
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setContextMenuPolicy(Qt::CustomContextMenu);
}

void ProjectTree::rebuild(const project::ProjectInfo& projectInfo,
                          const std::span<const project::Document> documents,
                          const std::string_view activeDocumentPath)
{
  const QSignalBlocker blocker(this);
  clear();

  auto* projectItem = new QTreeWidgetItem(this);
  projectItem->setText(0, QString::fromStdString(projectInfo.name));
  setKind(projectItem, ProjectTreeItemKind::Project);
  projectItem->setExpanded(true);

  for (const auto& info : project::DOCUMENT_TYPE_INFO)
    addSection(projectItem, info.type, documents);

  expandAll();

  if (!activeDocumentPath.empty())
    selectDocument(activeDocumentPath);
}

void ProjectTree::selectDocument(const std::string_view path)
{
  const QSignalBlocker blocker(this);
  clearSelection();

  const auto targetPath = QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
  for (QTreeWidgetItemIterator it(this); *it; ++it) {
    if ((*it)->data(0, PathRole).toString() != targetPath)
      continue;

    (*it)->setSelected(true);
    setCurrentItem(*it);
    return;
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
  if (!item || itemKind(item) != ProjectTreeItemKind::Document)
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

std::string ProjectTree::documentPath(const QTreeWidgetItem* item)
{
  return item->data(0, PathRole).toString().toStdString();
}

void ProjectTree::addSection(QTreeWidgetItem* projectItem,
                             const project::DocumentType type,
                             const std::span<const project::Document> documents)
{
  auto* section = new QTreeWidgetItem(projectItem);
  section->setText(0, sectionTitle(type));
  setKind(section, ProjectTreeItemKind::Section);
  setDocumentType(section, type);
  section->setExpanded(true);

  for (const auto& document : documents) {
    if (document.getType() == type)
      addDocument(section, document);
  }
}

void ProjectTree::addDocument(QTreeWidgetItem* parent, const project::Document& document)
{
  auto* item = new QTreeWidgetItem(parent);
  item->setText(0, documentLabel(document));
  const auto iconName =
      project::documentCategoryIconName(project::categoryOf(document.getType()));
  item->setIcon(
      0, Icon(QString::fromUtf8(iconName.data(),
                               static_cast<qsizetype>(iconName.size()))));
  setKind(item, ProjectTreeItemKind::Document);
  setDocumentType(item, document.getType());
  item->setData(0, PathRole, QString::fromStdString(document.getPath()));
}

}  // namespace SILICON::ui
