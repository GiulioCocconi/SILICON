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

#include "componentCatalogOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>

#include <QAbstractItemView>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <ui/common/componentSearchMatcher.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/graphicalSubcircuit.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <core/projectDocument.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace {

constexpr int CatalogPreviewPadding            = 8;
constexpr int CatalogCategoryRowHeight         = 32;
constexpr int CatalogMinimumComponentRowHeight = 32;
constexpr int CatalogNameColumnPadding         = 28;

QPixmap componentPreviewPixmap(const ComponentCatalogOverlay::CatalogRow& rowData)
{
  auto         component = GUIComponentFactory::instance().create(rowData.guiType);
  if (!rowData.initialProperties.empty()) {
    if (auto* logicComponent = dynamic_cast<GraphicalLogicComponent*>(component.get())) {
      for (const auto& [key, value] : rowData.initialProperties)
        logicComponent->applyProperty(key, value);
    }
  }
  const QRectF bounds    = component->sceneBoundingRect();
  const QSize  size(
      static_cast<int>(std::ceil(bounds.width())) + 2 * CatalogPreviewPadding,
      static_cast<int>(std::ceil(bounds.height())) + 2 * CatalogPreviewPadding);

  QPixmap pixmap(size);
  pixmap.fill(Qt::transparent);

  QGraphicsScene previewScene;
  previewScene.addItem(component.get());

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  previewScene.render(
      &painter,
      QRectF(QPointF(CatalogPreviewPadding, CatalogPreviewPadding), bounds.size()),
      bounds, Qt::KeepAspectRatio);
  previewScene.removeItem(component.get());

  return pixmap;
}

std::vector<ComponentCatalogOverlay::CatalogRow> componentCatalogRows(
    const bool editingSubcircuit)
{
  Q_UNUSED(editingSubcircuit);

  const auto& guiFactory  = GUIComponentFactory::instance();
  const auto& coreFactory = ComponentRegistry::instance();

  std::vector<ComponentCatalogOverlay::CatalogRow> catalogRows;
  for (const std::string& guiType : guiFactory.availableTypes()) {
    const auto& entryMetadata = guiFactory.metadata(guiType);

    if (!entryMetadata.coreType.empty() && coreFactory.hasType(entryMetadata.coreType)) {
      catalogRows.push_back(
          {.guiType = guiType,
           .metadata = coreFactory.metadata(entryMetadata.coreType),
           .initialProperties = {}});
      continue;
    }

    auto  graphicalComponent = guiFactory.create(guiType);
    auto* logicComponent =
        dynamic_cast<GraphicalLogicComponent*>(graphicalComponent.get());
    if (logicComponent && logicComponent->getComponent())
      catalogRows.push_back(
          {.guiType = guiType,
           .metadata = logicComponent->getComponent()->metadata(),
           .initialProperties = {}});
  }

  for (const auto& document :
       silicon::project::DocumentStore::active().documents(
           silicon::project::DocumentKind::Subcircuit)) {
    if (!subcircuitHasGraphicalMetadata(document.sceneJson()))
      continue;

    const auto slug = document.subcircuitSlug().value_or(std::string{});
    PropertyMap initialProperties;
    initialProperties.emplace(std::string("slug"),
                              PropertyValue(slug));
    catalogRows.push_back(
        {.guiType = std::string(SubcircuitComponent::Type),
         .metadata = {.displayName = slug,
                      .description = "Project subcircuit",
                      .category    = ComponentCategory::Subcircuits},
         .initialProperties = std::move(initialProperties)});
  }

  std::ranges::sort(catalogRows, [](const auto& lhs, const auto& rhs) {
    if (lhs.metadata.category != rhs.metadata.category)
      return std::to_underlying(lhs.metadata.category)
             < std::to_underlying(rhs.metadata.category);
    return lhs.metadata.displayName < rhs.metadata.displayName;
  });

  return catalogRows;
}

}  // namespace

ComponentCatalogOverlay::ComponentCatalogOverlay(DiagramScene* scene, QWidget* parent)
  : QWidget(parent), diagramScene(scene)
{
  setObjectName(QStringLiteral("componentCatalogOverlay"));
  setAutoFillBackground(true);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);

  searchInput = new QLineEdit(this);
  searchInput->setPlaceholderText(tr("Search components"));
  layout->addWidget(searchInput);

  table = new QTableWidget(this);
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels(
      {tr("Component shape"), tr("Component name"), tr("Component description")});
  table->verticalHeader()->hide();
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setShowGrid(false);
  table->setTextElideMode(Qt::ElideNone);
  table->horizontalHeader()->setStretchLastSection(true);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);

  layout->addWidget(table);

  auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(closeShortcut, &QShortcut::activated, this, &QWidget::hide);

  connect(searchInput, &QLineEdit::textChanged, this, [this] { rebuildRows(); });
  connect(table, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) { activateRow(row); });
  connect(table, &QTableWidget::itemActivated, this, [this](QTableWidgetItem* item) {
    if (item)
      activateRow(item->row());
  });

  catalogRows = componentCatalogRows(diagramScene && diagramScene->isSubcircuitDocumentMode());
  rebuildRows();
  hide();
}

void ComponentCatalogOverlay::open()
{
  catalogRows = componentCatalogRows(diagramScene && diagramScene->isSubcircuitDocumentMode());
  QSignalBlocker blocker(searchInput);
  searchInput->clear();
  blocker.unblock();
  rebuildRows();
  show();
  raise();
  searchInput->setFocus(Qt::OtherFocusReason);
  selectFirstComponentRow();
}

void ComponentCatalogOverlay::rebuildRows()
{
  table->clearContents();
  table->setRowCount(0);

  const QString query          = searchInput->text().trimmed();
  const bool    showCategories = query.isEmpty();

  std::vector<const CatalogRow*> visibleRows;
  visibleRows.reserve(catalogRows.size());

  if (showCategories) {
    for (const CatalogRow& row : catalogRows)
      visibleRows.push_back(&row);
  } else {
    QStringList         candidates;
    std::vector<size_t> candidateRows;
    candidates.reserve(static_cast<qsizetype>(catalogRows.size() * 4));
    candidateRows.reserve(catalogRows.size() * 4);

    for (size_t rowIndex = 0; rowIndex < catalogRows.size(); ++rowIndex) {
      for (const QString& field : searchableFields(catalogRows[rowIndex])) {
        candidates.append(field);
        candidateRows.push_back(rowIndex);
      }
    }

    std::vector<bool> selected(catalogRows.size(), false);
    for (const auto& match : ComponentSearchMatcher::rank(candidates, query, false)) {
      const size_t rowIndex = candidateRows[static_cast<size_t>(match.index)];
      if (selected[rowIndex])
        continue;

      visibleRows.push_back(&catalogRows[rowIndex]);
      selected[rowIndex] = true;
    }
  }

  std::optional<ComponentRegistry::ComponentCategory> currentCategory;
  for (const CatalogRow* row : visibleRows) {
    if (showCategories
        && (!currentCategory || *currentCategory != row->metadata.category)) {
      addCategoryRow(row->metadata.category);
      currentCategory = row->metadata.category;
    }
    addComponentRow(*row);
  }

  resizeCatalogColumns();
  selectFirstComponentRow();
}

void ComponentCatalogOverlay::addCategoryRow(
    ComponentRegistry::ComponentCategory category)
{
  const int row = table->rowCount();
  table->insertRow(row);
  table->setSpan(row, 0, 1, 3);
  table->setRowHeight(row, CatalogCategoryRowHeight);

  auto* item = new QTableWidgetItem(
      QString::fromStdString(std::string(componentCategoryName(category))));
  QFont font = item->font();
  font.setBold(true);
  item->setFont(font);
  item->setFlags(Qt::NoItemFlags);
  table->setItem(row, 0, item);
}

void ComponentCatalogOverlay::addComponentRow(const CatalogRow& rowData)
{
  const int row = table->rowCount();
  table->insertRow(row);

  QPixmap previewPixmap = componentPreviewPixmap(rowData);
  auto*   preview       = new QLabel(table);
  preview->setFixedSize(previewPixmap.size());
  preview->setAlignment(Qt::AlignCenter);
  preview->setPixmap(previewPixmap);
  table->setCellWidget(row, 0, preview);
  table->setRowHeight(row,
                      std::max(CatalogMinimumComponentRowHeight, previewPixmap.height()));

  auto* name = new QTableWidgetItem(QString::fromStdString(rowData.metadata.displayName));
  name->setData(Qt::UserRole, QString::fromStdString(rowData.guiType));
  name->setData(Qt::UserRole + 1,
                static_cast<int>(&rowData - catalogRows.data()));
  table->setItem(row, 1, name);

  auto* description =
      new QTableWidgetItem(QString::fromStdString(rowData.metadata.description));
  description->setData(Qt::UserRole, QString::fromStdString(rowData.guiType));
  description->setData(Qt::UserRole + 1,
                       static_cast<int>(&rowData - catalogRows.data()));
  table->setItem(row, 2, description);
}

void ComponentCatalogOverlay::activateRow(const int row)
{
  if (row < 0)
    return;

  QTableWidgetItem* item = table->item(row, 1);
  if (!item)
    item = table->item(row, 2);
  if (!item)
    return;

  const QString typeName = item->data(Qt::UserRole).toString();
  if (typeName.isEmpty())
    return;

  const int catalogIndex = item->data(Qt::UserRole + 1).toInt();
  const PropertyMap initialProperties =
      catalogIndex >= 0 && catalogIndex < static_cast<int>(catalogRows.size())
          ? catalogRows[static_cast<size_t>(catalogIndex)].initialProperties
          : PropertyMap{};

  if (diagramScene->getInteractionMode() != DiagramScene::InteractionMode::NORMAL_MODE)
    diagramScene->setInteractionMode(DiagramScene::InteractionMode::NORMAL_MODE);

  diagramScene->placeComponent(typeName.toStdString(), false, initialProperties);
  hide();

  if (!diagramScene->views().empty())
    diagramScene->views().first()->setFocus(Qt::OtherFocusReason);
}

void ComponentCatalogOverlay::selectFirstComponentRow()
{
  for (int row = 0; row < table->rowCount(); ++row) {
    const auto* item = table->item(row, 1);
    if (item && !item->data(Qt::UserRole).toString().isEmpty()) {
      table->selectRow(row);
      return;
    }
  }
}

void ComponentCatalogOverlay::resizeCatalogColumns()
{
  table->resizeColumnToContents(0);

  const QFontMetrics fontMetrics(table->font());
  int                nameColumnWidth = 0;
  if (const QTableWidgetItem* headerItem = table->horizontalHeaderItem(1))
    nameColumnWidth = fontMetrics.horizontalAdvance(headerItem->text());

  for (int row = 0; row < table->rowCount(); ++row) {
    const QTableWidgetItem* item = table->item(row, 1);
    if (!item)
      continue;

    nameColumnWidth = std::max(nameColumnWidth,
                               fontMetrics.horizontalAdvance(item->text()));
  }

  table->setColumnWidth(1, nameColumnWidth + CatalogNameColumnPadding);
}

QStringList ComponentCatalogOverlay::searchableFields(const CatalogRow& rowData)
{
  return {QString::fromStdString(rowData.metadata.displayName),
          QString::fromStdString(rowData.guiType),
          QString::fromStdString(rowData.metadata.description),
          QString::fromStdString(
              std::string(componentCategoryName(rowData.metadata.category)))};
}
