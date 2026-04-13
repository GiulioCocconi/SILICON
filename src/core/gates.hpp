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
#include <array>
#include <memory>
#include <string>
#include <utility>

#include <core/component.hpp>
#include <core/wire.hpp>

class Gate : public Component {
public:
  Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  Gate() = default;
};

class AndGate : public Gate {
public:
  static constexpr std::string_view Type = "AndGate";
  std::string_view typeName() const override { return Type; }

  AndGate() = default;
  AndGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class OrGate : public Gate {
public:
  static constexpr std::string_view Type = "OrGate";
  std::string_view typeName() const override { return Type; }

  OrGate() = default;
  OrGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NotGate : public Gate {
public:
  static constexpr std::string_view Type = "NotGate";
  std::string_view typeName() const override { return Type; }

  NotGate() = default;
  NotGate(Wire_ptr input, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NandGate : public Gate {
public:
  static constexpr std::string_view Type = "NandGate";
  std::string_view typeName() const override { return Type; }

  NandGate() = default;
  NandGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NorGate : public Gate {
public:
  static constexpr std::string_view Type = "NorGate";
  std::string_view typeName() const override { return Type; }

  NorGate() = default;
  NorGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class XorGate : public Gate {
public:
  static constexpr std::string_view Type = "XorGate";
  std::string_view typeName() const override { return Type; }

  XorGate() = default;
  XorGate(const std::array<Wire_ptr, 2>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};
