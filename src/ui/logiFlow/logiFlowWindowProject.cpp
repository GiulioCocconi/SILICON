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

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <QByteArray>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QMenu>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QTreeWidget>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>
#include <ui/common/binaryEditor.hpp>
#include <ui/common/codeEditor.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

  QString defaultProjectName(const QString& currentFileName)
  {
    const QString baseName = QFileInfo(currentFileName).baseName();
    return baseName.isEmpty() ? QStringLiteral("Untitled Project") : baseName;
  }

}  // namespace

QString LogiFlowWindow::documentTypeName(const SILICON::project::DocumentType type)
{
  switch (type) {
    case SILICON::project::DocumentType::Circuit: return tr("Circuit");
    case SILICON::project::DocumentType::Subcircuit: return tr("Subcircuit");
    case SILICON::project::DocumentType::Code: return tr("Code File");
    case SILICON::project::DocumentType::Binary: return tr("Binary File");
  }
  return {};
}

std::string LogiFlowWindow::defaultMainCircuitPath()
{
  return std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH);
}

SILICON::project::Document LogiFlowWindow::defaultCircuitDocument()
{
  return {defaultMainCircuitPath(), ""};
}

SILICON::project::ProjectInfo
LogiFlowWindow::defaultProjectInfo(const QString& currentFileName)
{
  return {.name        = defaultProjectName(currentFileName).toStdString(),
          .mainCircuit = defaultMainCircuitPath(),
          .description = ""};
}

std::string LogiFlowWindow::projectMainCircuitPath() const
{
  if (currentProjectInfo && !currentProjectInfo->mainCircuit.empty())
    return currentProjectInfo->mainCircuit;

  return defaultMainCircuitPath();
}

void LogiFlowWindow::ensureProjectDocuments()
{
  auto& store = SILICON::project::DocumentStore::active();
  if (!store.contains(SILICON::project::DocumentType::Circuit))
    store.upsertDocument(defaultCircuitDocument());
}

void LogiFlowWindow::initializeProjectTree()
{
  projectTree = new ProjectTree(componentsDock);
  componentsDock->setWidget(projectTree);

  connect(projectTree, &QTreeWidget::itemSelectionChanged, this,
          &LogiFlowWindow::projectTreeSelectionChanged);
  connect(projectTree, &QTreeWidget::customContextMenuRequested, this,
          &LogiFlowWindow::showProjectTreeContextMenu);
}

bool LogiFlowWindow::activateProjectDocument(const std::string& documentPath)
{
  if (!SILICON::project::DocumentStore::active().contains(documentPath))
    return false;

  if (documentPath == activeDocumentPath) {
    selectProjectTreeDocument(documentPath);
    return true;
  }

  return switchToDocument(documentPath, true);
}

std::shared_ptr<Circuit> LogiFlowWindow::activeCircuit()
{
  return diagramScene->getCircuit();
}

std::string LogiFlowWindow::emptyGraphicalDocumentJson(
    const SILICON::project::DocumentType type, const std::string& name) const
{
  const auto circuitName =
      name.empty() ? documentTypeName(type).toStdString() : name;

  nlohmann::ordered_json scene;
  scene["circuit"] =
      nlohmann::ordered_json{{"version", SILICON_VERSION},
                             {"name", circuitName},
                             {"description", ""},
                             {"components", nlohmann::ordered_json::array()}};
  scene["visual"]["components"] = nlohmann::ordered_json::array();
  scene["visual"]["wires"]      = nlohmann::ordered_json::array();

  if (type == SILICON::project::DocumentType::Subcircuit) {
    scene["graphicalComponent"] =
        nlohmann::ordered_json{{"shape",
                                {{"type", "rectangle"},
                                 {"width", GraphicalSubcircuitDefaultSize},
                                 {"height", GraphicalSubcircuitDefaultSize}}},
                               {"inputs", nlohmann::ordered_json::array()},
                               {"outputs", nlohmann::ordered_json::array()}};
  }

  return scene.dump(2);
}

std::string LogiFlowWindow::uniqueDocumentPath(
    const SILICON::project::DocumentType type, const QString& requestedName) const
{
  if (!SILICON::project::documentTypeInfo(type).isGraphical)
    throw std::invalid_argument("Only graphical documents use generated slugs");

  const bool subcircuit = type == SILICON::project::DocumentType::Subcircuit;
  const auto fallback   = subcircuit ? std::string("subcircuit") : std::string("circuit");

  auto slug = requestedName.trimmed().toStdString();
  if (slug.empty())
    slug = fallback;

  for (char& ch : slug) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte))
      ch = static_cast<char>(std::tolower(byte));
    else if (ch != '-' && ch != '_')
      ch = '_';
  }

  if (const auto first = slug.find_first_not_of('_'); first == std::string::npos) {
    slug = fallback;
  } else {
    slug = slug.substr(first, slug.find_last_not_of('_') - first + 1);
  }

  if (subcircuit) {
    if (std::isdigit(static_cast<unsigned char>(slug.front())))
      slug.insert(slug.begin(), '_');
    std::ranges::replace(slug, '-', '_');
  }

  auto& store = SILICON::project::DocumentStore::active();
  auto candidate = SILICON::project::documentPathForSlug(type, slug);
  for (int suffix = 2; store.contains(candidate); ++suffix) {
    const auto numberedSlug = subcircuit ? std::format("{}_{}", slug, suffix)
                                           : std::format("{}-{}", slug, suffix);
    candidate = SILICON::project::documentPathForSlug(type, numberedSlug);
  }

  return candidate;
}

void LogiFlowWindow::saveActiveDocumentPayload()
{
  if (activeDocumentPath.empty())
    activeDocumentPath = projectMainCircuitPath();

  auto& store = SILICON::project::DocumentStore::active();
  const auto* activeDocument = store.find(activeDocumentPath);
  if (!activeDocument)
    throw std::runtime_error("Active project document is missing");

  const auto type = activeDocument->getType();
  switch (type) {
    case SILICON::project::DocumentType::Code:
      store.upsertDocument(
          {activeDocumentPath, codeEditor->toPlainText().toStdString()});
      codeEditor->document()->setModified(false);
      return;

    case SILICON::project::DocumentType::Binary: {
      const auto& data = binaryEditor->data();
      store.upsertDocument(
          {activeDocumentPath,
           std::string(data.constData(), static_cast<std::size_t>(data.size()))});
      binaryEditor->setModified(false);
      return;
    }

    case SILICON::project::DocumentType::Circuit:
    case SILICON::project::DocumentType::Subcircuit:
      break;
  }

  auto serializedScene = diagramScene->serialize();

  if (type == SILICON::project::DocumentType::Subcircuit) {
    if (const auto* existing = store.find(activeDocumentPath)) {
      try {
        auto newJson = nlohmann::json::parse(serializedScene);
        const auto fallback =
            parseGraphicalSubcircuitMetadata(existing->getContents())
                .value_or(GraphicalSubcircuitMetadata{});
        newJson["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
            synchronizeGraphicalSubcircuitMetadata(serializedScene, fallback));
        serializedScene = newJson.dump(2);
      } catch (const nlohmann::json::exception&) {
      }
    }
  }

  dependencyGraph.replaceDocumentDependencies(activeDocumentPath, serializedScene);

  if (type == SILICON::project::DocumentType::Subcircuit)
    store.upsertDocument(
        preparedSubcircuitDocument(activeDocumentPath, std::move(serializedScene)));
  else
    store.upsertDocument({activeDocumentPath, std::move(serializedScene)});
}

void LogiFlowWindow::loadDocumentPayload(const SILICON::project::Document& document)
{
  const auto type = document.getType();
  const auto& payload = document.getContents();

  switch (type) {
    case SILICON::project::DocumentType::Code: {
      const auto codeType = SILICON::project::codeFileTypeForPath(document.getPath());
      if (!codeType)
        throw std::runtime_error("Code file type is invalid");

      codeEditor->setFileType(*codeType);
      codeEditor->setPlainText(QString::fromStdString(payload));
      codeEditor->document()->setModified(false);
      editorStack->setCurrentWidget(codeEditor);
      codeEditor->setFocus();
      break;
    }

    case SILICON::project::DocumentType::Binary:
      binaryEditor->setData(
          QByteArray(payload.data(), static_cast<qsizetype>(payload.size())));
      editorStack->setCurrentWidget(binaryEditor);
      binaryEditor->setFocus();
      break;

    case SILICON::project::DocumentType::Circuit:
    case SILICON::project::DocumentType::Subcircuit: {
      diagramScene->clear(false, false);
      diagramScene->setSubcircuitDocumentMode(
          type == SILICON::project::DocumentType::Subcircuit);
      auto& guiFactory   = GUIComponentFactory::instance();
      auto& coreRegistry = ComponentRegistry::instance();
      diagramScene->deserialize(payload, guiFactory, coreRegistry);
      editorStack->setCurrentWidget(diagramView);
      break;
    }
  }
}

void LogiFlowWindow::selectProjectTreeDocument(const std::string& path)
{
  if (projectTree)
    projectTree->selectDocument(path);
}

bool LogiFlowWindow::switchToDocument(const std::string& path,
                                       const bool selectInTree)
{
  auto& store = SILICON::project::DocumentStore::active();
  const auto* target = store.find(path);
  if (!target)
    return false;

  const auto type = target->getType();
  if (path == activeDocumentPath) {
    if (selectInTree)
      selectProjectTreeDocument(path);
    updateSubcircuitShapeAction();
    updatePropertyDock();
    return true;
  }

  if (componentCatalogOverlay)
    componentCatalogOverlay->hide();

  if (diagramScene->getInteractionMode() != InteractionMode::NORMAL_MODE)
    diagramScene->setInteractionMode(InteractionMode::NORMAL_MODE);

  try {
    saveActiveDocumentPayload();
  } catch (const std::exception& e) {
    const auto noun = documentTypeName(type);
    SILICON::ui::inputDialog::warning(
        this, tr("%1 Switch Error").arg(noun),
        tr("Failed to save the current document before switching:\n%1").arg(e.what()));
    return false;
  }

  target = store.find(path);
  if (!target)
    return false;

  try {
    activeDocumentPath = path;
    loadDocumentPayload(*target);
    updateSubcircuitShapeAction();
  } catch (const std::exception& e) {
    const auto noun = documentTypeName(type);
    SILICON::ui::inputDialog::critical(
        this, tr("%1 Switch Error").arg(noun),
        tr("Failed to load the selected %1:\n%2").arg(noun.toLower(), e.what()));
    return false;
  }

  if (selectInTree)
    selectProjectTreeDocument(path);

  const bool code = type == SILICON::project::DocumentType::Code;
  setActionsEnabled({cutAct, copyAct, pasteAct, deleteAct}, code);
  rotateAct->setEnabled(false);
  updatePropertyDock();
  return true;
}

void LogiFlowWindow::removeDocument(const std::string& path)
{
  auto& store = SILICON::project::DocumentStore::active();
  const auto* document = store.find(path);
  if (!document)
    return;

  const bool graphical =
      SILICON::project::documentTypeInfo(document->getType()).isGraphical;
  if (graphical)
    dependencyGraph.validateDocumentRemoval(path);

  if (activeDocumentPath == path
      && !switchToDocument(projectMainCircuitPath(), true))
    return;

  if (graphical)
    dependencyGraph.removeDocument(path);

  store.removeDocument(path);
  rebuildProjectTree();
  selectProjectTreeDocument(activeDocumentPath);
  updatePropertyDock();
}

void LogiFlowWindow::insertDocument(SILICON::project::Document          document,
                                     const std::optional<std::ptrdiff_t> insertAt,
                                     const bool                          activate)
{
  auto& store = SILICON::project::DocumentStore::active();
  const auto path = document.getPath();
  if (store.contains(path))
    return;

  const bool graphical =
      SILICON::project::documentTypeInfo(document.getType()).isGraphical;

  if (graphical) {
    dependencyGraph.addDocument(path);
    try {
      dependencyGraph.replaceDocumentDependencies(path, document.getContents());
    } catch (...) {
      dependencyGraph.removeDocument(path);
      throw;
    }
  }

  try {
    if (insertAt)
      store.insertDocument(
          std::move(document),
          static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, *insertAt)));
    else
      store.upsertDocument(std::move(document));
  } catch (...) {
    if (graphical && !store.contains(path))
      dependencyGraph.removeDocument(path);
    throw;
  }

  rebuildProjectTree();
  if (activate)
    switchToDocument(path, true);
}

void LogiFlowWindow::rebuildProjectTree()
{
  if (!projectTree)
    return;

  ensureProjectDocuments();
  const auto project = currentProjectInfo.value_or(defaultProjectInfo(currentFileName));
  const auto& store  = SILICON::project::DocumentStore::active();
  projectTree->rebuild(project, store.getDocuments(), activeDocumentPath);
}

void LogiFlowWindow::setFileName(const QString& fn)
{
  currentFileName               = fn;
  const QString displayFileName = QFileInfo(currentFileName).fileName();

  if (!displayFileName.isEmpty())
    setWindowTitle(QString("LogiFlow - %1").arg(displayFileName));
  else
    setWindowTitle("SILICON LogiFlow");
}

#ifndef QT_NO_CONTEXTMENU
void LogiFlowWindow::contextMenuEvent(QContextMenuEvent* event)
{
#ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addAction(cutAct);
  menu->addAction(copyAct);
  menu->addAction(pasteAct);
  menu->addAction(rotateAct);
  menu->addAction(deleteAct);
  menu->popup(event->globalPos());
#else
  QMenu menu(this);
  menu.addAction(cutAct);
  menu.addAction(copyAct);
  menu.addAction(pasteAct);
  menu.addAction(rotateAct);
  menu.addAction(deleteAct);
  menu.exec(event->globalPos());
#endif
  event->accept();
}
#endif  // QT_NO_CONTEXTMENU

bool LogiFlowWindow::eventFilter(QObject* watched, QEvent* event)
{
  if (diagramView && watched == diagramView->viewport()
      && event->type() == QEvent::Resize) {
    updateComponentCatalogGeometry();
  }

  return QMainWindow::eventFilter(watched, event);
}

void LogiFlowWindow::resizeEvent(QResizeEvent* event)
{
  QMainWindow::resizeEvent(event);
  updateComponentCatalogGeometry();

  const int currentWidth  = event->size().width();
  const int currentHeight = event->size().height();

  const int minWidth = currentWidth / 10;
  const int maxWidth = currentWidth / 2;

  const int minHeight = currentHeight / 3;

  auto configureSizeConstraints = [minWidth, maxWidth, minHeight](QDockWidget* widget) {
    widget->setMinimumWidth(minWidth);
    widget->setMaximumWidth(maxWidth);
    widget->setMinimumHeight(minHeight);
  };

  configureSizeConstraints(componentsDock);
  configureSizeConstraints(propertyDock);

  logDock->setMinimumWidth(160);
  logDock->setMaximumWidth(QWIDGETSIZE_MAX);
  logDock->setMinimumHeight(120);
  logDock->setMaximumHeight(std::max(160, currentHeight / 3));
}

bool LogiFlowWindow::hasUnsavedChanges() const
{
  return (undoStack && !undoStack->isClean()) || codeDocumentsDirty
         || binaryDocumentsDirty
         || (codeEditor && codeEditor->document()->isModified())
         || (binaryEditor && binaryEditor->isModified());
}

void LogiFlowWindow::closeEvent(QCloseEvent* event)
{
  if (hasUnsavedChanges() && !closeAfterSaveConfirmation) {
    event->ignore();
    confirmSaveIfDirty([this] {
      closeAfterSaveConfirmation = true;
      close();
    });
    return;
  }

  closeAfterSaveConfirmation = false;
  QMainWindow::closeEvent(event);
}

void LogiFlowWindow::updateComponentCatalogGeometry()
{
  if (!componentCatalogOverlay || !diagramView)
    return;

  componentCatalogOverlay->setGeometry(diagramView->viewport()->rect());
}

/* ACTIONS IMPLEMENTATION */

}  // namespace ui
}  // namespace SILICON
