/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "kernel/register.h"
#include "kernel/rtlil.h"
#include "kernel/yosys.h"

#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct SiliconBmuxCasePass : public Pass {
  SiliconBmuxCasePass() : Pass("silicon_bmux_case", "raise $bmux cells to case processes")
  {
  }

  void help() override
  {
    log("\n");
    log("    silicon_bmux_case [selection]\n");
    log("\n");
    log("Replace selected $bmux cells with combinational RTLIL switch processes.\n");
    log("The Verilog backend emits those processes as case statements instead of\n");
    log("lowering the binary muxes to nested conditional expressions.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    log_header(design, "Executing SILICON_BMUX_CASE pass.\n");
    extra_args(args, 1, design);

    for (auto* module : design->selected_modules()) {
      std::vector<RTLIL::Cell*> muxes;
      for (auto* cell : module->selected_cells())
        if (cell->type == ID($bmux))
          muxes.push_back(cell);

      for (auto* cell : muxes) {
        const RTLIL::SigSpec data      = cell->getPort(ID::A);
        const RTLIL::SigSpec selection = cell->getPort(ID::S);
        const RTLIL::SigSpec output    = cell->getPort(ID::Y);
        const int            width     = cell->getParam(ID::WIDTH).as_int();

        if (width <= 0 || output.size() != width || selection.empty()
            || data.size() % width != 0) {
          log_error("Malformed $bmux cell %s.%s.\n", log_id(module), log_id(cell));
        }

        const int lane_count = data.size() / width;
        if (selection.size() >= 31 || lane_count != (1 << selection.size())) {
          log_error("Inconsistent selector or data width on $bmux cell %s.%s.\n",
                    log_id(module), log_id(cell));
        }

        auto* process       = module->addProcess(module->uniquify("$silicon$bmux_case"));
        process->attributes = cell->attributes;

        auto* switch_rule   = new RTLIL::SwitchRule;
        switch_rule->signal = selection;
        process->root_case.switches.push_back(switch_rule);

        for (int lane = 0; lane < lane_count; ++lane) {
          auto* case_rule = new RTLIL::CaseRule;
          case_rule->compare.emplace_back(lane, selection.size());
          case_rule->actions.emplace_back(output, data.extract(lane * width, width));
          switch_rule->cases.push_back(case_rule);
        }

        auto* default_rule = new RTLIL::CaseRule;
        default_rule->actions.emplace_back(output, RTLIL::Const(RTLIL::State::Sx, width));
        switch_rule->cases.push_back(default_rule);

        log("Raised $bmux cell %s.%s to process %s.\n", log_id(module), log_id(cell),
            log_id(process));
        module->remove(cell);
      }
    }
  }
} SiliconBmuxCasePass;

PRIVATE_NAMESPACE_END
