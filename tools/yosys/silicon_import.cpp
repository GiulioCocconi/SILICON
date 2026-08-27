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

#include <algorithm>
#include <optional>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

constexpr int MaxDecodedSelectorWidth = 10;
constexpr int DecoderDensityFactor    = 4;

#include "silicon_import_pm.h"

namespace {

using EqMember = std::pair<RTLIL::Cell*, int>;

// One equality comparison that may become an output of a shared decoder. value
// is the compared constant and therefore the output index; selector identifies
// the non-constant side of the equality.
struct EqCandidate {
  RTLIL::Cell*   cell;
  RTLIL::SigSpec selector;
  int            value;
};

struct EqDecoderGroup {
  RTLIL::SigSpec        selector;
  std::vector<EqMember> members;
};

std::vector<RTLIL::Cell*> allCells(RTLIL::Module* module)
{
  std::vector<RTLIL::Cell*> cells;
  cells.reserve(module->cells().size());
  for (auto* cell : module->cells())
    cells.push_back(cell);
  return cells;
}

void collectSelectedCells(pool<RTLIL::Cell*>& selectedCells, RTLIL::Module* module)
{
  for (auto* cell : module->selected_cells())
    selectedCells.insert(cell);
}

class SignalUsers {
public:
  SignalUsers(RTLIL::Module* module, SigMap& sigmap) : sigmap_(sigmap)
  {
    for (auto port : module->ports)
      add(module->wire(port), nullptr);

    for (auto* cell : module->cells())
      for (const auto& connection : cell->connections())
        add(connection.second, cell);
  }

  int count(const RTLIL::SigSpec& signal)
  {
    pool<RTLIL::Cell*> users;
    for (const auto bit : sigmap_(signal)) {
      const auto it = users_.find(bit);
      if (it == users_.end())
        continue;
      for (auto* user : it->second)
        users.insert(user);
    }
    return GetSize(users);
  }

private:
  void add(const RTLIL::SigSpec& signal, RTLIL::Cell* cell)
  {
    for (const auto bit : sigmap_(signal)) {
      if (bit.wire == nullptr)
        continue;
      users_[bit].insert(cell);
    }
  }

  SigMap&                                 sigmap_;
  dict<RTLIL::SigBit, pool<RTLIL::Cell*>> users_;
};

std::optional<EqCandidate> decodeEquality(RTLIL::Cell* cell, SigMap& sigmap,
                                          SignalUsers& users)
{
  if (cell->type != ID($eq))
    return std::nullopt;

  const int aWidth = cell->getParam(ID::A_WIDTH).as_int();
  const int bWidth = cell->getParam(ID::B_WIDTH).as_int();
  const int yWidth = cell->getParam(ID::Y_WIDTH).as_int();

  if (aWidth != bWidth || aWidth <= 0 || aWidth > MaxDecodedSelectorWidth || yWidth != 1)
    return std::nullopt;

  const auto output = sigmap(cell->getPort(ID::Y));
  if (GetSize(output) != 1 || users.count(output) <= 1)
    return std::nullopt;

  const auto a = sigmap(cell->getPort(ID::A));
  const auto b = sigmap(cell->getPort(ID::B));
  if (GetSize(a) != aWidth || GetSize(b) != bWidth)
    return std::nullopt;

  const bool aConstant = a.is_fully_const();
  const bool bConstant = b.is_fully_const();
  if (aConstant == bConstant)
    return std::nullopt;

  // Comparisons without exactly one constant operand, or whose constant contains
  // an x/z literal, are unsuitable for decoder recovery. SigMap gives structurally
  // identical selectors the same canonical identity when candidates are grouped.
  const auto& signal   = aConstant ? b : a;
  const auto& constant = aConstant ? a : b;
  const auto  value    = constant.as_const();
  if (!value.is_fully_def())
    return std::nullopt;

  return EqCandidate{cell, signal, value.as_int()};
}

std::vector<EqDecoderGroup> collectEqDecoderGroups(RTLIL::Module* module)
{
  /*
   * Yosys commonly represents a case statement as a bank of $eq cells that
   * compare one selector against constants:
   *
   *   $eq(selector, K) -> $demux.outputs[K]
   *
   * Recover a sufficiently dense bank with distinct constants as one word-level
   * decoder. A bank needs at least two members, and comparisons whose results
   * have only one consumer remain standalone instead of being disguised as an
   * unnecessarily large decoder.
   */
  SigMap      sigmap(module);
  SignalUsers users(module, sigmap);

  dict<RTLIL::SigSpec, std::vector<EqMember>> buckets;
  for (auto* cell : module->selected_cells()) {
    const auto candidate = decodeEquality(cell, sigmap, users);
    if (candidate)
      buckets[candidate->selector].emplace_back(candidate->cell, candidate->value);
  }

  std::vector<EqDecoderGroup> groups;
  for (auto& [selector, bucket] : buckets) {
    if (GetSize(bucket) < 2)
      continue;

    pool<int> values;
    bool      unique = true;
    for (const auto& [cell, value] : bucket) {
      (void)cell;
      if (!values.insert(value).second) {
        unique = false;
        break;
      }
    }
    if (!unique)
      continue;

    const int laneCount = 1 << GetSize(selector);
    if (GetSize(bucket) * DecoderDensityFactor < laneCount)
      continue;

    std::sort(bucket.begin(), bucket.end(), [](const EqMember& lhs, const EqMember& rhs) {
      return lhs.first->name.str() < rhs.first->name.str();
    });

    groups.push_back({selector, std::move(bucket)});
  }

  std::sort(groups.begin(), groups.end(),
            [](const EqDecoderGroup& lhs, const EqDecoderGroup& rhs) {
              return lhs.members.front().first->name.str()
                     < rhs.members.front().first->name.str();
            });

  return groups;
}

void raiseDecodedPmux(silicon_import_pm& matcher)
{
  auto& state = matcher.st_decoded_pmux;

  // The PMG matcher has proved that every select predicate decodes the same
  // selector and has materialized a complete, ordered lane table. Reusing the
  // existing cell preserves its output wiring while changing only its inputs.
  RTLIL::SigSpec packedData;
  for (const auto& lane : state.lanes)
    packedData.append(lane);

  matcher.blacklist(state.pmux);
  state.pmux->type = ID($bmux);
  state.pmux->setParam(ID::S_WIDTH, state.selector.size());
  state.pmux->setPort(ID::A, packedData);
  state.pmux->setPort(ID::S, state.selector);
  state.pmux->unsetPort(ID::B);

  log("Raised decoded $pmux cell %s.%s to $bmux.\n", log_id(matcher.module),
      log_id(state.pmux));
}

void raiseEqDecoder(RTLIL::Module* module, const EqDecoderGroup& group)
{
  const int laneCount = 1 << GetSize(group.selector);

  // The fresh wire supplies private bits for sparse/unmatched decoder outputs.
  // Replacing only matched indices preserves the exact Y bits consumed elsewhere.
  RTLIL::SigSpec outputs(module->addWire(NEW_ID, laneCount));
  for (const auto& [cell, value] : group.members)
    outputs[value] = cell->getPort(ID::Y)[0];

  auto* demux = module->addDemux(NEW_ID, RTLIL::Const(RTLIL::State::S1, 1),
                                 group.selector, outputs);

  const int memberCount = GetSize(group.members);
  for (const auto& [cell, value] : group.members) {
    (void)value;
    module->remove(cell);
  }

  log("Raised %d equality cells in %s to decoder %s.\n", memberCount, log_id(module),
      log_id(demux));
}

}  // namespace

struct SiliconPmuxBmuxPass : public Pass {
  SiliconPmuxBmuxPass() : Pass("silicon_pmux_bmux", "raise decoded $pmux cells to $bmux")
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
      silicon_import_pm matcher(module, allCells(module));
      collectSelectedCells(matcher.ud_decoded_pmux.selected_cells, module);
      matcher.run_decoded_pmux(raiseDecodedPmux);
    }
  }
} SiliconPmuxBmuxPass;

struct SiliconEqDecoderPass : public Pass {
  SiliconEqDecoderPass()
    : Pass("silicon_eq_decoder", "raise shared constant equality cells to $demux")
  {
  }

  void help() override
  {
    log("\n");
    log("    silicon_eq_decoder [selection]\n");
    log("\n");
    log("Replace groups of selected equal-width $eq cells that compare one selector\n");
    log("against distinct fully-defined constants with an equivalent $demux decoder.\n");
    log("Groups with fewer than two comparisons or duplicate values are unchanged.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    log_header(design, "Executing SILICON_EQ_DECODER pass.\n");
    extra_args(args, 1, design);

    for (auto* module : design->selected_modules())
      for (const auto& group : collectEqDecoderGroups(module))
        raiseEqDecoder(module, group);
  }
} SiliconEqDecoderPass;

PRIVATE_NAMESPACE_END
