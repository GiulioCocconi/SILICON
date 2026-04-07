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

#include <QGraphicsItem>

#include <memory>
#include <ui/common/graphicalComponent.hpp>

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class GraphicalComponent;

class GUIComponentFactory {
public:
  using Factory = std::function<std::unique_ptr<GraphicalComponent>(QGraphicsItem*)>;

  GUIComponentFactory();
  static GUIComponentFactory empty();

  static GUIComponentFactory& instance();

  void                                registerType(std::string type, Factory factory);
  std::unique_ptr<GraphicalComponent> create(std::string_view type,
                                             QGraphicsItem*   parent = nullptr) const;
  std::vector<std::string>            availableTypes() const;
  bool                                hasType(std::string_view type) const;

private:
  std::map<std::string, Factory, std::less<>> factories_;
};
