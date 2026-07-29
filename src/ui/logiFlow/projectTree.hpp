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
  Subcircuit
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

  void rebuild(const SILICON::project::ProjectInfo&           project,
               const std::vector<SILICON::project::Document>& circuits,
               const std::vector<SILICON::project::Document>& subcircuits,
               const std::string&                             activeDocumentPath);
  void updateLabels(const SILICON::project::ProjectInfo&           project,
                    const std::vector<SILICON::project::Document>& circuits,
                    const std::vector<SILICON::project::Document>& subcircuits);

  void selectDocument(const std::string& path);
  void clearDocumentSelection();

  [[nodiscard]] QTreeWidgetItem* selectedProjectItem() const;
  [[nodiscard]] QTreeWidgetItem* sectionFor(SILICON::project::DocumentKind kind) const;
  [[nodiscard]] static ProjectTreeItemKind itemKind(const QTreeWidgetItem* item);
  [[nodiscard]] static std::string         documentPath(const QTreeWidgetItem* item);

private:
  void addSection(QTreeWidgetItem* projectItem, SILICON::project::DocumentKind kind,
                  const std::vector<SILICON::project::Document>& documents);
};

}  // namespace ui
}  // namespace SILICON
