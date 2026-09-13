/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include "projectCircuitResolver.hpp"

#include <format>
#include <stdexcept>

#include <core/activeKeyGuard.hpp>
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

  return SILICON::core::parseCircuitDocument(document->getContents(), registry, this);
}

}  // namespace SILICON::project
