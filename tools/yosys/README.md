# SILICON RTLIL import boundary

`elaborateHierarchy` accepts raw Yosys JSON. It runs `silicon_legalize` after
hierarchy/muxpack and writes **SILICON-legalized Yosys JSON** for
`deserialize`. Direct `deserialize` callers must supply that legalized form.
`readVerilog` resolves parameterized module instances while the frontend AST is
available, lowers processes, and then returns raw JSON. `serialize` also emits
raw Yosys cells and may need legalization before import.

The ordered entry point runs:

1. `silicon_pmux_bmux`, `silicon_eq_decoder`: recognize mux and decoder patterns.
2. `pmuxtree`, `delete t:$scopeinfo`, `opt -nosdff`, `memory_share -nosat -nowiden`,
   `memory_dff` for read-only memories: Yosys preparation.
3. `silicon_memrd_address`, `silicon_memory_lower`: legalize ROMs and writable RAMs.
4. `silicon_logic`, `silicon_compare`, `silicon_shift`: normalize expression semantics.
5. Selective `simplemap` (including `$reduce_bool` to OR gates), `extract_fa`,
   SILICON `techmap`, `opt_clean`.
6. `silicon_gates`, `silicon_mux`: recover gates with multiple inputs, combine
   compatible mux trees, and compact repeated mux choices.
7. `silicon_validate`: reject unsupported cells before `write_json`.

The expression passes only touch selected cells. Each changes raw cells to a
canonical type once; a second invocation leaves those cells unchanged.

## Canonical cells

The importer accepts `$and`, `$or`, `$xor`, `$not`, `$_NAND_`, `$_NOR_`,
`$pos`, `$add`, `$sub`, `$neg`, `$mux`, `$bmux`, `$demux`, scalar zero-reset
`$adffe`, declared module instances, and the existing
`SILICON_*` technology cells listed in `cells.hpp`, including `SILICON_ROM` and
`SILICON_RAM`.
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
- `SILICON_GATE`: `MODE` 0/1 means AND/OR; `INPUTS` counts ports `A0` through
  `A<INPUTS-1>`. `BITWISE=1` requires every input and `Y` to have `WIDTH` bits.
  `BITWISE=0` applies whole-operand truth semantics to inputs of any positive
  width and requires `WIDTH=1` and a one-bit `Y`.
- `SILICON_MUX`: `WIDTH` is the data width, `S_WIDTH` is the selector width,
  and `INPUTS` counts distinct data lanes. `A` packs those lanes in
  least-significant-lane-first order; `S` and `Y` have `S_WIDTH` and `WIDTH`
  bits. `TABLE` contains one unsigned `S_WIDTH`-bit lane index per binary
  selector value, packed least-significant-word first. Every index must be
  below `INPUTS`, and every data lane must appear. Export expands this cell
  to a standard `$bmux`.

`SILICON_RAM` is emitted only for writable memories. It has independent asynchronous
read ports and one positive-edge write port with per-bit write enables. Parameters are
`WIDTH`, `SIZE`, `ABITS`, `INIT`, and `ID`, plus `RD_PORTS` for multiple read ports
(omitted for a single read port). `RD_ADDR` and `RD_DATA` pack read ports in
least-significant-lane-first order. Ports are `RD_ADDR`, `RD_DATA`, `WR_ADDR`,
`WR_DATA`, `WR_EN`, and `WR_CLK`. Initialization must be fully binary or entirely
X. Address buses may be wider than the minimum needed for `SIZE`. Memory
attributes, including `silicon_mem_slug`, survive lowering. Memories without
write ports use `SILICON_ROM` instead.

`silicon_validate` rejects any cell outside this subset, including native
memories and unsupported operators. The memory pass diagnoses unsupported ROM
and RAM configurations before validation.

## Graphical bus connections

`silicon_gates` folds compatible AND/OR chains into gates with multiple inputs.
Only selected gates whose complete output has one consumer are absorbed;
shared outputs, bit taps, `keep` attributes, and feedback loops prevent
contraction. The importer decodes the resulting canonical cells, and the editor
expands gate symbols to fit their inputs.

`silicon_mux` combines mux trees when every branch is a private mux with the same
child selector and data width. Combined selectors put the root bits first, which
preserves UNKNOWN/ERROR precedence. Combined selectors are limited to eight bits.
Priority chains, independent branch selectors and tapped intermediate outputs
keep their original structure. Both structure passes respect selection and
`keep` attributes and can be run repeatedly without further changes.

The mux pass also makes repeated lanes share a single data input. The optional
`selectionTable` property lists data-port indices in binary selector order, for example `0,1,0,2`
for a two-bit selector with three distinct choices. Empty uses the ordinary
one-input-per-selection layout. Table indices must cover every selector value
and use contiguous data-port indices starting at zero. Clear the table before
changing the selector width. Native save/load preserves the compact layout;
Yosys export expands it to a standard `$bmux`. Port tooltips list selector values.

Import keeps contiguous source slices as buses. One shared `WireSplitter` per
source exposes only the requested slices, including overlapping slices; an
ordinary part-select does not need a `WireMerger`. Concatenations use one merger
input per contiguous source or literal run. Repeated sign bits and high zero
padding of a contiguous source slice become `Extender` components.

`WireSplitter` retains its total input `size`. Its optional `slices` property is
a comma-separated list of `offset:width` pairs in output-port order, for example
`0:8,8:8,0:16` for two bytes and an overlapping half-word. An empty value retains
the original individual-bit outputs. `WireMerger` has an optional `widths`
property listing input widths in least-significant-first order, for example
`8,8,16` for a 32-bit output. The widths must sum to `size`; an empty value
retains individual-bit inputs. Both configurations survive native save/load and
Verilog export. The editor labels bus ports with their bit ranges.
