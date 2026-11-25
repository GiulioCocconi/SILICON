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

#include "kernel/ffinit.h"
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

constexpr int MAX_DECODED_SELECTOR_WIDTH = 10;
constexpr int DECODER_DENSITY_FACTOR    = 4;

// Init attributes may be attached to an alias of the register output.
bool hasDefinedInit(const RTLIL::SigSpec& signal, RTLIL::Module* module,
                    const SigMap& sigmap)
{
  return !FfInitVals(&sigmap, module)(signal).is_fully_undef_x_only();
}

// Cell-level user counts cannot distinguish two lanes of the same memory.
// Only remove feedback storage when no surviving memory input uses its bits.
bool privateReadAddress(RTLIL::Cell* mem, int port, const SigMap& sigmap)
{
  const int           abits     = mem->getParam(ID::ABITS).as_int();
  const auto          addresses = sigmap(mem->getPort(ID::RD_ADDR));
  pool<RTLIL::SigBit> lane;
  for (int bit = port * abits; bit < (port + 1) * abits; ++bit)
    if (addresses[bit].wire)
      lane.insert(addresses[bit]);
  for (int bit = 0; bit < GetSize(addresses); ++bit)
    if ((bit < port * abits || bit >= (port + 1) * abits) && lane.count(addresses[bit]))
      return false;
  for (const auto& connection : mem->connections()) {
    if (connection.first == ID::RD_ADDR || !mem->input(connection.first))
      continue;
    const auto signal = sigmap(connection.second);
    for (int bit = 0; bit < GetSize(signal); ++bit) {
      if (connection.first == ID::RD_EN && bit == port)
        continue;  // This lane's address and enable are replaced by the fold.
      if (lane.count(signal[bit]))
        return false;
    }
  }
  return true;
}

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
  SignalUsers(RTLIL::Module* module, SigMap& sigmap) : sigmap(sigmap)
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
    for (const auto bit : sigmap(signal)) {
      const auto it = this->users.find(bit);
      if (it == this->users.end())
        continue;
      for (auto* user : it->second)
        users.insert(user);
    }
    return GetSize(users);
  }

private:
  void add(const RTLIL::SigSpec& signal, RTLIL::Cell* cell)
  {
    for (const auto bit : sigmap(signal)) {
      if (bit.wire == nullptr)
        continue;
      this->users[bit].insert(cell);
    }
  }

  SigMap&                                 sigmap;
  dict<RTLIL::SigBit, pool<RTLIL::Cell*>> users;
};

std::optional<EqCandidate> decodeEquality(RTLIL::Cell* cell, SigMap& sigmap,
                                          SignalUsers& users)
{
  if (cell->type != ID($eq))
    return std::nullopt;

  const int aWidth = cell->getParam(ID::A_WIDTH).as_int();
  const int bWidth = cell->getParam(ID::B_WIDTH).as_int();
  const int yWidth = cell->getParam(ID::Y_WIDTH).as_int();

  if (aWidth != bWidth || aWidth <= 0 || aWidth > MAX_DECODED_SELECTOR_WIDTH || yWidth != 1)
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
    if (GetSize(bucket) * DECODER_DENSITY_FACTOR < laneCount)
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

void foldMemrdAddress(silicon_import_pm& matcher)
{
  auto& state = matcher.st_memrd_address;
  auto  addresses = state.mem->getPort(ID::RD_ADDR);
  auto  enables   = state.mem->getPort(ID::RD_EN);
  addresses.replace(state.read_port * state.mem->getParam(ID::ABITS).as_int(),
                    state.reg->getPort(ID::D));
  enables.replace(state.read_port, state.reg->getPort(ID::EN));
  state.mem->setPort(ID::RD_ADDR, addresses);
  state.mem->setPort(ID::RD_EN, enables);
  log("Folded ROM address register %s and mux %s into %s.\n",
      log_id(state.reg), log_id(state.mux), log_id(state.mem));
  matcher.autoremove(state.mux);
  matcher.autoremove(state.reg);
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
      silicon_import_pm matcher(module, allCells(module));
      collectSelectedCells(matcher.ud_memrd_address.selected_cells, module);
      matcher.run_memrd_address(foldMemrdAddress);
    }
  }
} SiliconMemrdAddressPass;

// SILICON_MEMORY follows $mem_v2's packed, least-significant-port-first buses.
// WIDTH, SIZE, ABITS, RD_PORTS, WR_PORTS, INIT and ID describe one memory;
// WR_PORTS=0 is ROM, WR_PORTS=1 is RAM with positive-edge, per-bit writes.
// Reads are asynchronous. Clocked ROM reads become external enabled registers
// (address registers, or data registers for SIZE=1); output holds remain external
// latches. INIT is binary, entirely X, or a byte-aligned binary prefix with X tail.
// silicon_mem_slug names a project document; only the importer chooses its path.
static bool supportedMemoryInit(const RTLIL::Const& init)
{
  int known = 0;
  while (known < GetSize(init)
         && (init[known] == RTLIL::State::S0 || init[known] == RTLIL::State::S1))
    ++known;
  for (int bit = known; bit < GetSize(init); ++bit)
    if (init[bit] != RTLIL::State::Sx)
      return false;
  return known == GetSize(init) || known % 8 == 0;
}

struct SiliconMemoryLowerPass : public Pass {
  SiliconMemoryLowerPass()
    : Pass("silicon_memory_lower", "lower supported Yosys memories to SILICON_MEMORY")
  {
  }

  void help() override
  {
    log("\n    silicon_memory_lower [selection]\n\n");
    log("Validate $mem_v2 cells and emit one SILICON_MEMORY per memory.\n");
    log("Keep clocked ROM reads and output holds in external storage cells.\n");
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
        const bool writable = mem->getParam(ID::WR_PORTS).as_int() != 0;
        const auto reject = [&](const char* reason) {
          log_error("Unsupported %s %s.%s: %s.\n", writable ? "RAM" : "ROM",
                    log_id(module), log_id(mem), reason);
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
        const int writePorts = mem->getParam(ID::WR_PORTS).as_int();
        if (width <= 0 || width > std::numeric_limits<unsigned short>::max() || size <= 0
            || size > std::numeric_limits<int>::max() / width
            || abits < std::max(
                   1, static_cast<int>(std::bit_width(static_cast<unsigned>(size - 1))))
            || abits > std::numeric_limits<unsigned short>::max() || ports <= 0
            || ports > std::numeric_limits<unsigned short>::max()
            || ports > std::numeric_limits<int>::max() / width
            || ports > std::numeric_limits<int>::max() / std::max(1, abits)
            || !isZero(mem->getParam(ID::OFFSET))
            || !mem->getParam(ID::WR_PORTS).is_fully_def() || writePorts < 0
            || writePorts > 1)
          reject("unsupported geometry or port count");
        for (auto key :
             {ID::RD_CE_OVER_SRST, ID::RD_TRANSPARENCY_MASK, ID::RD_COLLISION_X_MASK,
              ID::RD_WIDE_CONTINUATION, ID::WR_PRIORITY_MASK, ID::WR_WIDE_CONTINUATION})
          if (!isZero(mem->getParam(key)))
            reject("unsupported read controls or write behavior");
        if (writable) {
          if (mem->getParam(ID::RD_CLK_ENABLE) != RTLIL::Const(0, ports)
              || mem->getParam(ID::WR_CLK_ENABLE) != RTLIL::Const(1, 1)
              || mem->getParam(ID::WR_CLK_POLARITY) != RTLIL::Const(1, 1))
            reject("requires asynchronous reads and positive-edge writes");
          if (mem->getPort(ID::RD_EN) != RTLIL::SigSpec(RTLIL::State::S1, ports)
              || mem->getPort(ID::RD_ARST) != RTLIL::SigSpec(RTLIL::State::S0, ports)
              || mem->getPort(ID::RD_SRST) != RTLIL::SigSpec(RTLIL::State::S0, ports))
            reject("read enable must be constant one and resets constant zero");
        } else {
          for (auto key : {ID::WR_CLK_ENABLE, ID::WR_CLK_POLARITY})
            if (!isZero(mem->getParam(key)))
              reject("unsupported write clock control");
        }
        if (GetSize(mem->getPort(ID::WR_ADDR)) != writePorts * abits
            || GetSize(mem->getPort(ID::WR_DATA)) != writePorts * width
            || GetSize(mem->getPort(ID::WR_EN)) != writePorts * width
            || GetSize(mem->getPort(ID::WR_CLK)) != writePorts)
          reject("invalid write port widths");
        for (auto key : {ID::RD_ARST_VALUE, ID::RD_SRST_VALUE, ID::RD_INIT_VALUE}) {
          const auto value = mem->getParam(key);
          if (GetSize(value) != ports * width)
            reject("invalid read register value width");
          for (int bit = 0; bit < ports * width; ++bit)
            if (value[bit] != RTLIL::State::S0 && value[bit] != RTLIL::State::S1
                && value[bit] != RTLIL::State::Sx)
              reject("invalid read register value");
        }
        const auto init = mem->getParam(ID::INIT);
        if (GetSize(init) != size * width || !supportedMemoryInit(init))
          reject("initialization must be binary, entirely X, or a byte-aligned binary "
                 "prefix with X tail");
        auto       addr        = mem->getPort(ID::RD_ADDR);
        auto       data        = mem->getPort(ID::RD_DATA);
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
          const auto readData = data.extract(port * width, width);
          if (sync) {
            // Native Memory has asynchronous reads. Preserve the selected edge and
            // enable in a separate register, retaining a vector Register even for
            // a one-bit address or data bus.
            auto      input = size == 1 ? RTLIL::SigSpec(module->addWire(NEW_ID, width))
                                        : addr.extract(port * abits, abits);
            const int bits  = GetSize(input);
            auto      output =
                size == 1 ? readData : RTLIL::SigSpec(module->addWire(NEW_ID, abits));
            if (size == 1)
              data.replace(port * width, input);
            else
              addr.replace(port * abits, output);
            if (bits == 1) {
              input.append(RTLIL::State::S0);
              output.append(module->addWire(NEW_ID));
            }
            module->addDffe(NEW_ID, clock.extract(port, 1), enable.extract(port, 1),
                            input, output, positive);
          }

          // Keep output holds outside the memory. The ordinary technology map
          // accepts scalar latches, so split a matching vector latch here.
          if (!writable) {
            for (auto* latch : cells) {
              if (removed.count(latch) || !selected.count(latch)
                  || latch->type != ID($dlatch) || latch->getPort(ID::D) != readData)
                continue;
              if (latch->getParam(ID::EN_POLARITY).as_int() != 1
                  || GetSize(latch->getPort(ID::Q)) != width
                  || GetSize(latch->getPort(ID::EN)) != 1)
                reject("unsupported ROM output latch");
              for (int bit = 0; bit < width; ++bit) {
                auto* scalar       = module->addDlatch(NEW_ID, latch->getPort(ID::EN),
                                                       readData.extract(bit, 1),
                                                       latch->getPort(ID::Q).extract(bit, 1));
                scalar->attributes = latch->attributes;
              }
              removed.insert(latch);
            }
          }
        }
        auto* memory = module->addCell(NEW_ID, RTLIL::escape_id("SILICON_MEMORY"));
        for (auto key :
             {ID::WIDTH, ID::SIZE, ID::ABITS, ID::RD_PORTS, ID::WR_PORTS, ID::INIT})
          memory->setParam(key, mem->getParam(key));
        memory->setParam(RTLIL::escape_id("ID"), mem->getParam(ID::MEMID));
        memory->setPort(ID::RD_ADDR, addr);
        memory->setPort(ID::RD_DATA, data);
        for (auto key : {ID::WR_ADDR, ID::WR_DATA, ID::WR_EN, ID::WR_CLK})
          memory->setPort(key, mem->getPort(key));
        memory->attributes = mem->attributes;
        module->remove(mem);
      }
      for (auto* latch : removed)
        module->remove(latch);
    }
  }
} SiliconMemoryLowerPass;

PRIVATE_NAMESPACE_END
