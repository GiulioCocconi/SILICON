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

#include <string_view>

#include <core/component.hpp>
#include <core/wire.hpp>

class Multiplexer : public Component {
public:
  static constexpr std::string_view Type = "Multiplexer";

  enum class Inputs : unsigned int {
    Data      = 0,
    Selection = 1,
  };

  enum class Outputs : unsigned int {
    Out = 0,
  };

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Multiplexer", "Selects one bit from a data bus using a selection bus.",
            ComponentCategory::Multiplexers};
  }

  Multiplexer();
  Multiplexer(Bus data, Bus selection, Wire_ptr output);

  int setSelectionSize(int selectionSize);
  int setBusSize(int busSize);

  void simulate(Simulator& sim) override;
  void serializeYosys(silicon::yosys::SerializationContext& context) const override;
};

class Demultiplexer : public Component {
public:
  static constexpr std::string_view Type = "Demultiplexer";

  enum class Inputs : unsigned int {
    Data      = 0,
    Selection = 1,
  };

  enum class Outputs : unsigned int {
    Out = 0,
  };

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Demultiplexer",
            "Routes one data bit to one output selected by a selection bus.",
            ComponentCategory::Multiplexers};
  }

  Demultiplexer();
  Demultiplexer(Bus data, Bus selection, Bus output);

  int setSelectionSize(int selectionSize);
  int setBusSize(int busSize);

  void simulate(Simulator& sim) override;
  void serializeYosys(silicon::yosys::SerializationContext& context) const override;
};

class Decoder : public Component {
public:
  static constexpr std::string_view Type = "Decoder";

  enum class Inputs : unsigned int {
    Enable    = 0,
    Selection = 1,
  };

  enum class Outputs : unsigned int {
    Out = 0,
  };

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Decoder", "Drives one output high from a binary selection bus when enabled.",
            ComponentCategory::Multiplexers};
  }

  Decoder();
  Decoder(Bus enable, Bus selection, Bus output);

  int setSelectionSize(int selectionSize);

  void simulate(Simulator& sim) override;
  void serializeYosys(silicon::yosys::SerializationContext& context) const override;
};
