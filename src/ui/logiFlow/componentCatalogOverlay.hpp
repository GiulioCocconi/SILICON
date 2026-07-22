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

#pragma once

#include <string>
#include <vector>

#include <QStringList>
#include <QWidget>

#include <core/serialization/component_registry.hpp>
#include <core/component.hpp>

class DiagramScene;
class QLineEdit;
class QTableWidget;

class ComponentCatalogOverlay : public QWidget {
public:
  struct CatalogRow {
    std::string                          guiType;
    ComponentRegistry::ComponentMetadata metadata;
    PropertyMap                          initialProperties;
  };

  explicit ComponentCatalogOverlay(DiagramScene* scene, QWidget* parent = nullptr);

  void open();

private:
  void rebuildRows();
  void addCategoryRow(ComponentRegistry::ComponentCategory category);
  void addComponentRow(const CatalogRow& rowData);
  void activateRow(int row);
  void selectFirstComponentRow();
  void resizeCatalogColumns();
  [[nodiscard]] static QStringList searchableFields(const CatalogRow& rowData);

  DiagramScene*           diagramScene = nullptr;
  QLineEdit*              searchInput  = nullptr;
  QTableWidget*           table        = nullptr;
  std::vector<CatalogRow> catalogRows;
};
