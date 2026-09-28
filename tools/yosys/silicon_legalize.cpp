/* SILICON's word-level RTLIL legalization and capability boundary. */
#include "kernel/rtlil.h"
#include "kernel/yosys.h"

#include <algorithm>
#include <limits>
#include <string>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace {
RTLIL::IdString id(const char* name) { return RTLIL::escape_id(name); }

int width(RTLIL::Module* module, RTLIL::Cell* cell, RTLIL::IdString key)
{
  if (!cell->hasParam(key))
    log_error("SILICON: missing %s in module %s cell %s (%s).\n", log_id(key),
              log_id(module), log_id(cell), log_id(cell->type));
  int result = cell->getParam(key).as_int();
  if (result <= 0 || result > std::numeric_limits<unsigned short>::max())
    log_error("SILICON: invalid %s in module %s cell %s (%s): %d.\n", log_id(key),
              log_id(module), log_id(cell), log_id(cell->type), result);
  return result;
}

bool flag(RTLIL::Module* module, RTLIL::Cell* cell, RTLIL::IdString key)
{
  if (!cell->hasParam(key))
    log_error("SILICON: missing %s in module %s cell %s (%s).\n", log_id(key),
              log_id(module), log_id(cell), log_id(cell->type));
  auto value = cell->getParam(key);
  if (!value.is_fully_def() || (value.as_int() != 0 && value.as_int() != 1))
    log_error("SILICON: invalid %s in module %s cell %s (%s).\n", log_id(key),
              log_id(module), log_id(cell), log_id(cell->type));
  return value.as_int() == 1;
}

RTLIL::SigSpec port(RTLIL::Module* module, RTLIL::Cell* cell, RTLIL::IdString key,
                    int expected)
{
  auto value = cell->getPort(key);
  if (GetSize(value) != expected)
    log_error("SILICON: invalid %s width in module %s cell %s (%s): expected %d, got %d.\n",
              log_id(key), log_id(module), log_id(cell), log_id(cell->type), expected,
              GetSize(value));
  return value;
}

void narrowTruth(RTLIL::Module* module, RTLIL::Cell* cell, int yWidth)
{
  auto y = port(module, cell, ID::Y, yWidth);
  if (yWidth > 1) {
    module->connect(y.extract(1, yWidth - 1), RTLIL::SigSpec(RTLIL::State::S0, yWidth - 1));
    cell->setPort(ID::Y, y.extract(0, 1));
  }
}

void normalizeOperand(RTLIL::Module* module, RTLIL::Cell* cell, RTLIL::IdString key,
                      int originalWidth, int targetWidth, bool signExtend)
{
  auto signal = port(module, cell, key, originalWidth);
  signal.extend_u0(targetWidth, signExtend);
  cell->setPort(key, signal);
}

void runLogic(RTLIL::Design* design)
{
  for (auto* module : design->selected_modules())
    for (auto* cell : module->selected_cells()) {
      int mode = cell->type == ID($logic_and) ? 0 : cell->type == ID($logic_or) ? 1
                 : cell->type == ID($logic_not) ? 2 : -1;
      if (mode < 0) continue;
      int aWidth = width(module, cell, ID::A_WIDTH);
      int yWidth = width(module, cell, ID::Y_WIDTH);
      (void)flag(module, cell, ID::A_SIGNED);
      port(module, cell, ID::A, aWidth);
      if (mode != 2) {
        int bWidth = width(module, cell, ID::B_WIDTH);
        (void)flag(module, cell, ID::B_SIGNED);
        port(module, cell, ID::B, bWidth);
        cell->setParam(ID::B_WIDTH, bWidth);
      }
      narrowTruth(module, cell, yWidth);
      cell->parameters.clear();
      cell->setParam(id("MODE"), mode);
      cell->setParam(ID::A_WIDTH, aWidth);
      if (mode != 2) cell->setParam(ID::B_WIDTH, GetSize(cell->getPort(ID::B)));
      cell->type = id("SILICON_LOGIC");
    }
}

void runCompare(RTLIL::Design* design)
{
  for (auto* module : design->selected_modules())
    for (auto* cell : module->selected_cells()) {
      int mode = cell->type == ID($eq) ? 0 : cell->type == ID($lt) ? 1
                 : cell->type == ID($le) ? 2 : cell->type == ID($gt) ? 3
                 : cell->type == ID($ge) ? 4 : -1;
      if (mode < 0) continue;
      int aWidth = width(module, cell, ID::A_WIDTH);
      int bWidth = width(module, cell, ID::B_WIDTH);
      int yWidth = width(module, cell, ID::Y_WIDTH);
      bool aSigned = flag(module, cell, ID::A_SIGNED);
      bool bSigned = flag(module, cell, ID::B_SIGNED);
      bool sign = aSigned && bSigned;
      int targetWidth = std::max(aWidth, bWidth);
      normalizeOperand(module, cell, ID::A, aWidth, targetWidth, sign);
      normalizeOperand(module, cell, ID::B, bWidth, targetWidth, sign);
      narrowTruth(module, cell, yWidth);
      cell->parameters.clear();
      cell->setParam(ID::WIDTH, targetWidth);
      cell->setParam(id("MODE"), mode);
      cell->setParam(id("SIGNED"), sign ? 1 : 0);
      cell->type = id("SILICON_COMPARE");
    }
}

void runShift(RTLIL::Design* design)
{
  for (auto* module : design->selected_modules())
    for (auto* cell : module->selected_cells()) {
      int direction = cell->type == ID($shl) ? 0 :
                      (cell->type == ID($shr) || cell->type == ID($sshr)) ? 1 : -1;
      if (direction < 0) continue;
      bool arithmeticCell = cell->type == ID($sshr);
      int aWidth = width(module, cell, ID::A_WIDTH);
      int bWidth = width(module, cell, ID::B_WIDTH);
      int yWidth = width(module, cell, ID::Y_WIDTH);
      bool signedInput = flag(module, cell, ID::A_SIGNED);
      if (flag(module, cell, ID::B_SIGNED))
        log_error("SILICON: unsupported signed shift amount in module %s cell %s (%s).\n",
                  log_id(module), log_id(cell), log_id(cell->type));
      normalizeOperand(module, cell, ID::A, aWidth, yWidth, signedInput);
      port(module, cell, ID::B, bWidth);
      port(module, cell, ID::Y, yWidth);
      cell->parameters.clear();
      cell->setParam(ID::WIDTH, yWidth);
      cell->setParam(id("B_WIDTH"), bWidth);
      cell->setParam(id("DIRECTION"), direction);
      cell->setParam(id("ARITHMETIC"), arithmeticCell && signedInput ? 1 : 0);
      cell->type = id("SILICON_SHIFT");
    }
}

bool supported(RTLIL::IdString type)
{
  static const pool<RTLIL::IdString> types = {
    ID($and), ID($or), ID($xor), ID($not), ID($_NAND_), ID($_NOR_),
    ID($pos), ID($add), ID($sub), ID($mux), ID($bmux), ID($demux),
    ID($dff), ID($dffsr), ID($dffsre), ID($dlatch), ID($adffe),
    id("SILICON_ROM"), id("SILICON_LOGIC"), id("SILICON_COMPARE"),
    id("SILICON_SHIFT"), id("SILICON_DFF"), id("SILICON_DFFE"),
    id("SILICON_DLATCH"), id("SILICON_DFFSR"), id("SILICON_DFFSRE"),
    id("SILICON_JKFF"), id("SILICON_HALF_ADDER"), id("SILICON_FULL_ADDER"),
    id("SILICON_ADDER"), id("SILICON_PIPO"), id("SILICON_PISO"),
    id("SILICON_SIPO"), id("SILICON_SISO")};
  return types.count(type);
}

void runValidate(RTLIL::Design* design)
{
  for (auto* module : design->selected_modules())
    for (auto* cell : module->selected_cells()) {
      if (cell->type == ID($add)
          && (flag(module, cell, ID::A_SIGNED) || flag(module, cell, ID::B_SIGNED)))
        log_error("SILICON: unsupported signed $add in module %s cell %s.\n",
                  log_id(module), log_id(cell));
      if (supported(cell->type) || design->module(cell->type)) continue;
      log_error("SILICON: unsupported cell type %s in module %s cell %s.\n",
                log_id(cell->type), log_id(module), log_id(cell));
    }
}
}

struct SiliconLogicPass : Pass {
  SiliconLogicPass() : Pass("silicon_logic", "legalize word-level logical operators") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    extra_args(args, 1, design); runLogic(design);
  }
} SiliconLogicPass;

struct SiliconComparePass : Pass {
  SiliconComparePass() : Pass("silicon_compare", "normalize comparison widths and signedness") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    extra_args(args, 1, design); runCompare(design);
  }
} SiliconComparePass;

struct SiliconShiftPass : Pass {
  SiliconShiftPass() : Pass("silicon_shift", "normalize shift widths and modes") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    extra_args(args, 1, design); runShift(design);
  }
} SiliconShiftPass;

struct SiliconValidatePass : Pass {
  SiliconValidatePass() : Pass("silicon_validate", "reject cells outside SILICON's canonical subset") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    extra_args(args, 1, design); runValidate(design);
  }
} SiliconValidatePass;

struct SiliconLegalizePass : Pass {
  SiliconLegalizePass() : Pass("silicon_legalize", "run SILICON's ordered legalization pipeline") {}
  void execute(std::vector<std::string> args, RTLIL::Design* design) override {
    if (args.size() != 2)
      log_error("Usage: silicon_legalize <technology-map-path>\n");
    Pass::call(design, "silicon_pmux_bmux");
    Pass::call(design, "silicon_eq_decoder");
    Pass::call(design, "pmuxtree");
    Pass::call(design, "delete t:$scopeinfo");
    Pass::call(design, "opt -nosdff");
    Pass::call(design, "memory_dff");
    Pass::call(design, "silicon_memrd_address");
    Pass::call(design, "silicon_memory_lower");
    Pass::call(design, "silicon_logic");
    Pass::call(design, "silicon_compare");
    Pass::call(design, "silicon_shift");
    Pass::call(design, "simplemap t:$and r:Y_WIDTH=1 %i t:$or r:Y_WIDTH=1 %i "
                       "t:$xor r:Y_WIDTH=1 %i t:$not r:Y_WIDTH=1 %i "
                       "t:$reduce_and t:$reduce_or t:$reduce_xor");
    Pass::call(design, "extract_fa");
    // The vector overload preserves a parsed path as one argument, including spaces.
    Pass::call(design, std::vector<std::string>{"techmap", "-map", args[1]});
    Pass::call(design, "opt_clean");
    Pass::call(design, "silicon_validate");
  }
} SiliconLegalizePass;

PRIVATE_NAMESPACE_END
