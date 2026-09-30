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

#include "architectureController.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>

#include <sisl/sisl.hpp>

#include <core/isaArchitecture.hpp>
#include <logging/logger.hpp>
#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/architecture/sislVisualizer.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectSession.hpp>

namespace SILICON::ui {
namespace {
  const SILICON::logging::Logger isaLog("sisl");

  /**
   * @brief Reports whether the active document is an ISA architecture declaration.
   *
   * Architecture builds and the visualizer only apply to SISL documents; other code files
   * silently have nothing to do.
   */
  [[nodiscard]] bool isArchitectureDocument(const ProjectSession& session)
  {
    return SILICON::project::documentTypeForPath(session.activeDocumentPath)
           == SILICON::project::DocumentType::Sisl;
  }

  /**
   * Builds the architecture shown in @p editor, validating its declaration against
   * the architecture name of @p documentPath and reporting every failure.
   * @return The parsed architecture, or std::nullopt when the build failed
   */
  std::optional<sisl::Isa> buildEditedArchitecture(const std::string_view documentPath,
                                                   const CodeEditor&      editor)
  {
    const auto result = SILICON::project::buildArchitecture(
        SILICON::project::documentSlugForPath(documentPath),
        editor.toPlainText().toStdString());
    if (result)
      return result.isa;
    for (const auto& diagnostic : result.diagnostics) {
      const auto location =
          diagnostic.source
              ? std::format("{}:{}:{}", documentPath, diagnostic.source->line.value_or(0),
                            diagnostic.source->column.value_or(0))
              : std::string(documentPath);
      isaLog.error(std::format("{}: [{}] {}", location, sisl::to_string(diagnostic.code),
                               diagnostic.message));
    }
    if (result.error)
      isaLog.error(std::format("{}: {}", documentPath, *result.error));
    return std::nullopt;
  }
}  // namespace

ArchitectureController::ArchitectureController(ProjectSession&            session,
                                               EditorWorkspace&           workspace,
                                               ProjectDocumentController& documents,
                                               QObject*                   parent)
  : QObject(parent), session_(session), workspace_(workspace), documents_(documents)
{
  auto* architecture = workspace_.architectureWorkspace();
  architecture->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(architecture->tabBar(), &QWidget::customContextMenuRequested, &documents_,
          &ProjectDocumentController::showArchitectureTabContextMenu);
  connect(architecture, &QTabWidget::currentChanged, this,
          &ArchitectureController::handleTabChanged);
}

void ArchitectureController::handleTabChanged(const int index)
{
  auto* architecture = workspace_.architectureWorkspace();
  if (index < 0)
    return;

  if (architecture->isVisualizerTab(index)) {
    emit visualizerTabSelected();
    return;
  }

  const auto active = SILICON::project::documentTypeForPath(session_.activeDocumentPath);
  if (!active
      || SILICON::project::categoryOf(*active)
             != SILICON::project::DocumentCategory::Architecture)
    return;

  const auto name = SILICON::project::documentSlugForPath(session_.activeDocumentPath);
  if (!name)
    return;

  const auto type = static_cast<SILICON::project::DocumentType>(
      architecture->tabBar()->tabData(index).toInt());
  const auto path = SILICON::project::documentPathForSlug(type, *name);
  if (documents_.switchToDocument(path, false))
    return;

  // The document was rejected, so the workspace must not keep showing its tab.
  const QSignalBlocker blocker(architecture);
  for (int tab = 0; tab < architecture->count(); ++tab) {
    if (architecture->tabBar()->tabData(tab).toInt() == static_cast<int>(*active)) {
      architecture->setCurrentIndex(tab);
      break;
    }
  }
}

void ArchitectureController::visualizeActiveArchitecture()
{
  if (!isArchitectureDocument(session_))
    return;
  const auto* editor = workspace_.activeCodeEditor();
  if (!editor)
    return;
  const auto isa = buildEditedArchitecture(session_.activeDocumentPath, *editor);
  if (!isa)
    return;
  workspace_.architectureWorkspace()->showVisualization(isa->describe());
}

void ArchitectureController::buildActiveArchitecture()
{
  if (!isArchitectureDocument(session_))
    return;
  const auto* editor = workspace_.activeCodeEditor();
  if (!editor)
    return;
  if (buildEditedArchitecture(session_.activeDocumentPath, *editor))
    isaLog.info(std::format("{}: build succeeded", session_.activeDocumentPath));
}

}  // namespace SILICON::ui
