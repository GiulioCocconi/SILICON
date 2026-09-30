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

#include <string_view>
#include <vector>

#include <core/circuit.hpp>

namespace SILICON::core {

class ComponentRegistry;

/** Semantic reusable-circuit implementation and interface. */
struct SubcircuitDefinition {
  Circuit                  circuit;
  std::vector<CircuitPort> inputs;
  std::vector<CircuitPort> outputs;
};

/**
 * Explicit source of project-local reusable Circuit definitions.
 *
 * Resolver pointers passed into circuits and components are non-owning. The
 * resolver must outlive every circuit, component, elaborator, simulation
 * session, or serialization operation that retains or uses that pointer.
 */
class CircuitResolver {
public:
  virtual ~CircuitResolver() = default;

  [[nodiscard]] virtual SubcircuitDefinition resolve(std::string_view slug) const = 0;
};

/**
 * Parses persisted Circuit document contents while preserving every component,
 * including the boundary I/O components used by document-level serializers.
 */
[[nodiscard]] Circuit
deserializeCircuitDocument(std::string_view contents, const ComponentRegistry& registry,
                           const CircuitResolver* resolver = nullptr);

/**
 * Parses persisted Circuit document contents without constructing graphical objects.
 * Boundary I/O components define the reusable interface and are removed from the
 * returned implementation circuit.
 */
[[nodiscard]] SubcircuitDefinition
parseCircuitDocument(std::string_view contents, const ComponentRegistry& registry,
                     const CircuitResolver* resolver = nullptr);

}  // namespace SILICON::core
