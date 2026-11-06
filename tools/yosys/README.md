# SILICON RTLIL import boundary

`elaborateHierarchy` accepts raw Yosys JSON. It runs `silicon_legalize` after
hierarchy/muxpack and writes **SILICON-legalized Yosys JSON** for
`deserialize`. Direct `deserialize` callers must supply that legalized form.
`readVerilog` resolves parameterized module instances while the frontend AST is
available, lowers processes, and then returns raw JSON. `serialize` also emits
raw Yosys cells and may need legalization before import.

The ordered entry point runs:

1. `silicon_pmux_bmux`, `silicon_eq_decoder`: recognize mux and decoder patterns.
2. `pmuxtree`, `delete t:$scopeinfo`, `opt -nosdff`, `memory_dff`: Yosys preparation.
3. `silicon_memrd_address`, `silicon_memory_lower`: legalize read-only memories.
4. `silicon_logic`, `silicon_compare`, `silicon_shift`: normalize expression semantics.
5. Selective `simplemap` (including `$reduce_bool` to OR gates), `extract_fa`,
   SILICON `techmap`, `opt_clean`.
6. `silicon_validate`: reject unsupported cells before `write_json`.

The expression passes only touch selected cells. Each changes raw cells to a
canonical type once; a second invocation leaves those cells unchanged.

## Canonical cells

The importer accepts `$and`, `$or`, `$xor`, `$not`, `$_NAND_`, `$_NOR_`,
`$pos`, `$add`, `$sub`, `$neg`, `$mux`, `$bmux`, `$demux`, scalar zero-reset
`$adffe`, declared module instances, and the existing
`SILICON_*` technology cells listed in `cells.hpp`, including `SILICON_ROM`.
Native `$mul`, `$div`, `$mod`, `$dff`, `$dffsr`, `$dffsre`, and supported `$dlatch`
cells are converted to technology cells before this boundary and are rejected by
both `silicon_validate` and the direct importer if they remain. Native `$add` and
`$sub` still reach the importer when their geometry is not covered by technology
mapping; their width handling remains necessary. Vector `$adffe` cells map to
`SILICON_PIPO`; only scalar zero-reset `$adffe` cells reach the native handler.

`$neg` cells map directly to the two's-complement `Complementer` component,
which also exports as `$neg`. Zero-minus-operand `$sub` cells are lowered to
`$neg` by the technology map, sign extending only when both subtraction operands
are signed. Negation operands are sign or zero extended, or truncated, to the
output width before negation.

`$mul` maps to `SILICON_MULTIPLIER`; `$div` and `$mod` map to the `div` and
`mod` outputs of `SILICON_DIVIDER`, whose inputs are `A` and `B`. Like
`SILICON_ADDER`, these technology
cells use `WIDTH` and a single `SIGNED` parameter and have behavioral,
black-box, and export mappings in `silicon_cells.v`. `SILICON_ADDER` requires
`SIGNED=0`, matching the unsigned adder component. They round-trip directly
to the `Multiplier` and `Divider` components. Both components expose `size`, `delay`, and
`signed` properties. All component buses have `size` bits; multiplication keeps
the low bits and signed division truncates toward zero. Division by zero drives
unknown outputs. Import extends operands to the widest input/output width,
using sign extension only when both inputs are signed, then truncates the result.

Declared module instances may omit unused output ports or connect them to empty
arrays. Each unused output receives a private bus at its declared width. Inputs
are required, and connected ports retain width and driver-ownership validation.

Additional expression contracts:

- `SILICON_LOGIC`: `MODE` 0/1/2 means AND/OR/NOT of whole-operand truth
  values. `A_WIDTH` and, for binary modes, `B_WIDTH` equal port widths. `Y`
  is one bit. Wider raw results are zero-filled by RTLIL connections.
- `SILICON_COMPARE`: `WIDTH` equals both input port widths; `MODE` 0..4 means
  EQ/LT/LE/GT/GE; `SIGNED` is 0 or 1; `Y` is one bit. Inputs are already
  sign or zero extended according to Yosys semantics. Wider results are
  zero-filled by RTLIL connections. Raw `$ne` comparisons use EQ followed by
  a one-bit NOT, with any upper result bits still zero-filled.
- `SILICON_SHIFT`: `WIDTH` equals `A` and `Y` widths; `B_WIDTH` equals `B`
  width; `DIRECTION` 0/1 means left/right; `ARITHMETIC` 0/1 controls right
  sign fill. `A` is already truncated or extended to `WIDTH`. Signed shift
  amounts are rejected.

`silicon_validate` rejects any cell outside this subset, including writable
memories and unsupported operators. The memory pass diagnoses unsupported ROM
configuration before validation.
