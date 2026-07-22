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

#include "subcircuitDefinition.hpp"

#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/serialization/component_registry.hpp>
#include <core/activeKeyGuard.hpp>
#include <core/projectDocument.hpp>

namespace {

}  // namespace

namespace silicon::subcircuits {

std::string extractCoreCircuitJson(std::string_view sceneJson)
{
  auto json = nlohmann::json::parse(sceneJson);
  if (json.contains("circuit"))
    json = json["circuit"];
  if (!json.is_object())
    throw std::runtime_error("Subcircuit document must contain a circuit object");
  return json.dump();
}

SubcircuitDefinition loadSubcircuitDefinition(const std::string_view slug,
                                              const ComponentRegistry& registry)
{
  static thread_local std::vector<std::string> activeResolutionSlugs;
  ActiveKeyGuard activeSlug(activeResolutionSlugs, std::string(slug),
                           "Recursive subcircuit dependency detected: ");

  const auto path = silicon::project::subcircuitPathForSlug(slug);
  const auto* document = silicon::project::DocumentStore::active().find(path);
  if (!document)
    throw std::runtime_error(std::format("Unknown subcircuit slug '{}'", slug));

  const auto coreJson =
      document->coreCircuitJson().value_or(extractCoreCircuitJson(document->sceneJson()));
  auto circuit = Circuit::deserialize(coreJson, registry);
  auto inputs  = circuit.getInputs();
  auto outputs = circuit.getOutputs();

  return {.circuit = std::move(circuit),
          .inputs = std::move(inputs),
          .outputs = std::move(outputs)};
}

}  // namespace silicon::subcircuits
