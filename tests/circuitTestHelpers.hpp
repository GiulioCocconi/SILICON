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

#include <algorithm>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include <boost/graph/graph_traits.hpp>

#include <core/circuit.hpp>
#include <core/component.hpp>

using namespace SILICON::core;

inline std::vector<Component_ptr> componentsIn(const Circuit& circuit)
{
  std::vector<Component_ptr> components;
  const auto& graph = circuit.getGraph();
  components.reserve(boost::num_vertices(graph));
  for (const auto vertex : boost::make_iterator_range(boost::vertices(graph))) {
    if (auto component = graph[vertex].component)
      components.push_back(component);
  }
  return components;
}

inline std::multiset<std::string> componentTypes(const Circuit& circuit)
{
  auto components = componentsIn(circuit);
  std::multiset<std::string> result;
  std::ranges::transform(components, std::inserter(result, result.end()),
                         [](const Component_ptr& component) {
                           return std::string(component->typeName());
                         });
  return result;
}
