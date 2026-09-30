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
