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
#include <vector>

#include <core/component.hpp>
#include <core/simulator.hpp>
#include <core/wire.hpp>

namespace SILICON::core {

class Register : public Component {
public:
  static constexpr std::string_view Type         = "Register";
  static constexpr std::string_view ParallelType = "Parallel";
  static constexpr std::string_view SerialType   = "Serial";

  enum class Inputs : unsigned int {
    Data   = 0,
    Clock  = 1,
    Enable = 2,
    Clear  = 3,
    Load   = 4,
  };

  enum class Outputs : unsigned int {
    Out = 0,
  };

  Register();
  Register(Bus data, Wire_ptr clock, Wire_ptr enable, Wire_ptr clear, Bus output);

  std::string_view  typeName() const override { return Type; }
  ComponentMetadata metadata() const override
  {
    return {"Register", "Stores and shifts data while enabled on a positive clock edge.",
            ComponentCategory::Register};
  }

  [[nodiscard]] bool usesStagedSequentialOutputs() const override { return true; }

  int         setSize(int size);
  std::string setInputType(std::string inputType);
  std::string setOutputType(std::string outputType);

  void simulate(SILICON::simulation::Simulator&     sim,
                const SILICON::simulation::Context& context) override;
  void serializeYosys(SILICON::yosys::SerializationContext& context) const override;

private:
  void initializeProperties();
  void reshapeBuses();
  void reshapeBuses(int size, const std::string& inputType,
                    const std::string& outputType);
  void driveOutput(SILICON::simulation::Simulator& sim);
  void clearState();

  [[nodiscard]] int         configuredSize() const;
  [[nodiscard]] std::string configuredInputType() const;
  [[nodiscard]] std::string configuredOutputType() const;
  [[nodiscard]] bool        parallelInput() const;
  [[nodiscard]] bool        parallelOutput() const;

  std::vector<State> state;
};

}  // namespace SILICON::core
