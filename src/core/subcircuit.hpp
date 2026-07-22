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
#include <string_view>

class Simulator;

class SubcircuitComponent : public Component {
public:
  static constexpr std::string_view Type = "Subcircuit";

  SubcircuitComponent();
  ~SubcircuitComponent() override;

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Subcircuit", "Project-local reusable circuit instance.",
            ComponentCategory::Subcircuits};
  }

  void simulate(Simulator& sim) override;

  void reloadFromRegistry();

private:
  std::uint64_t registryListenerId = 0;

  void configureFromSlug(std::string_view slug);
  void clearResolvedCircuit();
};
