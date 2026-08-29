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

#include "graphicalSubcircuit.hpp"

#include <core/projectDocument.hpp>

#include <memory>
#include <utility>

#include <ui/logiFlow/components/subcircuit/utils.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

[[nodiscard]] PortPair portPair(const GraphicalSubcircuitPortMetadata& metadata)
{
  return {QString::fromStdString(metadata.name), metadata.position};
}

[[nodiscard]] std::vector<PortPair>
portPairs(const std::vector<GraphicalSubcircuitPortMetadata>& ports)
{
  std::vector<PortPair> result;
  result.reserve(ports.size());
  for (const auto& port : ports)
    result.push_back(portPair(port));
  return result;
}

}  // namespace

GraphicalSubcircuitComponent::GraphicalSubcircuitComponent(QGraphicsItem* parent)
  : GraphicalLogicComponent(
        std::make_shared<SubcircuitComponent>(),
        new SubcircuitRectShape(QSize(
            GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE,
            GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE)),
        parent, false)
{
  printPortNames = true;

  registryListenerId =
      SILICON::project::DocumentStore::active().addListener(
          [this](const SILICON::project::DocumentChange& change) {
        const auto slug = currentSlug();
        const bool affectsConfiguredDocument =
            SILICON::project::isValidSubcircuitSlug(slug) && change.path
            && *change.path == SILICON::project::subcircuitPathForSlug(slug);
        if (change.kind == SILICON::project::DocumentChangeKind::Reset
            || affectsConfiguredDocument)
          refreshFromMetadata();
      });
  refreshFromMetadata();
}

GraphicalSubcircuitComponent::GraphicalSubcircuitComponent(std::string slug,
                                                           QGraphicsItem* parent)
  : GraphicalSubcircuitComponent(parent)
{
  if (!slug.empty())
    applyProperty("slug", std::move(slug));
}

GraphicalSubcircuitComponent::~GraphicalSubcircuitComponent()
{
  if (registryListenerId != 0)
    SILICON::project::DocumentStore::active().removeListener(registryListenerId);
}

void GraphicalSubcircuitComponent::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  refreshFromMetadata();
}

void GraphicalSubcircuitComponent::applyProperty(std::string_view     key,
                                                 const PropertyValue& value)
{
  GraphicalLogicComponent::applyProperty(key, value);
  if (key == "slug")
    refreshFromMetadata();
}

std::string GraphicalSubcircuitComponent::currentSlug() const
{
  if (!associatedComponent)
    return {};
  return associatedComponent->getPropertyValue<std::string>(
                                "slug")
      .value_or(std::string());
}

void GraphicalSubcircuitComponent::applyMetadata(
    const GraphicalSubcircuitMetadata& metadata)
{
  prepareGeometryChange();
  setItemShape(new SubcircuitRectShape(metadata.widthHeight));
  setPorts(portPairs(metadata.inputs), portPairs(metadata.outputs));
  update();
}

void GraphicalSubcircuitComponent::updatePortSizes()
{
  if (!associatedComponent)
    return;

  const auto componentInputs  = associatedComponent->getInputs();
  const auto componentOutputs = associatedComponent->getOutputs();

  if (inputPorts.size() != componentInputs.size()
      || outputPorts.size() != componentOutputs.size()) {
    refreshFromMetadata();
    return;
  }

  GraphicalLogicComponent::updatePortSizes();
}

void GraphicalSubcircuitComponent::refreshFromMetadata()
{
  auto applyEmptyMetadata = [this] {
    prepareGeometryChange();
    setItemShape(new SubcircuitRectShape(
        QSize(GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE,
              GraphicalSubcircuitDefaultSize * DiagramScene::GRID_SIZE)));
    clearPorts();
    update();
  };

  const auto slug = currentSlug();
  if (slug.empty()) {
    applyEmptyMetadata();
    return;
  }

  const auto* document = SILICON::project::DocumentStore::active().find(
      SILICON::project::subcircuitPathForSlug(slug));
  if (!document) {
    applyEmptyMetadata();
    return;
  }

  auto metadata = synchronizeGraphicalSubcircuitMetadata(
      document->getContents(),
      parseGraphicalSubcircuitMetadata(document->getContents())
          .value_or(GraphicalSubcircuitMetadata{}));

  if (associatedComponent) {
    const auto componentInputs  = associatedComponent->getInputs();
    const auto componentOutputs = associatedComponent->getOutputs();

    if (metadata.inputs.size() != componentInputs.size())
      metadata.inputs = synchronizePortsWithBuses(metadata.inputs, componentInputs,
                                                  metadata, true);
    if (metadata.outputs.size() != componentOutputs.size())
      metadata.outputs = synchronizePortsWithBuses(metadata.outputs, componentOutputs,
                                                   metadata, false);
  }

  try {
    applyMetadata(metadata);
  } catch (const std::exception&) {
  }
}

}  // namespace ui
}  // namespace SILICON
