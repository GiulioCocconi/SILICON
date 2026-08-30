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

/** Resolves reusable Circuit documents from one explicit project context. */
class ProjectCircuitResolver final : public SILICON::core::CircuitResolver {
public:
  explicit ProjectCircuitResolver(const ProjectContext& project);

  [[nodiscard]] SILICON::core::SubcircuitDefinition
  resolve(std::string_view slug) const override;

private:
  const ProjectContext&         project;
  mutable std::vector<std::string> activeSlugs;
};

}  // namespace SILICON::project
