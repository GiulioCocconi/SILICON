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

#include <ui/serialization/gui_component_factory.hpp>

GUIComponentFactory::GUIComponentFactory() = default;

GUIComponentFactory GUIComponentFactory::empty()
{
  return GUIComponentFactory{};
}

GUIComponentFactory& GUIComponentFactory::instance()
{
  static GUIComponentFactory factory;
  return factory;
}

void GUIComponentFactory::registerType(std::string type, Factory factory)
{
  auto [it, inserted] = factories_.emplace(std::move(type), std::move(factory));
  if (!inserted) {
    throw std::logic_error(std::string("Duplicate GUI component registration: ")
                           + it->first);
  }
}

std::unique_ptr<GraphicalComponent>
GUIComponentFactory::create(std::string_view type, QGraphicsItem* parent) const
{
  auto it = factories_.find(type);
  if (it == factories_.end()) {
    throw std::runtime_error(std::string("Unknown GUI component type: ")
                             + std::string{type});
  }
  return it->second(parent);
}

std::vector<std::string> GUIComponentFactory::availableTypes() const
{
  std::vector<std::string> types;
  types.reserve(factories_.size());
  for (const auto& [type, _] : factories_) {
    types.push_back(type);
  }
  return types;
}

bool GUIComponentFactory::hasType(std::string_view type) const
{
  return factories_.contains(type);
}
