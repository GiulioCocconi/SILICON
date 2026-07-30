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

#include <memory>

#include <core/circuit.hpp>

namespace SILICON::core {
class ComponentRegistry;
}

namespace SILICON::simulation {

using namespace SILICON::core;

namespace core = SILICON::core;

/**
 * @brief Converts source designs into simulator-ready runtime circuits.
 *
 * @details Runtime elaboration is the boundary between editing/serialization and
 * simulation:
 *
 * 1. The source Circuit is left structurally unchanged and may still contain
 *    placeholder components such as SubcircuitComponent.
 * 2. Primitive components are cloned into a new runtime circuit. Root-level wires are
 *    reused so existing UI/test buses can drive and observe simulation state.
 * 3. SubcircuitComponent instances are replaced by their project subcircuit's core
 *    Circuit. The subcircuit interface wires are mapped to the parent instance buses,
 *    while internal wires are cloned so repeated instances are independent.
 * 4. Nested subcircuits are elaborated recursively with active-slug cycle detection.
 * 5. The returned circuit is the only circuit that should be passed to Simulator.
 *
 * This keeps saved project JSON and UI-facing component structure unchanged while
 * ensuring simulation runs through one runtime circuit, one event queue, and one clock.
 */
class CircuitElaborator {
public:
  /**
   * @brief Creates an elaborator using the given component registry for cloning and
   * deserialization.
   */
  explicit CircuitElaborator(const core::ComponentRegistry& registry);

  /**
   * @brief Builds a simulator-ready runtime circuit from a source design.
   *
   * @param sourceCircuit Editable/source circuit to elaborate.
   * @return New runtime circuit containing simulator-executable components.
   * @throws std::runtime_error if a component type, subcircuit target, or recursive
   * subcircuit dependency cannot be resolved.
   */
  [[nodiscard]] std::shared_ptr<Circuit> elaborate(const Circuit& sourceCircuit) const;

private:
  const core::ComponentRegistry& registry;
};

}  // namespace SILICON::simulation
