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

#include <core/circuit.hpp>

#include <string>
#include <string_view>
#include <vector>

class ComponentRegistry;

namespace silicon::subcircuits {

/** @brief Core-circuit implementation and interface of a project subcircuit. */
struct SubcircuitDefinition {
  Circuit          circuit;
  std::vector<Bus> inputs;
  std::vector<Bus> outputs;
};

[[nodiscard]] std::string extractCoreCircuitJson(std::string_view sceneJson);

[[nodiscard]] SubcircuitDefinition
loadSubcircuitDefinition(std::string_view slug, const ComponentRegistry& registry);

}  // namespace silicon::subcircuits
