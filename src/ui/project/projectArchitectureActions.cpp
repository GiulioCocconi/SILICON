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

#include <ui/documents/architecture/architectureWorkspace.hpp>
#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectSession.hpp>

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
#include <ui/documents/code/codeFilePresentation.hpp>
#include <ui/shell/icons.hpp>
#include <ui/shell/inputDialogUtils.hpp>
#include <ui/documents/code/codeEditor.hpp>
#include <ui/documents/architecture/sislVisualizer.hpp>

namespace SILICON::ui {
void ProjectDocumentController::createArchitecture()
{
  SILICON::ui::inputDialog::getText(
      dialogParent, tr("New ISA Architecture"), tr("Architecture name"),
      QStringLiteral("NewArch"), [this](const QString& requestedName) {
        const auto name = requestedName.trimmed().toStdString();
        if (!SILICON::project::isValidArchitectureName(name)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, tr("New ISA Architecture"),
              tr("Enter a valid SISL architecture identifier."));
          return;
        }
        const auto path = SILICON::project::documentPathForSlug(
            SILICON::project::DocumentType::Sisl, name);
        if (session.projectContext.documents().contains(path)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, tr("New ISA Architecture"),
              tr("An architecture named '%1' already exists.")
                  .arg(QString::fromStdString(name)));
          return;
        }
        this->pushCreateDocumentCommand({path, std::format("arch {} = {{}};\n", name)},
                                        tr("Create ISA Architecture"));
      });
}

void ProjectDocumentController::showArchitectureTabContextMenu(const QPoint& position)
{
  const auto active = SILICON::project::documentTypeForPath(session.activeDocumentPath);
  if (!active
      || SILICON::project::categoryOf(*active)
             != SILICON::project::DocumentCategory::Architecture)
    return;
  const auto index = workspace.architectureWorkspace()->tabBar()->tabAt(position);
  if (index < 0)
    return;
  if (workspace.architectureWorkspace()->isVisualizerTab(index))
    return;
  const auto name = SILICON::project::documentSlugForPath(session.activeDocumentPath);
  if (!name)
    return;
  const auto type = static_cast<SILICON::project::DocumentType>(
      workspace.architectureWorkspace()->tabBar()->tabData(index).toInt());
  const auto path = SILICON::project::documentPathForSlug(type, *name);
  QMenu      menu(dialogParent);
  menu.addAction(Icon("delete"), tr("Delete File"), this,
                 [this, path] { deleteArchitectureComponent(path); });
  menu.exec(workspace.architectureWorkspace()->tabBar()->mapToGlobal(position));
}

void ProjectDocumentController::deleteArchitectureComponent(const std::string& path)
{
  if (!session.projectContext.documents().contains(path))
    return;
  SILICON::ui::inputDialog::question(
      dialogParent, tr("Delete Architecture File"),
      tr("Delete %1? If it is the last file, the architecture will also be removed.")
          .arg(QString::fromStdString(SILICON::project::documentFileName(
              *session.projectContext.documents().find(path)))),
      [this, path] {
        this->removeProjectDocuments({path}, tr("Delete Architecture File"));
      });
}

void ProjectDocumentController::renameArchitecture(const std::string& name)
{
  const auto title = tr("Rename ISA Architecture");
  SILICON::ui::inputDialog::getText(
      dialogParent, title, tr("Name"), QString::fromStdString(name),
      [this, name, title](const QString& requestedName) {
        const auto newName = requestedName.trimmed().toStdString();
        if (!SILICON::project::isValidArchitectureName(newName)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, title, tr("Enter a valid SISL architecture identifier."));
          return;
        }
        if (newName == name)
          return;
        const auto oldPrefix = SILICON::project::architectureDirectory(name);
        const auto newPrefix = SILICON::project::architectureDirectory(newName);
        for (const auto& document : session.projectContext.documents().getDocuments()) {
          if (document.getPath().starts_with(newPrefix)) {
            SILICON::ui::inputDialog::warning(
                dialogParent, title,
                tr("An architecture named '%1' already exists.")
                    .arg(QString::fromStdString(newName)));
            return;
          }
        }

        try {
          workspace.flushActiveDocument();
          const auto before = session.projectContext.documents().getDocuments();
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
          const auto beforeActive = session.activeDocumentPath;
          const auto afterActive  = beforeActive.starts_with(oldPrefix)
                                        ? newPrefix + beforeActive.substr(oldPrefix.size())
                                        : beforeActive;
          this->pushDocumentSnapshotCommand(title, before, beforeActive, std::move(after),
                                            afterActive);
        } catch (const std::exception& error) {
          SILICON::ui::inputDialog::warning(dialogParent, title, error.what());
        }
      });
}

void ProjectDocumentController::deleteArchitecture(const std::string& name)
{
  const auto               prefix = SILICON::project::architectureDirectory(name);
  std::vector<std::string> paths;
  for (const auto& document : session.projectContext.documents().getDocuments()) {
    if (document.getPath().starts_with(prefix))
      paths.push_back(document.getPath());
  }
  SILICON::ui::inputDialog::question(
      dialogParent, tr("Delete ISA Architecture"),
      tr("Delete ISA architecture \"%1\" and all its files?")
          .arg(QString::fromStdString(name)),
      [this, paths] {
        this->removeProjectDocuments(paths, tr("Delete ISA Architecture"));
      });
}

}  // namespace SILICON::ui
