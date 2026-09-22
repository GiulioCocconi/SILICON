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

#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <QMenu>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>

#include <sisl/sisl.hpp>

#include <core/isaArchitecture.hpp>
#include <logging/logger.hpp>
#include <ui/common/codeFilePresentation.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/logiFlow/code/codeEditor.hpp>
#include <ui/logiFlow/sislVisualizer.hpp>

namespace SILICON::ui {
namespace {
  const SILICON::logging::Logger isaLog("sisl");
}  // namespace

void LogiFlowWindow::initializeArchitectureEditor()
{
  architectureTabs = new QTabWidget(this);
  sislVisualizer = new SislVisualizer(architectureTabs);
  for (const auto& component : codeFilePresentations()) {
    if (!component.architectureTabName)
      continue;
    auto* editor = new CodeEditor(architectureTabs);
    editor->setFileType(component.type);
    architectureEditors.emplace(component.type, editor);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this](const bool modified) {
              if (modified)
                codeDocumentsDirty = true;
            });
    connect(editor, &QPlainTextEdit::textChanged, this, [this] {
      const int index = architectureTabs->indexOf(sislVisualizer);
      if (index >= 0) {
        sislVisualizer->hide();
        architectureTabs->removeTab(index);
      }
    });
  }
  architectureTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(architectureTabs->tabBar(), &QWidget::customContextMenuRequested, this,
          &LogiFlowWindow::showArchitectureTabContextMenu);
  connect(architectureTabs, &QTabWidget::currentChanged, this, [this](const int index) {
    if (index < 0)
      return;
    if (architectureTabs->widget(index) == sislVisualizer) {
      updateEditActions();
      updateHistoryActions();
      return;
    }
    const auto active = activeDocumentType();
    if (!active
        || SILICON::project::categoryOf(*active)
               != SILICON::project::DocumentCategory::Architecture)
      return;
    const auto name = SILICON::project::documentSlugForPath(activeDocumentPath);
    if (!name)
      return;
    const auto type = static_cast<SILICON::project::DocumentType>(
        architectureTabs->tabBar()->tabData(index).toInt());
    const auto path = SILICON::project::documentPathForSlug(type, *name);
    if (switchToDocument(path, false)) {
      updateEditActions();
      updateHistoryActions();
      return;
    }
    const QSignalBlocker blocker(architectureTabs);
    for (int tab = 0; tab < architectureTabs->count(); ++tab) {
      if (architectureTabs->tabBar()->tabData(tab).toInt() == static_cast<int>(*active)) {
        architectureTabs->setCurrentIndex(tab);
        break;
      }
    }
  });
}

bool LogiFlowWindow::isVisualizerActive() const noexcept
{
  return architectureTabs && sislVisualizer
         && architectureTabs->currentWidget() == sislVisualizer
         && editorStack && editorStack->currentWidget() == architectureTabs;
}

void LogiFlowWindow::visualizeActiveArchitecture()
{
  if (activeDocumentType() != SILICON::project::DocumentType::Sisl)
    return;
  const auto source = architectureEditor(SILICON::project::DocumentType::Sisl)
                          ->toPlainText().toStdString();
  const auto expected = SILICON::project::documentSlugForPath(activeDocumentPath);
  try {
    if (expected)
      SILICON::project::validateArchitectureDeclaration(*expected, source);
    auto isa = sisl::Isa::load_string(source);
    sislVisualizer->setDescription(isa.describe());
    if (architectureTabs->indexOf(sislVisualizer) < 0)
      architectureTabs->addTab(sislVisualizer, tr("Visualizer"));
    architectureTabs->setCurrentWidget(sislVisualizer);
  } catch (const sisl::Error& error) {
    for (const auto& diagnostic : error.diagnostics()) {
      const auto location =
          diagnostic.source
              ? std::format("{}:{}:{}", activeDocumentPath,
                            diagnostic.source->line.value_or(0),
                            diagnostic.source->column.value_or(0))
              : activeDocumentPath;
      isaLog.error(std::format("{}: [{}] {}", location,
                               sisl::to_string(diagnostic.code), diagnostic.message));
    }
  } catch (const std::exception& error) {
    isaLog.error(std::format("{}: {}", activeDocumentPath, error.what()));
  }
}

CodeEditor* LogiFlowWindow::architectureEditor(
    const SILICON::project::DocumentType type) const noexcept
{
  const auto it = architectureEditors.find(type);
  return it == architectureEditors.end() ? nullptr : it->second;
}

void LogiFlowWindow::loadArchitectureDocument(const SILICON::project::Document& document)
{
  const auto name = SILICON::project::documentSlugForPath(document.getPath());
  if (!name)
    throw std::invalid_argument("Architecture document has no architecture name");

  const QSignalBlocker blocker(architectureTabs);
  sislVisualizer->hide();
  architectureTabs->clear();
  const bool newArchitecture = loadedArchitectureName != *name;
  int        activeTab       = -1;
  for (const auto& component : codeFilePresentations()) {
    if (!component.architectureTabName)
      continue;
    const auto  path = SILICON::project::documentPathForSlug(component.type, *name);
    const auto* file = projectContext.documents().find(path);
    if (!file)
      continue;
    auto* editor = architectureEditor(component.type);
    if (!editor)
      throw std::logic_error("Registered architecture component has no editor");
    const auto contents = QString::fromStdString(file->getContents());
    if (newArchitecture || editor->toPlainText() != contents)
      editor->setPlainText(contents);
    editor->document()->setModified(false);
    const int tab = architectureTabs->addTab(editor, tr(component.architectureTabName));
    architectureTabs->tabBar()->setTabData(tab, static_cast<int>(component.type));
    if (path == document.getPath())
      activeTab = tab;
  }
  if (activeTab < 0)
    throw std::invalid_argument("Architecture component is not registered");
  loadedArchitectureName = *name;
  architectureTabs->setCurrentIndex(activeTab);
  editorStack->setCurrentWidget(architectureTabs);
  architectureEditor(document.getType())->setFocus();
}

void LogiFlowWindow::buildActiveArchitecture()
{
  if (activeDocumentType() != SILICON::project::DocumentType::Sisl)
    return;
  const auto path   = activeDocumentPath;
  const auto source = architectureEditor(SILICON::project::DocumentType::Sisl)
                          ->toPlainText()
                          .toStdString();
  const auto expected = SILICON::project::documentSlugForPath(path);
  bool       success  = true;
  try {
    if (expected)
      SILICON::project::validateArchitectureDeclaration(*expected, source);
  } catch (const std::invalid_argument& error) {
    isaLog.error(std::format("{}: {}", path, error.what()));
    success = false;
  }
  try {
    [[maybe_unused]] const auto isa = sisl::Isa::load_string(source);
  } catch (const sisl::Error& error) {
    success = false;
    for (const auto& diagnostic : error.diagnostics()) {
      const auto location =
          diagnostic.source
              ? std::format("{}:{}:{}", path, diagnostic.source->line.value_or(0),
                            diagnostic.source->column.value_or(0))
              : path;
      isaLog.error(std::format("{}: [{}] {}", location, sisl::to_string(diagnostic.code),
                               diagnostic.message));
    }
  } catch (const std::exception& error) {
    isaLog.error(std::format("{}: {}", path, error.what()));
    success = false;
  }
  if (success)
    isaLog.info(std::format("{}: build succeeded", path));
}

void LogiFlowWindow::createArchitecture()
{
  SILICON::ui::inputDialog::getText(
      this, tr("New ISA Architecture"), tr("Architecture name"),
      QStringLiteral("NewArch"), [this](const QString& requestedName) {
        const auto name = requestedName.trimmed().toStdString();
        if (!SILICON::project::isValidArchitectureName(name)) {
          SILICON::ui::inputDialog::warning(
              this, tr("New ISA Architecture"),
              tr("Enter a valid SISL architecture identifier."));
          return;
        }
        const auto path = SILICON::project::documentPathForSlug(
            SILICON::project::DocumentType::Sisl, name);
        if (projectContext.documents().contains(path)) {
          SILICON::ui::inputDialog::warning(
              this, tr("New ISA Architecture"),
              tr("An architecture named '%1' already exists.")
                  .arg(QString::fromStdString(name)));
          return;
        }
        pushCreateDocumentCommand({path, std::format("arch {} = {{}};\n", name)},
                                  tr("Create ISA Architecture"));
      });
}

void LogiFlowWindow::showArchitectureTabContextMenu(const QPoint& position)
{
  const auto active = activeDocumentType();
  if (!active
      || SILICON::project::categoryOf(*active)
             != SILICON::project::DocumentCategory::Architecture)
    return;
  const auto index = architectureTabs->tabBar()->tabAt(position);
  if (index < 0)
    return;
  if (architectureTabs->widget(index) == sislVisualizer)
    return;
  const auto name = SILICON::project::documentSlugForPath(activeDocumentPath);
  if (!name)
    return;
  const auto type = static_cast<SILICON::project::DocumentType>(
      architectureTabs->tabBar()->tabData(index).toInt());
  const auto path = SILICON::project::documentPathForSlug(type, *name);
  QMenu      menu(this);
  menu.addAction(Icon("delete"), tr("Delete File"), this,
                 [this, path] { deleteArchitectureComponent(path); });
  menu.exec(architectureTabs->tabBar()->mapToGlobal(position));
}

void LogiFlowWindow::deleteArchitectureComponent(const std::string& path)
{
  if (!projectContext.documents().contains(path))
    return;
  SILICON::ui::inputDialog::question(
      this, tr("Delete Architecture File"),
      tr("Delete %1? If it is the last file, the architecture will also be removed.")
          .arg(QString::fromStdString(SILICON::project::documentFileName(
              *projectContext.documents().find(path)))),
      [this, path] { removeProjectDocuments({path}, tr("Delete Architecture File")); });
}

void LogiFlowWindow::renameArchitecture(const std::string& name)
{
  const auto title = tr("Rename ISA Architecture");
  SILICON::ui::inputDialog::getText(
      this, title, tr("Name"), QString::fromStdString(name),
      [this, name, title](const QString& requestedName) {
        const auto newName = requestedName.trimmed().toStdString();
        if (!SILICON::project::isValidArchitectureName(newName)) {
          SILICON::ui::inputDialog::warning(
              this, title, tr("Enter a valid SISL architecture identifier."));
          return;
        }
        if (newName == name)
          return;
        const auto oldPrefix = SILICON::project::architectureDirectory(name);
        const auto newPrefix = SILICON::project::architectureDirectory(newName);
        for (const auto& document : projectContext.documents().getDocuments()) {
          if (document.getPath().starts_with(newPrefix)) {
            SILICON::ui::inputDialog::warning(
                this, title,
                tr("An architecture named '%1' already exists.")
                    .arg(QString::fromStdString(newName)));
            return;
          }
        }

        try {
          saveActiveDocumentPayload();
          const auto before = projectContext.documents().getDocuments();
          auto       after  = before;
          for (auto& document : after) {
            if (!document.getPath().starts_with(oldPrefix))
              continue;
            auto contents = document.getContents();
            if (document.getType() == SILICON::project::DocumentType::Sisl)
              contents = SILICON::project::renameArchitectureDeclaration(contents, name,
                                                                         newName);
            document = SILICON::project::Document(
                newPrefix + document.getPath().substr(oldPrefix.size()),
                std::move(contents));
          }
          SILICON::project::ProjectContext validated;
          validated.setDocuments(after);
          const auto beforeActive = activeDocumentPath;
          const auto afterActive  = beforeActive.starts_with(oldPrefix)
                                        ? newPrefix + beforeActive.substr(oldPrefix.size())
                                        : beforeActive;
          pushDocumentSnapshotCommand(title, before, beforeActive, std::move(after),
                                      afterActive);
        } catch (const std::exception& error) {
          SILICON::ui::inputDialog::warning(this, title, error.what());
        }
      });
}

void LogiFlowWindow::deleteArchitecture(const std::string& name)
{
  const auto               prefix = SILICON::project::architectureDirectory(name);
  std::vector<std::string> paths;
  for (const auto& document : projectContext.documents().getDocuments()) {
    if (document.getPath().starts_with(prefix))
      paths.push_back(document.getPath());
  }
  SILICON::ui::inputDialog::question(
      this, tr("Delete ISA Architecture"),
      tr("Delete ISA architecture \"%1\" and all its files?")
          .arg(QString::fromStdString(name)),
      [this, paths] { removeProjectDocuments(paths, tr("Delete ISA Architecture")); });
}

}  // namespace SILICON::ui
