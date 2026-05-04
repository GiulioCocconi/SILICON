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

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class Component;
using Component_ptr = std::shared_ptr<Component>;

class ComponentRegistry {
public:
  using Factory = std::function<Component_ptr()>;

  ComponentRegistry();
  static ComponentRegistry  empty();
  static ComponentRegistry& instance();

  void                     registerType(std::string type, Factory factory);
  Component_ptr            create(std::string_view type) const;
  bool                     hasType(std::string_view type) const;
  std::vector<std::string> availableTypes() const;

private:
  std::map<std::string, Factory, std::less<>> types_;
};
