/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#include "architectureWorkspace.hpp"

#include <algorithm>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QSignalBlocker>
#include <QTabBar>
#include <QTextDocument>

#include <core/projectDocument.hpp>
#include <ui/documents/architecture/sislVisualizer.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/documents/code/codeFilePresentation.hpp>

namespace SILICON::ui {

/** @brief Architecture files resolved before the editor tabs are rebuilt. */
class ArchitectureLoadPlan {
public:
  /** @brief One architecture file with its tab label and stored contents. */
  struct File {
    SILICON::project::DocumentType type;
    const char*                    tabName;
    QString                        contents;
  };

  /** @brief Architecture identifier shared by every file of the plan. */
  std::string name;
  /** @brief Architecture files to show as tabs, in presentation order. */
  std::vector<File> files;
  /** @brief File that should be focused once the tabs exist. */
  SILICON::project::DocumentType activeType = SILICON::project::DocumentType::Sisl;
};

ArchitectureWorkspace::ArchitectureWorkspace(QWidget* parent) : QTabWidget(parent)
{
  visualizer_ = new SislVisualizer(this);
  for (const auto& component : codeFilePresentations()) {
    if (!component.architectureTabName)
      continue;
    auto* codeEditor = new CodeEditor(this);
    codeEditor->setFileType(component.type);
    editors_.emplace(component.type, codeEditor);
    connect(codeEditor->document(), &QTextDocument::modificationChanged, this,
            [this](bool modified) {
              if (modified)
                emit documentModified();
            });
    connect(codeEditor, &QPlainTextEdit::textChanged, this, [this] {
      const int index = indexOf(visualizer_);
      if (index >= 0) {
        visualizer_->hide();
        removeTab(index);
      }
    });
  }
}

CodeEditor*
ArchitectureWorkspace::editor(const SILICON::project::DocumentType type) const noexcept
{
  const auto it = editors_.find(type);
  return it == editors_.end() ? nullptr : it->second;
}

bool ArchitectureWorkspace::hasModifiedEditors() const
{
  return std::ranges::any_of(
      editors_, [](const auto& entry) { return entry.second->document()->isModified(); });
}

bool ArchitectureWorkspace::isVisualizerActive() const noexcept
{
  return currentWidget() == visualizer_;
}

bool ArchitectureWorkspace::isVisualizerTab(int index) const noexcept
{
  return widget(index) == visualizer_;
}

void ArchitectureWorkspace::loadDocument(const SILICON::project::Document&      document,
                                         const SILICON::project::DocumentStore& store)
{
  applyLoadPlan(*prepareLoadPlan(document, store));
}

std::shared_ptr<ArchitectureLoadPlan>
ArchitectureWorkspace::prepareLoadPlan(const SILICON::project::Document&      document,
                                       const SILICON::project::DocumentStore& store)
{
  const auto name = SILICON::project::documentSlugForPath(document.getPath());
  if (!name)
    throw std::invalid_argument("Architecture document has no architecture name");

  // The plan resolves every file of the architecture up front, so the tabs are only
  // rebuilt once the whole architecture is known to be loadable.
  auto plan        = std::make_shared<ArchitectureLoadPlan>();
  plan->name       = *name;
  plan->activeType = document.getType();

  for (const auto& component : codeFilePresentations()) {
    if (!component.architectureTabName)
      continue;
    const auto  path = SILICON::project::documentPathForSlug(component.type, *name);
    const auto* file = store.find(path);
    if (!file)
      continue;
    if (!editor(component.type))
      throw std::logic_error("Registered architecture component has no editor");
    plan->files.push_back({component.type, component.architectureTabName,
                           QString::fromStdString(file->getContents())});
  }

  if (std::ranges::none_of(plan->files, [&plan](const auto& file) {
        return file.type == plan->activeType;
      }))
    throw std::invalid_argument("Architecture component is not registered");

  return plan;
}

void ArchitectureWorkspace::applyLoadPlan(const ArchitectureLoadPlan& plan)
{
  const QSignalBlocker blocker(this);
  visualizer_->hide();
  clear();
  const bool newArchitecture = loadedArchitectureName_ != plan.name;
  int        activeTab       = -1;
  for (const auto& file : plan.files) {
    auto* codeEditor = editor(file.type);
    if (!codeEditor)
      throw std::logic_error("Registered architecture component has no editor");
    if (newArchitecture || codeEditor->toPlainText() != file.contents)
      codeEditor->setPlainText(file.contents);
    codeEditor->document()->setModified(false);
    const int tab = addTab(codeEditor, tr(file.tabName));
    tabBar()->setTabData(tab, static_cast<int>(file.type));
    if (file.type == plan.activeType)
      activeTab = tab;
  }
  loadedArchitectureName_ = plan.name;
  setCurrentIndex(activeTab);
  if (auto* active = editor(plan.activeType))
    active->setFocus();
}

void ArchitectureWorkspace::showVisualization(sisl::IsaDescription description)
{
  visualizer_->setDescription(std::move(description));
  if (indexOf(visualizer_) < 0)
    addTab(visualizer_, tr("Visualizer"));
  setCurrentWidget(visualizer_);
}

}  // namespace SILICON::ui
