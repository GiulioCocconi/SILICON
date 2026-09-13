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

#include <core/component.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace SILICON::core {

class CircuitResolver;

class SubcircuitComponent : public Component {
public:
  static constexpr std::string_view Type = "Subcircuit";

  /** @param resolver Non-owning; must outlive this component when non-null. */
  explicit SubcircuitComponent(const CircuitResolver* resolver = nullptr);

  /**
   * @brief Creates an imported module instance before its project document exists.
   *
   * The supplied buses are authoritative for the transient imported circuit. Once the
   * circuit is serialized into a project document, normal registry-backed construction
   * is used on subsequent loads.
   */
  [[nodiscard]] static std::shared_ptr<SubcircuitComponent>
  imported(std::string slug, std::vector<std::string> inputNames, std::vector<Bus> inputs,
           std::vector<std::string> outputNames, std::vector<Bus> outputs);

  /** Port names retained while an imported module has no project document yet. */
  [[nodiscard]] const std::vector<std::string>& importedInputNames() const
  {
    return transientInputNames;
  }
  [[nodiscard]] const std::vector<std::string>& importedOutputNames() const
  {
    return transientOutputNames;
  }

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Subcircuit", "Project-local reusable circuit instance.",
            ComponentCategory::Subcircuits};
  }

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

  void reloadFromResolver();
  /** Sets a non-owning resolver that must outlive this component. */
  void setCircuitResolver(const CircuitResolver* resolver);
  [[nodiscard]] const CircuitResolver* circuitResolver() const noexcept;

private:
  const CircuitResolver*   resolver = nullptr;
  std::vector<std::string> transientInputNames;
  std::vector<std::string> transientOutputNames;

  void configureFromSlug(std::string_view slug);
  void clearResolvedCircuit();
};

}  // namespace SILICON::core
