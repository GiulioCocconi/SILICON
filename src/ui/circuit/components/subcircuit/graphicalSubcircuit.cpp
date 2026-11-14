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

#include <algorithm>
#include <memory>
#include <utility>

#include <QScopedValueRollback>

#include <ui/circuit/components/subcircuit/utils.hpp>

namespace SILICON::ui {
  using namespace SILICON::core;

  namespace {

    [[nodiscard]] std::vector<PortPair>
    portPairs(const std::vector<GraphicalSubcircuitPortMetadata>& ports)
    {
      std::vector<PortPair> result;
      result.reserve(ports.size());
      for (const auto& port : ports)
        result.emplace_back(QString::fromStdString(port.name), port.position);
      return result;
    }

  }  // namespace

  GraphicalSubcircuitComponent::GraphicalSubcircuitComponent(
      QGraphicsItem* parent, const SILICON::project::DocumentStore* documents,
      const CircuitResolver* resolver)
    : GraphicalLogicComponent(
          std::make_shared<SubcircuitComponent>(resolver),
          new SubcircuitRectShape(GraphicalSubcircuitMetadata{}.widthHeight), parent,
          false),
      documents(documents)
  {
    printPortNames = true;

    subscribeToDocuments();
    refreshFromMetadata();
  }

  void GraphicalSubcircuitComponent::subscribeToDocuments()
  {
    if (!documents || registryListenerId != 0)
      return;
    registryListenerId =
        documents->addListener([this](const SILICON::project::DocumentChange& change) {
          const auto slug = currentSlug();
          const bool affectsConfiguredDocument =
              SILICON::project::isValidDocumentSlug(slug) && change.path
              && *change.path
                     == SILICON::project::documentPathForSlug(
                         SILICON::project::DocumentType::Circuit, slug);
          if (change.kind == SILICON::project::DocumentChangeKind::Reset
              || affectsConfiguredDocument) {
            {
              const QScopedValueRollback guard(refreshSuppressed, true);
              if (const auto subcircuit =
                      std::dynamic_pointer_cast<SubcircuitComponent>(associatedComponent))
                subcircuit->reloadFromResolver();
            }
            refreshFromMetadata();
          }
        });
  }

  void GraphicalSubcircuitComponent::unsubscribeFromDocuments()
  {
    if (documents && registryListenerId != 0)
      documents->removeListener(registryListenerId);
    registryListenerId = 0;
  }

  GraphicalSubcircuitComponent::GraphicalSubcircuitComponent(
      std::string slug, QGraphicsItem* parent,
      const SILICON::project::DocumentStore* documents, const CircuitResolver* resolver)
    : GraphicalSubcircuitComponent(parent, documents, resolver)
  {
    if (!slug.empty())
      applyProperty("slug", std::move(slug));
  }

  GraphicalSubcircuitComponent::~GraphicalSubcircuitComponent()
  {
    unsubscribeFromDocuments();
  }

  void GraphicalSubcircuitComponent::setComponent(const Component_ptr& component)
  {
    {
      const QScopedValueRollback guard(refreshSuppressed, true);
      GraphicalLogicComponent::setComponent(component);
    }
    refreshFromMetadata();
  }

  void GraphicalSubcircuitComponent::setProjectContext(
      const SILICON::project::DocumentStore* newDocuments,
      const CircuitResolver*                 newResolver)
  {
    {
      const QScopedValueRollback guard(refreshSuppressed, true);
      if (documents != newDocuments) {
        unsubscribeFromDocuments();
        documents = newDocuments;
        subscribeToDocuments();
      }
      if (const auto subcircuit =
              std::dynamic_pointer_cast<SubcircuitComponent>(associatedComponent))
        subcircuit->setCircuitResolver(newResolver);
    }
    refreshFromMetadata();
  }

  void GraphicalSubcircuitComponent::applyProperty(std::string_view     key,
                                                   const PropertyValue& value)
  {
    {
      const QScopedValueRollback guard(refreshSuppressed, true);
      GraphicalLogicComponent::applyProperty(key, value);
    }
    if (key == "slug")
      refreshFromMetadata();
  }

  std::string GraphicalSubcircuitComponent::currentSlug() const
  {
    if (!associatedComponent)
      return {};
    return associatedComponent->getPropertyValue<std::string>("slug").value_or(
        std::string());
  }

  void
  GraphicalSubcircuitComponent::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
  {
    const auto slug = currentSlug();
    if (SILICON::project::isValidDocumentSlug(slug)
        && openDocumentOnDoubleClick(event,
                                     SILICON::project::documentPathForSlug(
                                         SILICON::project::DocumentType::Circuit, slug)))
      return;

    GraphicalLogicComponent::mouseDoubleClickEvent(event);
  }

  void
  GraphicalSubcircuitComponent::applyMetadata(const GraphicalSubcircuitMetadata& metadata)
  {
    const QScopedValueRollback guard(refreshSuppressed, true);
    prepareGeometryChange();
    setItemShape(new SubcircuitRectShape(metadata.widthHeight));
    if (metadata.inputs.empty() && metadata.outputs.empty())
      clearPorts();
    else
      setPorts(portPairs(metadata.inputs), portPairs(metadata.outputs));
    GraphicalLogicComponent::updatePortSizes();
    update();
  }

  void GraphicalSubcircuitComponent::updatePortSizes()
  {
    if (refreshSuppressed || !associatedComponent)
      return;

    const auto& componentInputs  = associatedComponent->inputBuses();
    const auto& componentOutputs = associatedComponent->outputBuses();

    if (inputPorts.size() != componentInputs.size()
        || outputPorts.size() != componentOutputs.size()) {
      refreshFromMetadata();
      return;
    }

    GraphicalLogicComponent::updatePortSizes();
  }

  void GraphicalSubcircuitComponent::refreshFromMetadata()
  {
    if (refreshSuppressed)
      return;
    const QScopedValueRollback guard(refreshSuppressed, true);
    const auto                 slug = currentSlug();
    if (slug.empty() || !documents) {
      applyMetadata({});
      return;
    }

    const auto* document = documents->find(SILICON::project::documentPathForSlug(
        SILICON::project::DocumentType::Circuit, slug));
    if (!document) {
      useAttachedInterfaceMetadata();
      return;
    }

    const auto json = nlohmann::json::parse(document->getContents(), nullptr, false);
    const auto subcircuit =
        std::dynamic_pointer_cast<SubcircuitComponent>(associatedComponent);
    auto metadata = synchronizeGraphicalSubcircuitMetadata(
        json,
        parseGraphicalSubcircuitMetadata(json).value_or(GraphicalSubcircuitMetadata{}),
        subcircuit ? subcircuit->circuitResolver() : nullptr);

    if (associatedComponent) {
      const auto& componentInputs  = associatedComponent->inputBuses();
      const auto& componentOutputs = associatedComponent->outputBuses();

      if (metadata.inputs.size() != componentInputs.size())
        metadata.inputs =
            synchronizePortsWithBuses(metadata.inputs, componentInputs, metadata, true);
      if (metadata.outputs.size() != componentOutputs.size())
        metadata.outputs = synchronizePortsWithBuses(metadata.outputs, componentOutputs,
                                                     metadata, false);
    }

    applyMetadata(metadata);
  }

  void GraphicalSubcircuitComponent::useAttachedInterfaceMetadata()
  {
    GraphicalSubcircuitMetadata metadata;
    if (associatedComponent) {
      metadata.widthHeight =
          defaultGraphicalSubcircuitSize(associatedComponent->inputBuses().size(),
                                         associatedComponent->outputBuses().size());
      metadata.inputs  = synchronizePortsWithBuses({}, associatedComponent->inputBuses(),
                                                   metadata, true);
      metadata.outputs = synchronizePortsWithBuses({}, associatedComponent->outputBuses(),
                                                   metadata, false);

      if (const auto imported =
              std::dynamic_pointer_cast<SubcircuitComponent>(associatedComponent)) {
        const auto& inputNames = imported->importedInputNames();
        for (std::size_t i = 0; i < std::min(metadata.inputs.size(), inputNames.size());
             ++i)
          metadata.inputs[i].name = inputNames[i];
        const auto& outputNames = imported->importedOutputNames();
        for (std::size_t i = 0; i < std::min(metadata.outputs.size(), outputNames.size());
             ++i)
          metadata.outputs[i].name = outputNames[i];
      }
    }
    applyMetadata(metadata);
  }

}  // namespace SILICON::ui
