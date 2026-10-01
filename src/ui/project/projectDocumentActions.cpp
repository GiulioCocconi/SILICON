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

#include <ui/documents/editorWorkspace.hpp>
#include <ui/project/projectDocumentController.hpp>
#include <ui/project/projectDocumentPolicy.hpp>
#include <ui/project/projectSession.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QStringList>
#include <QUndoStack>

#include <core/circuitDependencyGraph.hpp>
#include <core/isaArchitecture.hpp>
#include <ui/documents/code/codeFilePresentation.hpp>
#include <ui/shell/fileDialogUtils.hpp>
#include <ui/shell/icons.hpp>
#include <ui/shell/inputDialogUtils.hpp>
#include <ui/circuit/diagram/undoCommands.hpp>
#include <ui/project/projectTree.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

void ProjectDocumentController::pushCreateDocumentCommand(
    SILICON::project::Document document, const QString& commandText)
{
  const auto path = document.getPath();

  auto addDocument = [this, document] {
    try {
      workspace.flushActiveDocument();
    } catch (const std::exception&) {
    }
    this->insertDocument(document, std::nullopt, true);
  };

  auto removeDocumentCommand = [this, path] { this->removeDocument(path); };
  undoStack->push(
      new CallbackUndoCommand(commandText, removeDocumentCommand, addDocument));
}

void ProjectDocumentController::importProjectDocument()
{
  SILICON::ui::fileDialog::openFileContent(
      dialogParent, tr("Import Document"),
      tr("Supported Documents (*.json *.v *.sisl *.bin);;All Files (*)"),
      [this](const QString& fileName, const QByteArray& fileContent) {
        try {
          auto document = SILICON::project::importDocument(
              fileName.toStdString(),
              std::string(fileContent.constData(),
                          static_cast<std::size_t>(fileContent.size())));

          workspace.flushActiveDocument();
          const auto sourcePath = session.activeDocumentPath;
          const auto importPath = document.getPath();
          commitDocumentChanges({std::move(document)}, sourcePath, importPath,
                                tr("Import Document"), tr("Document Import Error"));
        } catch (const std::exception& error) {
          SILICON::ui::inputDialog::critical(
              dialogParent, tr("Document Import Error"),
              tr("Failed to import the document:\n%1").arg(error.what()));
        }
      });
}

void ProjectDocumentController::exportSelectedDocument()
{
  const auto selection = projectTree ? projectTree->selectedDocument() : std::nullopt;
  if (!selection)
    return;

  try {
    if (selection->path == session.activeDocumentPath)
      workspace.flushActiveDocument();

    const auto* document = session.projectContext.documents().find(selection->path);
    if (!document)
      throw std::runtime_error("The selected document no longer exists");

    const auto& contents = document->getContents();
    SILICON::ui::fileDialog::saveFileContent(
        dialogParent, tr("Export Document"),
        QString::fromStdString(SILICON::project::documentFileName(*document)),
        tr("All Files (*)"),
        QByteArray(contents.data(), static_cast<qsizetype>(contents.size())));
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::critical(
        dialogParent, tr("Document Export Error"),
        tr("Failed to export the document:\n%1").arg(error.what()));
  }
}

void ProjectDocumentController::createCircuit()
{
  createDocument(SILICON::project::DocumentType::Circuit);
}

void ProjectDocumentController::createCodeFile()
{
  auto* dialog = new QDialog(dialogParent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("New Code File"));
  dialog->setModal(true);

  auto* form     = new QFormLayout(dialog);
  auto* nameEdit = new QLineEdit(tr("untitled"), dialog);
  auto* typeBox  = new QComboBox(dialog);
  for (const auto& info : codeFilePresentations()) {
    if (info.type == SILICON::project::DocumentType::Verilog)
      typeBox->addItem(QString::fromUtf8(info.displayName), static_cast<int>(info.type));
  }

  form->addRow(tr("Name"), nameEdit);
  form->addRow(tr("File type"), typeBox);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(
      buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, nameEdit, typeBox] {
        const auto index = typeBox->currentIndex();
        if (index < 0)
          return;

        const auto type =
            static_cast<SILICON::project::DocumentType>(typeBox->currentData().toInt());
        const auto slug = nameEdit->text().trimmed().toStdString();

        if (!SILICON::project::isValidDocumentSlug(slug)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, tr("New Code File"),
              tr("The name must be non-empty and cannot contain path separators."));
          return;
        }
        const auto path = SILICON::project::documentPathForSlug(type, slug);
        if (session.projectContext.documents().contains(path)) {
          SILICON::ui::inputDialog::warning(dialogParent, tr("New Code File"),
                                            tr("A code file named '%1' already exists.")
                                                .arg(QString::fromStdString(path)));
          return;
        }

        pushCreateDocumentCommand({path, ""}, tr("Create Code File"));
        dialog->accept();
      });

  nameEdit->selectAll();
  nameEdit->setFocus();
  dialog->open();
}

void ProjectDocumentController::createBinaryFile()
{
  constexpr int MAXIMUM_BINARY_SIZE = 256 * 1024 * 1024;

  auto* dialog = new QDialog(dialogParent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("New Binary File"));
  dialog->setModal(true);

  auto* form     = new QFormLayout(dialog);
  auto* nameEdit = new QLineEdit(tr("untitled"), dialog);
  auto* sizeEdit = new QSpinBox(dialog);
  sizeEdit->setRange(1, MAXIMUM_BINARY_SIZE);
  sizeEdit->setValue(256);
  sizeEdit->setSuffix(tr(" bytes"));

  form->addRow(tr("Name"), nameEdit);
  form->addRow(tr("Size"), sizeEdit);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(
      buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, nameEdit, sizeEdit] {
        const auto slug = nameEdit->text().trimmed().toStdString();
        if (!SILICON::project::isValidDocumentSlug(slug)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, tr("New Binary File"),
              tr("The name must be non-empty and cannot contain path separators."));
          return;
        }

        const auto path = SILICON::project::documentPathForSlug(
            SILICON::project::DocumentType::RawBinary, slug);
        if (session.projectContext.documents().contains(path)) {
          SILICON::ui::inputDialog::warning(dialogParent, tr("New Binary File"),
                                            tr("A binary file named '%1' already exists.")
                                                .arg(QString::fromStdString(path)));
          return;
        }

        const auto roundedSize =
            std::bit_ceil(static_cast<unsigned int>(sizeEdit->value()));
        pushCreateDocumentCommand(
            {path, std::string(static_cast<std::size_t>(roundedSize), '\0')},
            tr("Create Binary File"));
        dialog->accept();
      });

  nameEdit->selectAll();
  nameEdit->setFocus();
  dialog->open();
}

void ProjectDocumentController::createDocument(const SILICON::project::DocumentType type)
{
  if (SILICON::project::categoryOf(type) != SILICON::project::DocumentCategory::Diagram)
    throw std::invalid_argument("createDocument requires a graphical document type");

  const auto noun = documentTypeName(type);
  SILICON::ui::inputDialog::getText(
      dialogParent, tr("New %1").arg(noun), tr("%1 name").arg(noun), noun,
      [this, type, noun](const QString& requestedName) {
        const auto trimmed     = requestedName.trimmed();
        const auto displayName = trimmed.isEmpty() ? noun : trimmed;
        const auto path        = projectDocumentPolicy::uniqueDocumentPath(
            session.projectContext, type, displayName);
        const auto sceneJson = projectDocumentPolicy::emptyGraphicalDocumentJson(type);

        SILICON::project::Document document(path, sceneJson);

        pushCreateDocumentCommand(std::move(document), tr("Create %1").arg(noun));
      });
}

void ProjectDocumentController::pushDocumentSnapshotCommand(
    QString commandText, std::vector<SILICON::project::Document> before,
    std::string beforeActive, std::vector<SILICON::project::Document> after,
    std::string afterActive)
{
  undoStack->push(new CallbackUndoCommand(
      std::move(commandText),
      [this, before = std::move(before), beforeActive = std::move(beforeActive)] {
        this->restoreProjectDocuments(before, beforeActive);
      },
      [this, after = std::move(after), afterActive = std::move(afterActive)] {
        this->restoreProjectDocuments(after, afterActive);
      }));
}

void ProjectDocumentController::renameSelectedDocument()
{
  const auto selection = projectTree ? projectTree->selectedDocument() : std::nullopt;
  if (!selection)
    return;
  const auto slug = SILICON::project::documentSlugForPath(selection->path);
  if (!slug)
    return;
  if (ProjectTree::itemKind(projectTree->selectedProjectItem())
      == ProjectTreeItemKind::Architecture) {
    renameArchitecture(*slug);
    return;
  }

  const auto noun  = documentTypeName(selection->type);
  const auto title = tr("Rename %1").arg(noun);
  SILICON::ui::inputDialog::getText(
      dialogParent, title, tr("Name"), QString::fromStdString(*slug),
      [this, selection = *selection, noun, title](const QString& requestedName) {
        const auto newSlug = requestedName.trimmed().toStdString();
        if (!SILICON::project::isValidDocumentSlug(newSlug)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, title,
              tr("The name must be non-empty and cannot contain path separators."));
          return;
        }
        const auto newPath =
            SILICON::project::documentPathForSlug(selection.type, newSlug);
        if (newPath == selection.path)
          return;
        if (session.projectContext.documents().contains(newPath)) {
          SILICON::ui::inputDialog::warning(
              dialogParent, title,
              tr("A %1 named '%2' already exists.")
                  .arg(noun.toLower(), QString::fromStdString(newPath)));
          return;
        }

        try {
          workspace.flushActiveDocument();
          const auto before       = session.projectContext.documents().getDocuments();
          const auto beforeActive = session.activeDocumentPath;
          SILICON::project::ProjectContext renamed;
          renamed.setDocuments(before);
          renamed.renameDocument(selection.path, newPath);
          const auto afterActive =
              beforeActive == selection.path ? newPath : beforeActive;
          pushDocumentSnapshotCommand(title, before, beforeActive,
                                      renamed.documents().getDocuments(), afterActive);
        } catch (const std::exception& error) {
          SILICON::ui::inputDialog::warning(dialogParent, title, error.what());
        }
      });
}

void ProjectDocumentController::removeProjectDocuments(
    const std::vector<std::string>& paths, const QString& commandText)
{
  try {
    workspace.flushActiveDocument();
    const auto before = session.projectContext.documents().getDocuments();
    auto       after  = before;
    std::erase_if(after, [&paths](const auto& document) {
      return std::ranges::find(paths, document.getPath()) != paths.end();
    });
    if (after.size() == before.size())
      return;

    const auto beforeActive = session.activeDocumentPath;
    auto       afterActive  = beforeActive;
    if (std::ranges::find(paths, beforeActive) != paths.end()) {
      const auto activeType = SILICON::project::documentTypeForPath(beforeActive);
      const auto architecture =
          activeType
                  && SILICON::project::categoryOf(*activeType)
                         == SILICON::project::DocumentCategory::Architecture
              ? SILICON::project::documentSlugForPath(beforeActive)
              : std::nullopt;
      const auto prefix = architecture
                              ? SILICON::project::architectureDirectory(*architecture)
                              : std::string{};
      const auto nextComponent =
          architecture
              ? std::ranges::find_if(after,
                                     [&](const auto& document) {
                                       return document.getPath().starts_with(prefix);
                                     })
              : after.end();
      if (nextComponent != after.end())
        afterActive = nextComponent->getPath();
      else if (const auto circuit = std::ranges::find_if(
                   after,
                   [](const auto& document) {
                     return document.getType() == SILICON::project::DocumentType::Circuit;
                   });
               circuit != after.end())
        afterActive = circuit->getPath();
    }

    pushDocumentSnapshotCommand(commandText, before, beforeActive, std::move(after),
                                afterActive);
  } catch (const std::exception& error) {
    SILICON::ui::inputDialog::warning(dialogParent, commandText, error.what());
  }
}

void ProjectDocumentController::deleteSelectedDocument()
{
  const auto selection = projectTree ? projectTree->selectedDocument() : std::nullopt;
  auto*      item      = projectTree ? projectTree->selectedProjectItem() : nullptr;
  if (!selection || !item)
    return;

  if (ProjectTree::itemKind(item) == ProjectTreeItemKind::Architecture) {
    deleteArchitecture(ProjectTree::architectureName(item));
    return;
  }

  if (selection->type == SILICON::project::DocumentType::Circuit) {
    const auto circuitCount = std::ranges::count_if(
        session.projectContext.documents().getDocuments(), [](const auto& document) {
          return document.getType() == SILICON::project::DocumentType::Circuit;
        });
    if (circuitCount <= 1)
      return;
  }

  const auto noun  = documentTypeName(selection->type);
  const auto title = tr("Delete %1").arg(noun);

  try {
    workspace.flushActiveDocument();
  } catch (const std::exception& e) {
    SILICON::ui::inputDialog::warning(
        dialogParent, title,
        tr("Failed to save the active document before deleting it:\n%1").arg(e.what()));
    return;
  }

  if (selection->type == SILICON::project::DocumentType::Circuit) {
    const auto dependents =
        session.projectContext.circuitDependencies().dependentsOf(selection->path);
    if (!dependents.empty()) {
      QStringList names;
      for (const auto& dependent : dependents)
        names.push_back(QString::fromStdString(dependent));

      SILICON::ui::inputDialog::warning(
          dialogParent, title,
          tr("This subcircuit is still used by:\n%1").arg(names.join('\n')));
      return;
    }
  }

  SILICON::ui::inputDialog::question(
      dialogParent, title, tr("Delete %1 \"%2\"?").arg(noun.toLower(), item->text(0)),
      [this, path = selection->path, title] {
        const auto& store          = session.projectContext.documents();
        const auto* storedDocument = store.find(path);
        const auto  storedIndex    = store.indexOf(path);
        if (!storedDocument || !storedIndex)
          return;

        const auto document = *storedDocument;
        const auto index    = static_cast<std::ptrdiff_t>(*storedIndex);

        auto removeStored    = [this, path] { this->removeDocument(path); };
        auto restoreDocument = [this, document, index] {
          this->insertDocument(document, index, true);
        };

        undoStack->push(new CallbackUndoCommand(title, restoreDocument, removeStored));
      });
}

void ProjectDocumentController::commitDocumentChanges(
    std::vector<SILICON::project::Document> documents, const std::string& sourcePath,
    const std::string& activatePath, const QString& commandText,
    const QString& errorTitle)
{
  QStringList conflicts;
  for (const auto& document : documents) {
    if (session.projectContext.documents().contains(document.getPath()))
      conflicts.push_back(QString::fromStdString(document.getPath()));
  }

  auto commit = [this, documents = std::move(documents), sourcePath, activatePath,
                 commandText, errorTitle]() mutable {
    try {
      const auto& store           = session.projectContext.documents();
      auto        beforeDocuments = store.getDocuments();
      auto        afterDocuments  = beforeDocuments;

      for (auto& document : documents) {
        const auto existing = std::ranges::find(afterDocuments, document.getPath(),
                                                &SILICON::project::Document::getPath);
        if (existing == afterDocuments.end())
          afterDocuments.push_back(std::move(document));
        else
          *existing = std::move(document);
      }

      SILICON::project::CircuitDependencyGraph validatedDependencies;
      validatedDependencies.rebuildFromProject(afterDocuments);

      pushDocumentSnapshotCommand(commandText, std::move(beforeDocuments), sourcePath,
                                  std::move(afterDocuments), activatePath);
    } catch (const std::exception& error) {
      SILICON::ui::inputDialog::critical(
          dialogParent, errorTitle,
          tr("Failed to update the project documents:\n%1").arg(error.what()));
    }
  };

  if (!conflicts.empty()) {
    SILICON::ui::inputDialog::question(
        dialogParent, tr("Replace Existing Documents"),
        tr("The following documents already exist and will be replaced:\n\n%1")
            .arg(conflicts.join('\n')),
        std::move(commit));
  } else {
    commit();
  }
}

}  // namespace SILICON::ui
