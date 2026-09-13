/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <core/circuitDocument.hpp>

namespace SILICON::project {
class ProjectContext;

/**
 * Resolves reusable Circuit documents from one explicit project context and
 * component registry. Both dependencies must outlive this resolver.
 */
class ProjectCircuitResolver final : public SILICON::core::CircuitResolver {
public:
  ProjectCircuitResolver(const ProjectContext&                   project,
                         const SILICON::core::ComponentRegistry& registry);

  [[nodiscard]] SILICON::core::SubcircuitDefinition
  resolve(std::string_view slug) const override;

private:
  const ProjectContext&                   project;
  const SILICON::core::ComponentRegistry& registry;
  mutable std::vector<std::string>        activeSlugs;
};

}  // namespace SILICON::project
