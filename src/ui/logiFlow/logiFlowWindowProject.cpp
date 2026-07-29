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
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>
#include <core/serialization/yosys.hpp>
#include <core/simulator.hpp>
#include <core/subcircuitDefinition.hpp>
#include <logging/logger.hpp>
#include <ui/common/aboutDialog.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/diagramView.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/graphicalLogStream.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/logSideView.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/common/waveformViewer.hpp>
#include <ui/logiFlow/componentCatalogOverlay.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/componentShapeEditor.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/logiFlow/hdlCodeEditor.hpp>
#include <ui/logiFlow/metadataDescriptionEdit.hpp>
#include <ui/logiFlow/projectTree.hpp>
#include <ui/serialization/gui_component_factory.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

const SILICON::logging::Logger uiLog("ui");

QString defaultProjectName(const QString& currentFileName)
{
  const QString baseName = QFileInfo(currentFileName).baseName();
  return baseName.isEmpty() ? QStringLiteral("Untitled Project") : baseName;
}

}  // namespace

std::string LogiFlowWindow::defaultMainCircuitPath()
{
  return std::string(SILICON::project::DefaultMainCircuitPath);
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

std::string
LogiFlowWindow::projectMainCircuitPath() const
{
  if (currentProjectInfo && !currentProjectInfo->mainCircuit.empty())
    return currentProjectInfo->mainCircuit;

  return defaultMainCircuitPath();
}

void LogiFlowWindow::ensureProjectDocuments()
{
  auto& store = SILICON::project::DocumentStore::active();
  if (store.documents(SILICON::project::DocumentKind::Circuit).empty())
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

std::string LogiFlowWindow::activeProjectCircuitPath() const
{
  if (!activeDocumentPath.empty()
      && SILICON::project::classifyDocumentPath(activeDocumentPath)
             == SILICON::project::DocumentKind::Circuit)
    return activeDocumentPath;

  return projectMainCircuitPath();
}

bool LogiFlowWindow::activateProjectCircuit(const std::string& circuitPath)
{
  return activateProjectDocument(circuitPath);
}

std::string LogiFlowWindow::activeProjectSubcircuitSlug() const
{
  return SILICON::project::subcircuitSlugForPath(activeDocumentPath)
      .value_or(std::string{});
}

bool LogiFlowWindow::activateProjectDocument(const std::string& documentPath)
{
  if (!SILICON::project::classifyDocumentPath(documentPath)
      || !SILICON::project::DocumentStore::active().contains(documentPath))
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

bool LogiFlowWindow::hasDocument(const std::string& path) const
{
  return SILICON::project::DocumentStore::active().contains(path);
}

std::string LogiFlowWindow::emptyCircuitSceneJson(const std::string& name) const
{
  const auto             circuitName = name.empty() ? std::string("Circuit") : name;
  nlohmann::ordered_json scene;
  scene["circuit"] =
      nlohmann::ordered_json{{"version", SILICON_VERSION},
                             {"name", circuitName},
                             {"description", ""},
                             {"components", nlohmann::ordered_json::array()}};
  scene["visual"]["components"] = nlohmann::ordered_json::array();
  scene["visual"]["wires"]      = nlohmann::ordered_json::array();
  return scene.dump(2);
}

std::string LogiFlowWindow::emptySubcircuitSceneJson(const std::string& name) const
{
  auto scene = nlohmann::ordered_json::parse(emptyCircuitSceneJson(name));
  scene["graphicalComponent"] =
      nlohmann::ordered_json{{"shape",
                              {{"type", "rectangle"},
                               {"width", GraphicalSubcircuitDefaultSize},
                               {"height", GraphicalSubcircuitDefaultSize}}},
                             {"inputs", nlohmann::ordered_json::array()},
                             {"outputs", nlohmann::ordered_json::array()}};
  return scene.dump(2);
}

std::string LogiFlowWindow::uniqueDocumentPath(const SILICON::project::DocumentKind kind,
                                               const QString& requestedName) const
{
  const auto trimmed = requestedName.trimmed();
  const auto fallback =
      kind == SILICON::project::DocumentKind::Circuit ? "circuit" : "subcircuit";
  const auto directory =
      kind == SILICON::project::DocumentKind::Circuit ? "circuits" : "subcircuits";
  std::string slug = trimmed.isEmpty() ? fallback : trimmed.toStdString();

  for (char& ch : slug) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte)) {
      ch = static_cast<char>(std::tolower(byte));
    } else if (ch != '-' && ch != '_') {
      ch = '_';
    }
  }

  if (const auto first = slug.find_first_not_of('_'); first == std::string::npos) {
    slug = fallback;
  } else {
    const auto last = slug.find_last_not_of('_');
    slug            = slug.substr(first, last - first + 1);
  }

  if (kind == SILICON::project::DocumentKind::Subcircuit) {
    if (!slug.empty() && std::isdigit(static_cast<unsigned char>(slug.front())))
      slug.insert(slug.begin(), '_');
    std::ranges::replace(slug, '-', '_');
  }

  auto candidate = std::format("{}/{}.json", directory, slug);
  int  suffix    = 2;
  while (hasDocument(candidate)) {
    candidate = kind == SILICON::project::DocumentKind::Subcircuit
                    ? std::format("{}/{}_{}.json", directory, slug, suffix)
                    : std::format("{}/{}-{}.json", directory, slug, suffix);
    ++suffix;
  }

  return candidate;
}

void LogiFlowWindow::saveActiveDocumentPayload()
{
  if (activeDocumentPath.empty())
    activeDocumentPath = projectMainCircuitPath();

  if (activeDocumentHasHdl()) {
    if (hdlCodeMode)
      compileActiveHdl();
    return;
  }

  auto&      store           = SILICON::project::DocumentStore::active();
  auto       serializedScene = diagramScene->serialize();
  const auto kind            = SILICON::project::classifyDocumentPath(activeDocumentPath);
  if (!kind)
    throw std::runtime_error("Active project document path is invalid");

  if (*kind == SILICON::project::DocumentKind::Subcircuit) {
    if (const auto* existing = store.find(activeDocumentPath)) {
      try {
        auto       newJson  = nlohmann::json::parse(serializedScene);
        const auto fallback = parseGraphicalSubcircuitMetadata(existing->sceneJson())
                                  .value_or(GraphicalSubcircuitMetadata{});
        newJson["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
            synchronizeGraphicalSubcircuitMetadata(serializedScene, fallback));
        serializedScene = newJson.dump(2);
      } catch (const nlohmann::json::exception&) {
      }
    }
  }

  dependencyGraph.replaceDocumentDependencies(activeDocumentPath, serializedScene);
  if (*kind == SILICON::project::DocumentKind::Subcircuit)
    store.upsertDocument(
        preparedSubcircuitDocument(activeDocumentPath, std::move(serializedScene)));
  else
    store.upsertDocument({activeDocumentPath, std::move(serializedScene)});
}

void LogiFlowWindow::selectProjectTreeDocument(const std::string& path)
{
  if (projectTree)
    projectTree->selectDocument(path);
}

bool LogiFlowWindow::switchToDocument(const std::string& path, const bool selectInTree)
{
  auto&      store = SILICON::project::DocumentStore::active();
  const auto kind  = SILICON::project::classifyDocumentPath(path);
  if (path.empty() || !kind || !store.contains(path))
    return false;

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
    const auto noun = *kind == SILICON::project::DocumentKind::Circuit ? tr("Circuit")
                                                                       : tr("Subcircuit");
    SILICON::ui::inputDialog::warning(
        this, tr("%1 Switch Error").arg(noun),
        tr("Failed to save the current document before switching:\n%1").arg(e.what()));
    return false;
  }

  const auto* target = store.find(path);
  if (!target)
    return false;
  const auto payload = target->sceneJson();

  try {
    diagramScene->clear(false, false);
    diagramScene->setSubcircuitDocumentMode(
        *kind == SILICON::project::DocumentKind::Subcircuit);

    activeDocumentPath = path;
    hdlCodeMode        = false;
    if (SILICON::project::parseHdlDescriptor(payload)) {
      showActiveHdlDocument();
    } else {
      auto& guiFactory   = GUIComponentFactory::instance();
      auto& coreRegistry = ComponentRegistry::instance();
      diagramScene->deserialize(payload, guiFactory, coreRegistry);
      editorStack->setCurrentWidget(diagramView);
    }
    updateSubcircuitShapeAction();
  } catch (const std::exception& e) {
    const auto noun = *kind == SILICON::project::DocumentKind::Circuit ? tr("Circuit")
                                                                       : tr("Subcircuit");
    SILICON::ui::inputDialog::critical(
        this, tr("%1 Switch Error").arg(noun),
        tr("Failed to load the selected %1:\n%2").arg(noun.toLower(), e.what()));
    return false;
  }

  if (selectInTree)
    selectProjectTreeDocument(path);

  setActionsEnabled({rotateAct, cutAct, copyAct, deleteAct}, false);
  updatePropertyDock();
  return true;
}

void LogiFlowWindow::removeDocument(const std::string& path)
{
  auto& store = SILICON::project::DocumentStore::active();
  if (!store.contains(path))
    return;

  std::optional<SILICON::project::HdlDescriptor> hdl;
  if (const auto* document = store.find(path);
      document && document->kind() == SILICON::project::DocumentKind::Subcircuit) {
    hdl = SILICON::project::parseHdlDescriptor(document->sceneJson());
  }

  if (activeDocumentPath == path)
    switchToDocument(projectMainCircuitPath(), true);

  store.removeDocument(path);
  if (hdl) {
    std::erase_if(projectAssets,
                  [&](const auto& asset) { return asset.path == hdl->path; });
  }
  dependencyGraph.removeDocument(path);
  rebuildProjectTree();
  selectProjectTreeDocument(activeProjectCircuitPath());
  updatePropertyDock();
}

void LogiFlowWindow::insertDocument(SILICON::project::Document          document,
                                    const std::optional<std::ptrdiff_t> insertAt,
                                    const bool                          activate)
{
  auto&      store = SILICON::project::DocumentStore::active();
  const auto path  = document.path();
  if (store.contains(path))
    return;

  dependencyGraph.addDocument(path);
  try {
    dependencyGraph.replaceDocumentDependencies(path, document.sceneJson());
  } catch (...) {
    dependencyGraph.removeDocument(path);
    throw;
  }

  if (insertAt)
    store.insertDocument(
        std::move(document),
        static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, *insertAt)));
  else
    store.upsertDocument(std::move(document));

  rebuildProjectTree();
  if (activate)
    switchToDocument(path, true);
}

void LogiFlowWindow::rebuildProjectTree()
{
  if (!projectTree)
    return;

  ensureProjectDocuments();
  const auto  project = currentProjectInfo.value_or(defaultProjectInfo(currentFileName));
  const auto& store   = SILICON::project::DocumentStore::active();
  projectTree->rebuild(project, store.documents(SILICON::project::DocumentKind::Circuit),
                       store.documents(SILICON::project::DocumentKind::Subcircuit),
                       activeDocumentPath);
}

void LogiFlowWindow::updateProjectTreeLabels()
{
  if (!projectTree)
    return;
  const auto  project = currentProjectInfo.value_or(defaultProjectInfo(currentFileName));
  const auto& store   = SILICON::project::DocumentStore::active();
  projectTree->updateLabels(project,
                            store.documents(SILICON::project::DocumentKind::Circuit),
                            store.documents(SILICON::project::DocumentKind::Subcircuit));
}

QTreeWidgetItem* LogiFlowWindow::projectDocumentSectionItem(
    const SILICON::project::DocumentKind kind) const
{
  return projectTree ? projectTree->sectionFor(kind) : nullptr;
}

void LogiFlowWindow::clearProjectTreeSelection()
{
  if (projectTree)
    projectTree->clearDocumentSelection();
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

void LogiFlowWindow::closeEvent(QCloseEvent* event)
{
  try {
    saveActiveDocumentPayload();
    QMainWindow::closeEvent(event);
  } catch (const std::exception& error) {
    event->ignore();
    SILICON::ui::inputDialog::critical(
        this, tr("HDL Error"),
        tr("Compile the active HDL before closing the project:\n%1").arg(error.what()));
  }
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
