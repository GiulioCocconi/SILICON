/* Recover graphical gate and mux structures while the design is still RTLIL. */
#include "kernel/rtlil.h"
#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <algorithm>
#include <optional>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace {
IdString id(const char* name)
{
  return RTLIL::escape_id(name);
}

struct Topology {
  SigMap               signals;
  dict<SigSpec, Cell*> producers;
  dict<SigBit, int>    references;
  pool<SigBit>         kept;

  explicit Topology(Module* module) : signals(module)
  {
    for (auto* wire : module->wires())
      if (wire->get_bool_attribute(ID::keep))
        for (auto bit : signals(SigSpec(wire)))
          kept.insert(bit);
    for (auto name : module->ports)
      for (auto bit : signals(SigSpec(module->wire(name))))
        ++references[bit];
    for (auto* cell : module->cells()) {
      if (cell->hasPort(ID::Y))
        producers[signals(cell->getPort(ID::Y))] = cell;
      for (const auto& connection : cell->connections())
        for (auto bit : signals(connection.second))
          ++references[bit];
    }
  }

  bool privateOutput(Cell* cell, const SigSpec& output)
  {
    if (cell->get_bool_attribute(ID::keep))
      return false;
    for (auto bit : signals(output))
      // One driver reference and exactly one input reference. Ports, bit taps
      // and repeated uses all prevent absorption, including unselected users.
      if (!bit.wire || references[bit] != 2 || kept.count(bit))
        return false;
    return true;
  }
};

struct Gate {
  int                  mode, width;
  bool                 bitwise;
  std::vector<SigSpec> inputs;
  SigSpec              output;
};

std::optional<Gate> gate(Cell* cell, SigMap& signals)
{
  Gate result;
  if (cell->type == ID($and) || cell->type == ID($or)) {
    result.mode    = cell->type == ID($and) ? 0 : 1;
    result.width   = cell->getParam(ID::Y_WIDTH).as_int();
    result.bitwise = true;
    if (cell->getParam(ID::A_WIDTH).as_int() != result.width
        || cell->getParam(ID::B_WIDTH).as_int() != result.width)
      return std::nullopt;
    result.inputs = {signals(cell->getPort(ID::A)), signals(cell->getPort(ID::B))};
  } else if (cell->type == id("SILICON_LOGIC")) {
    result.mode = cell->getParam(id("MODE")).as_int();
    if (result.mode != 0 && result.mode != 1)
      return std::nullopt;
    result.width   = 1;
    result.bitwise = false;
    result.inputs  = {signals(cell->getPort(ID::A)), signals(cell->getPort(ID::B))};
  } else if (cell->type == id("SILICON_GATE")) {
    result.mode     = cell->getParam(id("MODE")).as_int();
    result.width    = cell->getParam(ID::WIDTH).as_int();
    result.bitwise  = cell->getParam(id("BITWISE")).as_bool();
    const int count = cell->getParam(id("INPUTS")).as_int();
    if (count < 2 || count > 65535 || result.mode < 0 || result.mode > 1)
      return std::nullopt;
    for (int i = 0; i < count; ++i)
      result.inputs.push_back(
          signals(cell->getPort(RTLIL::escape_id("A" + std::to_string(i)))));
  } else
    return std::nullopt;
  result.output = signals(cell->getPort(ID::Y));
  if (result.width < 1 || result.width > 65535 || GetSize(result.output) != result.width
      || (!result.bitwise && result.width != 1))
    return std::nullopt;
  for (const auto& input : result.inputs)
    if (input.empty() || (result.bitwise && GetSize(input) != result.width))
      return std::nullopt;
  return result;
}

void foldGates(Module* module)
{
  Topology           topology(module);
  pool<Cell*>        selected, removed;
  std::vector<Cell*> roots;
  for (auto* cell : module->selected_cells()) {
    selected.insert(cell);
    roots.push_back(cell);
  }
  for (auto* root : roots) {
    if (removed.count(root) || root->get_bool_attribute(ID::keep))
      continue;
    auto shape = gate(root, topology.signals);
    if (!shape)
      continue;
    std::vector<SigSpec> pending(shape->inputs.rbegin(), shape->inputs.rend()), inputs;
    pool<Cell*>          absorbed;
    bool                 acyclic = true;
    while (!pending.empty()) {
      auto input = pending.back();
      pending.pop_back();
      auto  found = topology.producers.find(input);
      Cell* child = found == topology.producers.end() ? nullptr : found->second;
      if (child == root) {
        acyclic = false;
        break;
      }
      auto childShape =
          child && selected.count(child) ? gate(child, topology.signals) : std::nullopt;
      if (!childShape || childShape->mode != shape->mode
          || childShape->width != shape->width || childShape->bitwise != shape->bitwise
          || !topology.privateOutput(child, input)) {
        inputs.push_back(input);
        continue;
      }
      if (!absorbed.insert(child).second) {
        acyclic = false;
        break;
      }
      pending.insert(pending.end(), childShape->inputs.rbegin(),
                     childShape->inputs.rend());
    }
    if (!acyclic || absorbed.empty() || GetSize(inputs) > 65535)
      continue;
    root->type = id("SILICON_GATE");
    root->parameters.clear();
    root->connections_.clear();
    root->setParam(id("MODE"), shape->mode);
    root->setParam(ID::WIDTH, shape->width);
    root->setParam(id("BITWISE"), shape->bitwise ? 1 : 0);
    root->setParam(id("INPUTS"), GetSize(inputs));
    for (int i = 0; i < GetSize(inputs); ++i)
      root->setPort(RTLIL::escape_id("A" + std::to_string(i)), inputs[i]);
    root->setPort(ID::Y, shape->output);
    for (auto* child : absorbed)
      removed.insert(child);
  }
  for (auto* cell : removed)
    module->remove(cell);
}

struct Mux {
  int                  width;
  SigSpec              selection, output;
  std::vector<SigSpec> lanes;
};

std::optional<Mux> mux(Cell* cell, SigMap& signals)
{
  if (cell->type != ID($mux) && cell->type != ID($bmux))
    return std::nullopt;
  Mux result;
  result.width     = cell->getParam(ID::WIDTH).as_int();
  result.selection = signals(cell->getPort(ID::S));
  result.output    = signals(cell->getPort(ID::Y));
  const int bits   = GetSize(result.selection);
  if (result.width < 1 || result.width > 65535 || bits < 1 || bits > 15
      || GetSize(result.output) != result.width)
    return std::nullopt;
  if (cell->type == ID($mux)) {
    if (bits != 1)
      return std::nullopt;
    result.lanes = {signals(cell->getPort(ID::A)), signals(cell->getPort(ID::B))};
  } else {
    auto data = signals(cell->getPort(ID::A));
    if (GetSize(data) != result.width * (1 << bits))
      return std::nullopt;
    for (int i = 0; i < 1 << bits; ++i)
      result.lanes.push_back(data.extract(i * result.width, result.width));
  }
  for (const auto& lane : result.lanes)
    if (GetSize(lane) != result.width)
      return std::nullopt;
  return result;
}

void foldMuxes(Module* module)
{
  bool changed;
  do {
    changed = false;
    Topology           topology(module);
    pool<Cell*>        selected, removed;
    std::vector<Cell*> roots;
    for (auto* cell : module->selected_cells()) {
      selected.insert(cell);
      roots.push_back(cell);
    }
    for (auto* root : roots) {
      if (removed.count(root) || root->get_bool_attribute(ID::keep))
        continue;
      auto shape = mux(root, topology.signals);
      if (!shape)
        continue;
      std::vector<Mux> children;
      pool<Cell*>      absorbed;
      for (const auto& lane : shape->lanes) {
        auto  found      = topology.producers.find(lane);
        Cell* child      = found == topology.producers.end() ? nullptr : found->second;
        auto  childShape = child && selected.count(child) && !removed.count(child)
                               ? mux(child, topology.signals)
                               : std::nullopt;
        if (!childShape || child == root || childShape->width != shape->width
            || !topology.privateOutput(child, lane) || !absorbed.insert(child).second
            || (!children.empty() && childShape->selection != children.front().selection))
          break;
        children.push_back(*childShape);
      }
      if (children.size() != shape->lanes.size()
          || GetSize(shape->selection) + GetSize(children.front().selection) > 8)
        continue;
      SigSpec data, selection = shape->selection;
      selection.append(
          children.front().selection);  // root bits first, including X/E precedence
      pool<SigBit> output;
      for (auto bit : shape->output)
        output.insert(bit);
      bool feedback = false;
      for (auto bit : selection)
        feedback |= output.count(bit);
      for (int lane = 0; lane < GetSize(children.front().lanes); ++lane)
        for (const auto& child : children) {
          data.append(child.lanes[lane]);
          for (auto bit : child.lanes[lane])
            feedback |= output.count(bit);
        }
      if (feedback)
        continue;
      root->type = ID($bmux);
      root->parameters.clear();
      root->connections_.clear();
      root->setParam(ID::WIDTH, shape->width);
      root->setParam(ID::S_WIDTH, GetSize(selection));
      root->setPort(ID::A, data);
      root->setPort(ID::S, selection);
      root->setPort(ID::Y, shape->output);
      for (auto* child : absorbed)
        removed.insert(child);
      changed = true;
    }
    for (auto* cell : removed)
      module->remove(cell);
  } while (changed);
}

void compactMuxes(Module* module)
{
  SigMap signals(module);
  for (auto* cell : module->selected_cells()) {
    if (cell->get_bool_attribute(ID::keep))
      continue;
    const auto shape = mux(cell, signals);
    if (!shape)
      continue;
    dict<SigSpec, int>        indices;
    SigSpec                   data;
    std::vector<RTLIL::State> table;
    for (const auto& lane : shape->lanes) {
      auto entry = indices.find(lane);
      int  index;
      if (entry == indices.end()) {
        index         = GetSize(indices);
        indices[lane] = index;
        data.append(lane);
      } else
        index = entry->second;
      for (int bit = 0; bit < GetSize(shape->selection); ++bit)
        table.push_back(index & (1 << bit) ? RTLIL::State::S1 : RTLIL::State::S0);
    }
    if (GetSize(indices) == GetSize(shape->lanes))
      continue;
    cell->type = id("SILICON_MUX");
    cell->parameters.clear();
    cell->connections_.clear();
    cell->setParam(ID::WIDTH, shape->width);
    cell->setParam(ID::S_WIDTH, GetSize(shape->selection));
    cell->setParam(id("INPUTS"), GetSize(indices));
    cell->setParam(id("TABLE"), RTLIL::Const(std::move(table)));
    cell->setPort(ID::A, data);
    cell->setPort(ID::S, shape->selection);
    cell->setPort(ID::Y, shape->output);
  }
}
}  // namespace

struct SiliconGatesPass : Pass {
  SiliconGatesPass() : Pass("silicon_gates", "recover gates with multiple inputs") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    extra_args(args, 1, design);
    for (auto* module : design->selected_modules())
      foldGates(module);
  }
} SiliconGatesPass;

struct SiliconMuxPass : Pass {
  SiliconMuxPass() : Pass("silicon_mux", "recover mux trees and compact repeated choices")
  {
  }
  void execute(std::vector<std::string> args, RTLIL::Design* design) override
  {
    extra_args(args, 1, design);
    for (auto* module : design->selected_modules()) {
      foldMuxes(module);
      compactMuxes(module);
    }
  }
} SiliconMuxPass;

PRIVATE_NAMESPACE_END
