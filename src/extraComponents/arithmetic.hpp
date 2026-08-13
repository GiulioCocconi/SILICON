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

#include <array>
#include <string>
#include <string_view>

#include <core/component.hpp>
#include <core/wire.hpp>

namespace SILICON::extra {
using namespace SILICON::core;

/**
 * @class Extender
 * @brief Changes a bus width using signed or unsigned extension.
 */
class Extender : public Component {
public:
  static constexpr std::string_view Type         = "Extender";
  static constexpr std::string_view SignedMode   = "signed";
  static constexpr std::string_view UnsignedMode = "unsigned";

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Extender", "Changes a bus width using signed or unsigned extension.",
            ComponentCategory::Arithmetic};
  }

  Extender();
  Extender(Bus in, Bus out, std::string mode = std::string(UnsignedMode));

  int setInputSize(int width);
  int setOutputSize(int width);

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

/**
 * @class Complementer
 * @brief Gives the two's complement of a number
 */
class Complementer : public Component {
public:
  static constexpr std::string_view Type = "Complementer";

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Two's complement", "Gives the two's complement of a binary number",
            ComponentCategory::Arithmetic};
  }

  Complementer();
  Complementer(Bus in, Bus out);
  int setSize(int width);

  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

/**
 * @class HalfAdder
 * @brief One-bit half adder.
 *
 * Computes Sum = A xor B and Cout = A and B after the configured delay.
 * Input order: A, B. Output order: Sum, Cout.
 */
class HalfAdder : public Component {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "HalfAdder";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Half Adder", "Adds two one-bit inputs and outputs sum and carry.",
            ComponentCategory::Arithmetic};
  }

  /** @brief Input bus indices for HalfAdder. */
  enum class Inputs : unsigned int {
    /** @brief First addend bit. */
    A = 0,
    /** @brief Second addend bit. */
    B = 1,
  };

  /** @brief Output bus indices for HalfAdder. */
  enum class Outputs : unsigned int {
    /** @brief Sum bit. */
    Sum = 0,
    /** @brief Carry-out bit. */
    Cout = 1,
  };

  /** @brief Constructs an unconnected half adder. */
  HalfAdder();

  /**
   * @brief Constructs a connected half adder.
   * @param inputs Input wires ordered as A and B
   * @param sum Sum output
   * @param cout Carry-out output
   */
  HalfAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr sum, Wire_ptr cout);

  /**
   * @brief Evaluates the half-adder truth table.
   * @param sim SILICON::simulation::Simulator used to drive output wires
   */
  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

/**
 * @class FullAdder
 * @brief One-bit full adder.
 *
 * Computes a one-bit sum and carry from A, B, and Cin after the configured delay.
 * Input order: A, B, Cin. Output order: Sum, Cout.
 */
class FullAdder : public Component {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "FullAdder";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Full Adder",
            "Adds two one-bit inputs plus carry-in and outputs sum and carry.",
            ComponentCategory::Arithmetic};
  }

  /** @brief Input bus indices for FullAdder. */
  enum class Inputs : unsigned int {
    /** @brief First addend bit. */
    A = 0,
    /** @brief Second addend bit. */
    B = 1,
    /** @brief Carry-in bit. */
    Cin = 2,
  };

  /** @brief Output bus indices for FullAdder. */
  enum class Outputs : unsigned int {
    /** @brief Sum bit. */
    Sum = 0,
    /** @brief Carry-out bit. */
    Cout = 1,
  };

  /** @brief Constructs an unconnected full adder. */
  FullAdder();

  /**
   * @brief Constructs a connected full adder.
   * @param inputs Input wires ordered as A and B
   * @param cin Carry-in input
   * @param sum Sum output
   * @param cout Carry-out output
   */
  FullAdder(std::array<Wire_ptr, 2> inputs, Wire_ptr cin, Wire_ptr sum, Wire_ptr cout);

  /**
   * @brief Evaluates the full-adder truth table.
   * @param sim SILICON::simulation::Simulator used to drive output wires
   */
  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;
};

/**
 * @class AdderNBits
 * @brief Fixed-width unsigned ripple adder.
 *
 * Adds two equally sized input buses, drives a same-width sum bus, and emits a
 * one-bit carry-out after the configured delay.
 * Input order: A, B. Output order: Sum, Cout.
 */
class AdderNBits : public Component {
public:
  /** @brief Serialization/type-registry identifier. */
  static constexpr std::string_view Type = "AdderNBits";

  /**
   * @brief Returns the component type name.
   * @return Type identifier
   */
  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"N-bit Adder", "Adds two fixed-width buses and outputs sum and carry.",
            ComponentCategory::Arithmetic};
  }

  /** @brief Input bus indices for AdderNBits. */
  enum class Inputs : unsigned int {
    /** @brief First addend bus, least-significant bit first. */
    A = 0,
    /** @brief Second addend bus, least-significant bit first. */
    B = 1,
  };

  /** @brief Output bus indices for AdderNBits. */
  enum class Outputs : unsigned int {
    /** @brief Sum bus, least-significant bit first. */
    Sum = 0,
    /** @brief Carry-out bit. */
    Cout = 1,
  };

  /** @brief Constructs an unconnected N-bit adder. */
  AdderNBits();

  /**
   * @brief Constructs a connected N-bit adder.
   * @param inputs Input buses ordered as A and B
   * @param sum Sum output bus
   * @param cout Carry-out output
   */
  AdderNBits(std::array<Bus, 2> inputs, Bus sum, Wire_ptr cout);

  /**
   * @brief Evaluates the N-bit adder.
   * @param sim SILICON::simulation::Simulator used to drive output wires
   */
  void simulate(SILICON::simulation::Simulator& sim) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

  /**
   * @brief Resizes both inputs, the sum output, and the carry output.
   * @param width New adder width in bits
   * @return Applied width, or the current configured size when width is invalid
   */
  int setSize(int width);
};

}  // namespace SILICON::extra
