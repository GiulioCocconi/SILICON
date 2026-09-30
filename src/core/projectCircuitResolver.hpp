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
