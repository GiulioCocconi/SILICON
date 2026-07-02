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

#include <core/serialization/component_registry.hpp>

#include <core/component.hpp>
#include <core/flipflops.hpp>
#include <core/gates.hpp>
#include <extraComponents/arithmetic.hpp>
#include <extraComponents/utils.hpp>

template <typename T> inline void registerComponent(ComponentRegistry& registry)
{
  static_assert(HasType<T>,
                "T must have a static constexpr std::string_view Type member");
  registry.registerType(std::string(T::Type), [] { return std::make_shared<T>(); });
}

void registerAllComponents(ComponentRegistry& registry)
{
  registerComponent<AndGate>(registry);
  registerComponent<OrGate>(registry);
  registerComponent<NotGate>(registry);
  registerComponent<NandGate>(registry);
  registerComponent<NorGate>(registry);
  registerComponent<XorGate>(registry);

  registerComponent<DFlipFlop>(registry);
  registerComponent<EFlipFlop>(registry);
  registerComponent<JKFlipFlop>(registry);

  registerComponent<HalfAdder>(registry);
  registerComponent<FullAdder>(registry);
  registerComponent<AdderNBits>(registry);

  registerComponent<WireSplitter>(registry);
  registerComponent<WireMerger>(registry);
}
