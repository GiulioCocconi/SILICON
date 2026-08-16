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
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <optional>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct DecodedChoice {
  RTLIL::SigSpec selector;
  int            value = 0;
};

std::optional<DecodedChoice> decodedChoice(RTLIL::Cell* cell, SigMap& sigmap)
{
  if (cell->type == ID($logic_not)) {
    const auto input  = sigmap(cell->getPort(ID::A));
    const auto output = sigmap(cell->getPort(ID::Y));
    if (input.empty() || output.size() != 1)
      return std::nullopt;
    return DecodedChoice{input, 0};
  }

  if (cell->type != ID($eq) || cell->getParam(ID::A_SIGNED).as_bool()
      || cell->getParam(ID::B_SIGNED).as_bool()) {
    return std::nullopt;
  }

  const auto lhs = sigmap(cell->getPort(ID::A));
  const auto rhs = sigmap(cell->getPort(ID::B));
  if (lhs.size() != rhs.size() || cell->getPort(ID::Y).size() != 1)
    return std::nullopt;

  const bool lhs_constant = lhs.is_fully_const() && lhs.as_const().is_fully_def();
  const bool rhs_constant = rhs.is_fully_const() && rhs.as_const().is_fully_def();
  if (lhs_constant == rhs_constant)
    return std::nullopt;
  return lhs_constant ? DecodedChoice{rhs, lhs.as_const().as_int()}
                      : DecodedChoice{lhs, rhs.as_const().as_int()};
}

struct SiliconPmuxBmuxPass : public Pass {
  SiliconPmuxBmuxPass()
    : Pass("silicon_pmux_bmux", "raise decoded $pmux cells to $bmux")
  {
  }

  void help() override
  {
    log("\n");
    log("    silicon_pmux_bmux [selection]\n");
    log("\n");
    log("Replace $pmux cells whose mutually-exclusive select inputs compare one\n");
    log("binary selector against constant values with an equivalent $bmux. Sparse\n");
    log("choices are filled from the $pmux default input. Priority muxes are left\n");
    log("unchanged.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    log_header(design, "Executing SILICON_PMUX_BMUX pass.\n");
    extra_args(args, 1, design);

    for (auto* module : design->selected_modules()) {
      SigMap                            sigmap(module);
      dict<RTLIL::SigBit, RTLIL::Cell*> drivers;
      for (auto* cell : module->cells()) {
        if (cell->type != ID($eq) && cell->type != ID($logic_not))
          continue;
        const auto output = sigmap(cell->getPort(ID::Y));
        if (output.size() == 1)
          drivers[output[0]] = cell;
      }

      for (auto* cell : module->selected_cells()) {
        if (cell->type != ID($pmux))
          continue;

        const int  width   = cell->getParam(ID::WIDTH).as_int();
        const auto fallback = cell->getPort(ID::A);
        const auto data    = cell->getPort(ID::B);
        const auto choices = sigmap(cell->getPort(ID::S));
        if (width <= 0 || fallback.size() != width || choices.empty()
            || data.size() != width * choices.size())
          continue;

        RTLIL::SigSpec              selector;
        std::vector<RTLIL::SigSpec> lanes;
        pool<int>                   seen_values;
        bool                        valid = true;
        for (int choice = 0; choice < choices.size(); ++choice) {
          const auto driver = drivers.find(choices[choice]);
          if (driver == drivers.end()) {
            valid = false;
            break;
          }
          const auto decoded = decodedChoice(driver->second, sigmap);
          if (!decoded || decoded->selector.empty() || decoded->selector.size() >= 31
              || (!selector.empty() && decoded->selector != selector)
              || !seen_values.insert(decoded->value).second) {
            valid = false;
            break;
          }
          selector = decoded->selector;
          if (lanes.empty())
            lanes.assign(std::size_t{1} << selector.size(), fallback);
          if (decoded->value < 0 || decoded->value >= static_cast<int>(lanes.size())) {
            valid = false;
            break;
          }
          lanes[decoded->value] = data.extract(choice * width, width);
        }
        if (!valid) {
          continue;
        }

        RTLIL::SigSpec packed_data;
        for (const auto& lane : lanes)
          packed_data.append(lane);

        log("Raised decoded $pmux cell %s.%s to $bmux.\n", log_id(module),
            log_id(cell));
        cell->type = ID($bmux);
        cell->setParam(ID::S_WIDTH, selector.size());
        cell->setPort(ID::A, packed_data);
        cell->setPort(ID::S, selector);
        cell->unsetPort(ID::B);
      }
    }
  }
} SiliconPmuxBmuxPass;

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
