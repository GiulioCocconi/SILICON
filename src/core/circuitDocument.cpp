/*
 Copyright (c) 2026. Giulio Cocconi
 ...
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

SubcircuitDefinition parseCircuitDocument(const std::string_view   contents,
                                          const ComponentRegistry& registry,
                                          const CircuitResolver*   resolver)
{
  auto document = nlohmann::json::parse(contents);
  if (const auto circuit = document.find("circuit"); circuit != document.end())
    document = *circuit;
  if (!document.is_object())
    throw std::runtime_error("Circuit document must contain a circuit object");

  auto circuit = Circuit::deserialize(document.dump(), registry, resolver);
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
