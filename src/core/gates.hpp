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
#include <utility>

#include <core/component.hpp>
#include <core/wire.hpp>

/**
 * @brief Base class for combinational logic gates.
 *
 * All gates expose a `delay` property. Gates other than `NotGate` also support
 * optional bitwise operation through the `bitwise` and `size` properties:
 * when `bitwise` is enabled, every input and the single output are resized to
 * `size` bits and the gate logic is applied independently on each bit.
 */
class Gate : public Component {
private:
  void initializeProperties(bool enableBitwiseProperties);

public:
  /**
   * @brief Constructs a gate with scalar I/O and bitwise properties enabled.
   * @param inputs Input wires, one per input port
   * @param output Output wire
   */
  Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);

  /**
   * @brief Constructs an empty gate instance for factory/serialization usage.
   */
  Gate();

protected:
  /**
   * @brief Constructs an empty gate and optionally enables bitwise properties.
   * @param enableBitwiseProperties False for scalar-only gates such as `NotGate`
   */
  explicit Gate(bool enableBitwiseProperties);

  /**
   * @brief Constructs a gate with I/O and optional bitwise property support.
   * @param inputs Input wires, one per input port
   * @param output Output wire
   * @param enableBitwiseProperties False for scalar-only gates such as `NotGate`
   */
  Gate(const std::vector<Wire_ptr>& inputs, Wire_ptr output,
       bool enableBitwiseProperties);

  /**
   * @brief Resizes every input bus and the output bus to the same width.
   * @param width New bus width to apply consistently across the gate I/O
   */
  int setSize(int width);
};

class AndGate : public Gate {
public:
  static constexpr std::string_view Type = "AndGate";
  std::string_view                  typeName() const override { return Type; }

  AndGate() = default;
  AndGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class OrGate : public Gate {
public:
  static constexpr std::string_view Type = "OrGate";
  std::string_view                  typeName() const override { return Type; }

  OrGate() = default;
  OrGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NotGate : public Gate {
public:
  static constexpr std::string_view Type = "NotGate";
  std::string_view                  typeName() const override { return Type; }

  NotGate() = default;
  NotGate(Wire_ptr input, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NandGate : public Gate {
public:
  static constexpr std::string_view Type = "NandGate";
  std::string_view                  typeName() const override { return Type; }

  NandGate() = default;
  NandGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class NorGate : public Gate {
public:
  static constexpr std::string_view Type = "NorGate";
  std::string_view                  typeName() const override { return Type; }

  NorGate() = default;
  NorGate(const std::vector<Wire_ptr>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};

class XorGate : public Gate {
public:
  static constexpr std::string_view Type = "XorGate";
  std::string_view                  typeName() const override { return Type; }

  XorGate() = default;
  XorGate(const std::array<Wire_ptr, 2>& inputs, Wire_ptr output);
  void simulate(Simulator& sim) override;
};
