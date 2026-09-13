/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include "projectCircuitResolver.hpp"

#include <format>
#include <stdexcept>

#include <core/activeKeyGuard.hpp>
#include <core/memory.hpp>
#include <core/projectContext.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::project {

ProjectCircuitResolver::ProjectCircuitResolver(
    const ProjectContext& project, const SILICON::core::ComponentRegistry& registry)
  : project(project), registry(registry)
{
}

SILICON::core::SubcircuitDefinition
ProjectCircuitResolver::resolve(const std::string_view slug) const
{
  SILICON::core::ActiveKeyGuard activeSlug(activeSlugs, std::string(slug),
                                           "Recursive subcircuit dependency detected: ");

  const auto  path     = documentPathForSlug(DocumentType::Circuit, slug);
  const auto* document = project.documents().find(path);
  if (!document)
    throw std::runtime_error(std::format("Unknown subcircuit slug '{}'", slug));

  auto definition =
      SILICON::core::parseCircuitDocument(document->getContents(), registry, this);
  for (const auto& [_component, vertex] : definition.circuit.getComponentToVertex()) {
    const auto rom = std::dynamic_pointer_cast<SILICON::core::ROM>(
        definition.circuit.getComponentByVertexId(vertex));
    if (!rom)
      continue;

    const auto binarySlug = rom->getPropertyValue<std::string>("binaryContents");
    rom->refreshBinaryContents(
        binarySlug ? binaryContentsSnapshot(project.documents(), *binarySlug) : nullptr);
  }
  return definition;
}

}  // namespace SILICON::project
