/*
  Copyright (C) 2026 Giulio Cocconi

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

#include <core/register.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

class GraphicalRegister : public GraphicalLogicComponent {
  Q_OBJECT
private:
  void setupCallbacks();
  void updateLayout();
  void updateLayout(const std::string& inputType, const std::string& outputType);
  int  applySize(int size);
  std::string applyInputType(const std::string& inputType);
  std::string applyOutputType(const std::string& outputType);

  [[nodiscard]] Register* getComponentAsRegister() const;

public:
  explicit GraphicalRegister(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::REGISTER; }

  void setComponent(const Component_ptr& component) override;
};

}  // namespace ui
}  // namespace SILICON
