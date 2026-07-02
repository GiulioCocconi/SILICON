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

#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

ComponentRegistry::ComponentRegistry() = default;

ComponentRegistry ComponentRegistry::empty()
{
  return ComponentRegistry{};
}

ComponentRegistry& ComponentRegistry::instance()
{
  static ComponentRegistry registry;
  return registry;
}

namespace {

bool isValidMetadata(const ComponentMetadata& metadata)
{
  return !metadata.displayName.empty() && !metadata.description.empty();
}

}  // namespace

void ComponentRegistry::registerType(std::string type, Factory factory)
{
  if (!factory)
    throw std::invalid_argument("Component registration requires a factory");

  const ComponentMetadata metadata = factory()->metadata();
  if (!isValidMetadata(metadata))
    throw std::invalid_argument("Component registration requires metadata");

  auto [it, inserted] = types_.emplace(std::move(type), Entry{std::move(factory)});
  if (!inserted) {
    throw std::logic_error(std::string("Duplicate component registration: ") + it->first);
  }
}

Component_ptr ComponentRegistry::create(std::string_view type) const
{
  auto it = types_.find(type);
  if (it == types_.end()) {
    throw std::runtime_error(std::string("Unknown component type: ") + std::string{type});
  }
  return it->second.factory();
}

bool ComponentRegistry::hasType(std::string_view type) const
{
  return types_.contains(type);
}

ComponentRegistry::ComponentMetadata ComponentRegistry::metadata(std::string_view type) const
{
  auto it = types_.find(type);
  if (it == types_.end()) {
    throw std::runtime_error(std::string("Unknown component type: ") + std::string{type});
  }
  return it->second.factory()->metadata();
}

std::vector<std::string> ComponentRegistry::availableTypes() const
{
  std::vector<std::string> types;
  types.reserve(types_.size());
  for (const auto& [type, _] : types_) {
    types.push_back(type);
  }
  return types;
}

std::string_view componentCategoryName(const ComponentCategory category)
{
  using Category = ComponentCategory;
  switch (category) {
    case Category::Gates: return "Gates";
    case Category::Multiplexers: return "Multiplexers";
    case Category::Arithmetic: return "Arithmetic";
    case Category::FlipFlops: return "Flip Flops";
    case Category::Register: return "Register";
    case Category::Inputs: return "Inputs";
    case Category::Outputs: return "Outputs";
    case Category::Utils: return "Utils";
  }
  std::unreachable();
}
