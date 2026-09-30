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

#include "circuitDocument.hpp"

#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::core {

Circuit deserializeCircuitDocument(const std::string_view   contents,
                                   const ComponentRegistry& registry,
                                   const CircuitResolver*   resolver)
{
  auto document = nlohmann::json::parse(contents);
  if (const auto circuit = document.find("circuit"); circuit != document.end())
    document = *circuit;
  if (!document.is_object())
    throw std::runtime_error("Circuit document must contain a circuit object");

  return Circuit::deserialize(document.dump(), registry, resolver);
}

SubcircuitDefinition parseCircuitDocument(const std::string_view   contents,
                                          const ComponentRegistry& registry,
                                          const CircuitResolver*   resolver)
{
  auto circuit = deserializeCircuitDocument(contents, registry, resolver);
  auto inputs  = circuit.getInputPorts();
  auto outputs = circuit.getOutputPorts();

  std::vector<Component_ptr> boundaryComponents;
  for (const auto& [component, _vertex] : circuit.getComponentToVertex()) {
    if (component && component->metadata().portRole != PortRole::None)
      boundaryComponents.push_back(circuit.getComponentByVertexId(_vertex));
  }
  for (const auto& component : boundaryComponents)
    circuit.removeComponent(component);

  return {.circuit = std::move(circuit),
          .inputs  = std::move(inputs),
          .outputs = std::move(outputs)};
}

}  // namespace SILICON::core
