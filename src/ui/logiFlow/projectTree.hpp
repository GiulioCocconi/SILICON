/*
Copyright (c) 2026. Giulio Cocconi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

#include <QTreeWidget>

#include <core/serialization/projectFile.hpp>

namespace SILICON {
namespace ui {

/** @brief Semantic type of an item shown in the LogiFlow project tree. */
enum class ProjectTreeItemKind {
  Project,
  CircuitSection,
  Circuit,
  SubcircuitSection,
  Subcircuit,
  CodeSection,
  CodeLanguage,
  CodeFile,
  BinarySection,
  BinaryFile
};

/**
 * @brief Project document navigator used by the LogiFlow editor.
 *
 * Owns tree-item roles, labels, section construction, and selection lookup. Project
 * mutations remain in LogiFlowWindow, which supplies the document snapshots to show.
 */
class ProjectTree : public QTreeWidget {
  Q_OBJECT

public:
  explicit ProjectTree(QWidget* parent = nullptr);

  void rebuild(const SILICON::project::ProjectInfo&                       project,
               const SILICON::project::DocumentStore::DocumentReferences& circuits,
               const SILICON::project::DocumentStore::DocumentReferences& subcircuits,
               const SILICON::project::DocumentStore::DocumentReferences& codeFiles,
               const SILICON::project::DocumentStore::DocumentReferences& binaryFiles,
               const std::string& activeDocumentPath);
  void
  updateLabels(const SILICON::project::ProjectInfo&                       project,
               const SILICON::project::DocumentStore::DocumentReferences& circuits,
               const SILICON::project::DocumentStore::DocumentReferences& subcircuits,
               const SILICON::project::DocumentStore::DocumentReferences& codeFiles,
               const SILICON::project::DocumentStore::DocumentReferences& binaryFiles);

  void selectDocument(const std::string& path);
  void clearDocumentSelection();

  [[nodiscard]] QTreeWidgetItem* selectedProjectItem() const;
  [[nodiscard]] QTreeWidgetItem* sectionFor(SILICON::project::DocumentType type) const;
  [[nodiscard]] static ProjectTreeItemKind itemKind(const QTreeWidgetItem* item);
  [[nodiscard]] static std::string         documentPath(const QTreeWidgetItem* item);

private:
  void addSection(QTreeWidgetItem* projectItem, SILICON::project::DocumentType type,
                  const SILICON::project::DocumentStore::DocumentReferences& documents);
  void
  addCodeSection(QTreeWidgetItem*                                           projectItem,
                 const SILICON::project::DocumentStore::DocumentReferences& documents);
};

}  // namespace ui
}  // namespace SILICON
