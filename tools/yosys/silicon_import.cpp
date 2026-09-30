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

#include "kernel/register.h"
#include "kernel/rtlil.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <algorithm>
#include <bit>
#include <limits>
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

struct SiliconMemrdAddressPass : public Pass {
  SiliconMemrdAddressPass()
    : Pass("silicon_memrd_address", "remove redundant ROM address feedback after memory_dff")
  {
  }

  void help() override
  {
    log("\n    silicon_memrd_address [selection]\n\n");
    log("Fold an enabled address register and its feedback mux into the read\n");
    log("enable of a synchronous, read-only memory port. Run after memory_dff.\n\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    log_header(design, "Executing SILICON_MEMRD_ADDRESS pass.\n");
    extra_args(args, 1, design);

    for (auto* module : design->selected_modules()) {
      SigMap      sigmap(module);
      SignalUsers users(module, sigmap);
      const auto  cells = allCells(module);
      pool<RTLIL::Cell*> selected;
      pool<RTLIL::Cell*> removed;
      collectSelectedCells(selected, module);

      for (auto* mem : cells) {
        if (removed.count(mem) || !selected.count(mem) || mem->type != ID($mem_v2)
            || mem->getParam(ID::WR_PORTS).as_int() != 0
            || mem->getParam(ID::RD_PORTS).as_int() != 1
            || mem->getParam(ID::RD_CLK_ENABLE).as_int() != 1
            || mem->getPort(ID::RD_EN) != RTLIL::SigSpec(RTLIL::State::S1))
          continue;

        const auto address = sigmap(mem->getPort(ID::RD_ADDR));
        bool folded = false;
        for (auto* mux : cells) {
          if (removed.count(mux) || !selected.count(mux) || mux->type != ID($mux)
              || sigmap(mux->getPort(ID::Y)) != address
              || mux->getParam(ID::WIDTH).as_int() != GetSize(address)
              || users.count(address) != 2)
            continue;

          for (auto* reg : cells) {
            if (removed.count(reg) || !selected.count(reg) || reg->type != ID($dffe)
                || reg->getParam(ID::WIDTH).as_int() != GetSize(address)
                || reg->getParam(ID::EN_POLARITY).as_int() != 1
                || reg->getParam(ID::CLK_POLARITY).as_int()
                       != mem->getParam(ID::RD_CLK_POLARITY).as_int()
                || sigmap(reg->getPort(ID::Q)) != sigmap(mux->getPort(ID::A))
                || sigmap(reg->getPort(ID::D)) != sigmap(mux->getPort(ID::B))
                || sigmap(reg->getPort(ID::EN)) != sigmap(mux->getPort(ID::S))
                || sigmap(reg->getPort(ID::CLK)) != sigmap(mem->getPort(ID::RD_CLK))
                || users.count(reg->getPort(ID::Q)) != 2)
              continue;

            mem->setPort(ID::RD_ADDR, reg->getPort(ID::D));
            mem->setPort(ID::RD_EN, reg->getPort(ID::EN));
            log("Folded ROM address register %s and mux %s into %s.\n",
                log_id(reg), log_id(mux), log_id(mem));
            removed.insert(mux);
            removed.insert(reg);
            module->remove(mux);
            module->remove(reg);
            folded = true;
            break;
          }
          if (folded)
            break;
        }
      }
    }
  }
} SiliconMemrdAddressPass;

// JSON contract for SILICON_ROM (one cell per read port): WIDTH, SIZE, ABITS,
// INIT (LSB-first packed bits), ID (unescaped memory identifier), SYNC and
// CLK_POLARITY are parameters. ADDR, DATA, SELECT, CLK and EN are ports. ADDR
// has ABITS bits even for SIZE=1; for that case it must be constant zero.
// SELECT is an optional output latch enable (otherwise one). CLK/EN are zero
// and one for asynchronous reads. SYNC means the importer places an enabled
// address register before the ROM (or a data register for SIZE=1). The
// silicon_mem_slug attribute, when present, is a document *identifier*; the
// importer alone chooses the project path and creates the binary document.
struct SiliconMemoryLowerPass : public Pass {
  SiliconMemoryLowerPass()
    : Pass("silicon_memory_lower", "lower supported Yosys memories to SILICON_ROM") {}

  void help() override
  {
    log("\n    silicon_memory_lower [selection]\n\n");
    log("Validate read-only $mem_v2 cells and emit one SILICON_ROM per read port.\n");
    log("Run after memory_dff and silicon_memrd_address.\n\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    log_header(design, "Executing SILICON_MEMORY_LOWER pass.\n");
    extra_args(args, 1, design);
    for (auto* module : design->selected_modules()) {
      const auto cells = allCells(module);
      pool<RTLIL::Cell*> selected, removed;
      collectSelectedCells(selected, module);
      for (auto* mem : cells) {
        if (!selected.count(mem) || mem->type != ID($mem_v2)) continue;
        const auto reject = [&](const char* reason) {
          log_error("Unsupported ROM %s.%s: %s.\n", log_id(module), log_id(mem), reason);
        };
        const int width = mem->getParam(ID::WIDTH).as_int();
        const int size = mem->getParam(ID::SIZE).as_int();
        const int abits = mem->getParam(ID::ABITS).as_int();
        const int ports = mem->getParam(ID::RD_PORTS).as_int();
        const auto isZero = [](const RTLIL::Const& value) {
          for (int bit = 0; bit < GetSize(value); ++bit)
            if (value[bit] != RTLIL::State::S0) return false;
          return true;
        };
        if (width <= 0 || width > std::numeric_limits<unsigned short>::max()
            || size <= 0 || size > std::numeric_limits<int>::max() / width
            || (size & (size - 1)) != 0
            || abits != std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(size - 1))))
            || ports <= 0 || ports > std::numeric_limits<int>::max() / width
            || ports > std::numeric_limits<int>::max() / std::max(1, abits)
            || !isZero(mem->getParam(ID::OFFSET))
            || !isZero(mem->getParam(ID::WR_PORTS)))
          reject("unsupported geometry or write ports");
        for (auto key : {ID::RD_CE_OVER_SRST, ID::RD_TRANSPARENCY_MASK,
                         ID::RD_COLLISION_X_MASK, ID::RD_WIDE_CONTINUATION,
                         ID::WR_CLK_ENABLE, ID::WR_CLK_POLARITY,
                         ID::WR_PRIORITY_MASK, ID::WR_WIDE_CONTINUATION})
          if (!isZero(mem->getParam(key)))
            reject("unsupported read controls or write behavior");
        for (auto key : {ID::WR_ADDR, ID::WR_DATA, ID::WR_EN, ID::WR_CLK})
          if (GetSize(mem->getPort(key)) != 0)
            reject("memory write connections are not supported");
        const auto init = mem->getParam(ID::INIT);
        if (GetSize(init) != size * width || !init.is_fully_def())
          reject("initialization must fully define every word");
        const auto addr = mem->getPort(ID::RD_ADDR);
        const auto data = mem->getPort(ID::RD_DATA);
        const auto enable = mem->getPort(ID::RD_EN);
        const auto clock = mem->getPort(ID::RD_CLK);
        const auto arst = mem->getPort(ID::RD_ARST);
        const auto srst = mem->getPort(ID::RD_SRST);
        const auto clkEnable = mem->getParam(ID::RD_CLK_ENABLE);
        const auto clkPolarity = mem->getParam(ID::RD_CLK_POLARITY);
        const auto initial = mem->getParam(ID::RD_INIT_VALUE);
        if (GetSize(addr) != ports * abits || GetSize(data) != ports * width
            || GetSize(enable) != ports || GetSize(clock) != ports
            || GetSize(arst) != ports || GetSize(srst) != ports
            || GetSize(clkEnable) != ports || GetSize(clkPolarity) != ports
            || GetSize(initial) != ports * width)
          reject("invalid read port widths");

        for (int port = 0; port < ports; ++port) {
          const bool sync = clkEnable[port] == RTLIL::State::S1;
          const bool positive = clkPolarity[port] == RTLIL::State::S1;
          if ((clkEnable[port] != RTLIL::State::S0 && !sync)
              || (clkPolarity[port] != RTLIL::State::S0 && !positive))
            reject("undefined read clock control");
          if (sync) {
            for (int bit = 0; bit < width; ++bit)
              if (initial[port * width + bit] != RTLIL::State::Sx)
                reject("initialized synchronous read register is unsupported");
            if (arst[port] != RTLIL::State::S0 || srst[port] != RTLIL::State::S0)
              reject("synchronous read reset is unsupported");
          } else if (enable[port] != RTLIL::State::S1) {
            reject("asynchronous read enable must be constant one");
          }
          const auto readAddr = addr.extract(port * abits, abits);
          const auto readData = data.extract(port * width, width);
          if (size == 1 && readAddr != RTLIL::SigSpec(RTLIL::State::S0, abits))
            reject("one-word memory address must be constant zero");

          RTLIL::SigSpec output = readData;
          RTLIL::SigSpec select(RTLIL::State::S1);
          for (auto* latch : cells) {
            if (removed.count(latch) || !selected.count(latch) || latch->type != ID($dlatch)
                || latch->getPort(ID::D) != readData) continue;
            if (latch->getParam(ID::EN_POLARITY).as_int() != 1
                || GetSize(latch->getPort(ID::Q)) != width
                || GetSize(latch->getPort(ID::EN)) != 1)
              reject("unsupported ROM output latch");
            output = latch->getPort(ID::Q);
            select = latch->getPort(ID::EN);
            removed.insert(latch);
            break;
          }

          auto* rom = module->addCell(NEW_ID, RTLIL::escape_id("SILICON_ROM"));
          rom->setParam(ID::WIDTH, width);
          rom->setParam(ID::SIZE, size);
          rom->setParam(ID::ABITS, abits);
          rom->setParam(ID::INIT, init);
          rom->setParam(RTLIL::escape_id("ID"), mem->getParam(ID::MEMID));
          rom->setParam(RTLIL::escape_id("SYNC"), sync ? 1 : 0);
          rom->setParam(ID::CLK_POLARITY, positive ? 1 : 0);
          rom->setPort(ID::ADDR, readAddr);
          rom->setPort(ID::DATA, output);
          rom->setPort(RTLIL::escape_id("SELECT"), select);
          rom->setPort(ID::CLK, sync ? clock.extract(port, 1) : RTLIL::SigSpec(RTLIL::State::S0));
          rom->setPort(ID::EN, sync ? enable.extract(port, 1) : RTLIL::SigSpec(RTLIL::State::S1));
          rom->attributes = mem->attributes;
        }
        module->remove(mem);
      }
      for (auto* latch : removed)
        module->remove(latch);
    }
  }
} SiliconMemoryLowerPass;

PRIVATE_NAMESPACE_END
