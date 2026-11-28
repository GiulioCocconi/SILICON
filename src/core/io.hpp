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

#include <cstdint>
#include <string>
#include <string_view>

#include <core/component.hpp>

namespace SILICON::core {

/** @brief Drives a one-bit constant used by imported hardware netlists. */
class ConstantComponent : public Component {
public:
  [[nodiscard]] std::vector<PortDescription> portDescriptions() const override;

  static constexpr std::string_view Type = "ConstantComponent";

  ConstantComponent();
  ConstantComponent(Wire_ptr output, BusValue value);
  ConstantComponent(Bus output, BusValue value);

  int      setSize(int newSize);
  BusValue setValue(const BusValue& value) const;

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {.displayName             = "Constant",
            .description             = "Drives a fixed binary or unknown logic value.",
            .category                = ComponentCategory::Inputs,
            .configurationProperties = {"size"}};
  }

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

class BoundaryIoComponent : public Component {
  uint64_t portIdentity;

protected:
  BoundaryIoComponent(std::vector<Bus> inputs, std::vector<Bus> outputs);

public:
  [[nodiscard]] std::vector<PortDescription> portDescriptions() const override;

  /** Persistent interface identity, independent of labels, wires and graph order. */
  [[nodiscard]] uint64_t           portId() const { return portIdentity; }
  void                             setPortId(uint64_t id);
  void                             resetPortId();
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

/** @brief Logical model behind the graphical single-bit input. */
class DummyInputComponent : public BoundaryIoComponent {
public:
  static constexpr std::string_view Type = "DummyInputComponent";

  DummyInputComponent();
  DummyInputComponent(Bus bus, std::string name);

  void setState(const State s)
  {
    (void)outputs[0].forceSetCurrentValue({s}, weak_from_this());
  }

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {.displayName             = "Input",
            .description             = "Provides a user-toggleable one-bit signal.",
            .category                = ComponentCategory::Inputs,
            .portRole                = PortRole::Input,
            .configurationProperties = {"portOrientation"}};
  }
  void simulate(SILICON::simulation::Simulator&) override {}
};

/** @brief Logical model behind the graphical bus input. */
class DummyBusInputComponent : public BoundaryIoComponent {
public:
  static constexpr std::string_view Type = "DummyBusInputComponent";

  DummyBusInputComponent();
  DummyBusInputComponent(Bus bus, std::string name);

  int  setSize(int newSize);
  void setBusValue(const BusValue& value)
  {
    (void)outputs[0].forceSetCurrentValue(value, weak_from_this());
  }

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {.displayName             = "Bus Input",
            .description             = "Provides an editable multi-bit bus value.",
            .category                = ComponentCategory::Inputs,
            .portRole                = PortRole::Input,
            .configurationProperties = {"size", "portOrientation"}};
  }
  void simulate(SILICON::simulation::Simulator&) override {}
};

/** @brief Logical model behind the graphical single-bit output. */
class DummyOutputComponent : public BoundaryIoComponent {
public:
  static constexpr std::string_view Type = "DummyOutputComponent";

  DummyOutputComponent();
  DummyOutputComponent(Bus bus, std::string name);

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {.displayName             = "Output",
            .description             = "Displays the current state of a one-bit signal.",
            .category                = ComponentCategory::Outputs,
            .portRole                = PortRole::Output,
            .configurationProperties = {"portOrientation"}};
  }
  void simulate(SILICON::simulation::Simulator&) override {}
};

/** @brief Logical model behind the graphical bus output. */
class DummyBusOutputComponent : public BoundaryIoComponent {
public:
  static constexpr std::string_view Type = "DummyBusOutputComponent";

  DummyBusOutputComponent();
  DummyBusOutputComponent(Bus bus, std::string name);

  int setSize(int newSize);

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {.displayName             = "Bus Output",
            .description             = "Displays the current value of a multi-bit bus.",
            .category                = ComponentCategory::Outputs,
            .portRole                = PortRole::Output,
            .configurationProperties = {"size", "portOrientation"}};
  }
  void simulate(SILICON::simulation::Simulator&) override {}
};

}  // namespace SILICON::core
