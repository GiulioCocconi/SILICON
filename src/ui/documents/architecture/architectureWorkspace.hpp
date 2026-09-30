/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include <QTabWidget>

#include <sisl/sisl.hpp>

#include <core/projectDocument.hpp>

namespace SILICON::project {
class DocumentStore;
}

namespace SILICON::ui {

class ArchitectureLoadPlan;
class CodeEditor;
class SislVisualizer;

/** Owns the editors, tabs, and visualization for architecture documents. */
class ArchitectureWorkspace : public QTabWidget {
  Q_OBJECT

public:
  explicit ArchitectureWorkspace(QWidget* parent = nullptr);

  [[nodiscard]] CodeEditor* editor(SILICON::project::DocumentType type) const noexcept;
  [[nodiscard]] const std::map<SILICON::project::DocumentType, CodeEditor*>&
  editors() const noexcept
  {
    return editors_;
  }
  [[nodiscard]] bool hasModifiedEditors() const;
  [[nodiscard]] bool isVisualizerActive() const noexcept;
  [[nodiscard]] bool isVisualizerTab(int index) const noexcept;
  void resetLoadedArchitecture() noexcept { loadedArchitectureName_.clear(); }
  void loadDocument(const SILICON::project::Document&      document,
                    const SILICON::project::DocumentStore& store);

  /**
   * @brief Resolves the architecture tabs of @p document without touching the tabs.
   *
   * Every failure mode of loadDocument() is detected here, so a rejected document
   * leaves the currently displayed architecture untouched.
   *
   * @param document Architecture document that should become active
   * @param store Project documents providing the architecture files
   * @return A detached plan consumed by @ref applyLoadPlan
   * @throws std::exception when the architecture is incomplete or invalid
   */
  [[nodiscard]] std::shared_ptr<ArchitectureLoadPlan>
  prepareLoadPlan(const SILICON::project::Document&      document,
                  const SILICON::project::DocumentStore& store);

  /**
   * @brief Rebuilds the architecture tabs from a prepared plan.
   *
   * @param plan Plan produced by @ref prepareLoadPlan
   */
  void applyLoadPlan(const ArchitectureLoadPlan& plan);

  void showVisualization(sisl::IsaDescription description);

signals:
  void documentModified();

private:
  std::map<SILICON::project::DocumentType, CodeEditor*> editors_;
  SislVisualizer*                                       visualizer_ = nullptr;
  std::string                                           loadedArchitectureName_;
};

}  // namespace SILICON::ui
