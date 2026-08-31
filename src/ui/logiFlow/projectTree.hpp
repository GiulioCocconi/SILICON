/*
 Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <QTreeWidget>

#include <core/serialization/projectFile.hpp>

namespace SILICON::ui {

/** @brief Semantic role of an item shown in the project tree. */
enum class ProjectTreeItemKind { Project, Section, CodeLanguage, Document };

struct ProjectTreeDocumentSelection {
  project::DocumentType type;
  std::string           path;
};

/** @brief Project document navigator used by the LogiFlow editor. */
class ProjectTree : public QTreeWidget {
  Q_OBJECT

public:
  explicit ProjectTree(QWidget* parent = nullptr);

  void rebuild(const project::ProjectInfo& project, std::span<const project::Document> documents,
               std::string_view activeDocumentPath = {});

  void selectDocument(std::string_view path);
  void clearDocumentSelection();

  [[nodiscard]] QTreeWidgetItem* selectedProjectItem() const;
  [[nodiscard]] std::optional<ProjectTreeDocumentSelection> selectedDocument() const;

  [[nodiscard]] static ProjectTreeItemKind itemKind(const QTreeWidgetItem* item);
  [[nodiscard]] static std::optional<project::DocumentType>
  itemDocumentType(const QTreeWidgetItem* item);
  [[nodiscard]] static std::string documentPath(const QTreeWidgetItem* item);

private:
  void addSection(QTreeWidgetItem* projectItem, project::DocumentType type,
                  std::span<const project::Document> documents);
  void addCodeDocuments(QTreeWidgetItem* section,
                        std::span<const project::Document> documents);
  void addDocument(QTreeWidgetItem* parent, const project::Document& document);
};

}  // namespace SILICON::ui
