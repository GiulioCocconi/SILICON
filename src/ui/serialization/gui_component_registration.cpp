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

#include <ui/serialization/gui_component_registration.hpp>

#include <core/flipflops.hpp>
#include <core/gates.hpp>
#include <extraComponents/arithmetic.hpp>
#include <extraComponents/utils.hpp>
#include <ui/logiFlow/components/graphicalArithmetic.hpp>
#include <ui/logiFlow/components/graphicalFlipFlops.hpp>
#include <ui/logiFlow/components/graphicalGates.hpp>
#include <ui/logiFlow/components/graphicalIO.hpp>
#include <ui/logiFlow/components/graphicalUtils.hpp>

#include <utility>

void registerAllGUIComponents(GUIComponentFactory& factory)
{
  auto reg = [&factory](std::string name, auto factoryFunc) {
    factory.registerType(std::move(name), std::move(factoryFunc));
  };
  auto regGuiOnly = [&factory](std::string name, auto factoryFunc) {
    factory.registerType(std::move(name), std::move(factoryFunc), {.coreType = {}});
  };

  reg(std::string(AndGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalAnd>(p); });
  reg(std::string(OrGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalOr>(p); });
  reg(std::string(NotGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalNot>(p); });
  reg(std::string(NandGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalNand>(p); });
  reg(std::string(NorGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalNor>(p); });
  reg(std::string(XorGate::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalXor>(p); });
  reg(std::string(DFlipFlop::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalDFlipFlop>(p); });
  reg(std::string(EFlipFlop::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalEFlipFlop>(p); });
  reg(std::string(JKFlipFlop::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalJKFlipFlop>(p); });

  regGuiOnly(std::string(GraphicalInput::ComponentType),
             [](QGraphicsItem* p) { return std::make_unique<GraphicalInput>(p); });
  regGuiOnly(std::string(GraphicalOutputSingle::ComponentType),
             [](QGraphicsItem* p) {
               return std::make_unique<GraphicalOutputSingle>(p);
             });
  regGuiOnly(std::string(GraphicalBusInput::ComponentType),
             [](QGraphicsItem* p) { return std::make_unique<GraphicalBusInput>(p); });
  regGuiOnly(std::string(GraphicalBusOutput::ComponentType),
             [](QGraphicsItem* p) { return std::make_unique<GraphicalBusOutput>(p); });

  reg(std::string(WireSplitter::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalWireSplitter>(p); });
  reg(std::string(WireMerger::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalWireMerger>(p); });

  reg(std::string(HalfAdder::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalHalfAdder>(p); });
  reg(std::string(FullAdder::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalFullAdder>(p); });
  reg(std::string(AdderNBits::Type),
      [](QGraphicsItem* p) { return std::make_unique<GraphicalAdderNBits>(p); });
}
