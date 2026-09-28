# SILICON RTLIL import boundary

`elaborateHierarchy` accepts raw Yosys JSON. It runs `silicon_legalize` after
hierarchy/proc/muxpack and writes **SILICON-legalized Yosys JSON** for
`deserialize`. Direct `deserialize` callers must supply that legalized form.
`readVerilog` returns raw JSON; `serialize` also emits raw Yosys cells and may
need legalization before import.

The ordered entry point runs:

1. `silicon_pmux_bmux`, `silicon_eq_decoder`: recognize mux and decoder patterns.
2. `pmuxtree`, `delete t:$scopeinfo`, `opt -nosdff`, `memory_dff`: Yosys preparation.
3. `silicon_memrd_address`, `silicon_memory_lower`: legalize read-only memories.
4. `silicon_logic`, `silicon_compare`, `silicon_shift`: normalize expression semantics.
5. Selective `simplemap`, `extract_fa`, SILICON `techmap`, `opt_clean`.
6. `silicon_validate`: reject unsupported cells before `write_json`.

The expression passes only touch selected cells. Each changes raw cells to a
canonical type once; a second invocation leaves those cells unchanged.

## Canonical cells

The importer accepts `$and`, `$or`, `$xor`, `$not`, `$_NAND_`, `$_NOR_`,
`$pos`, `$add`, `$sub`, `$mux`, `$bmux`, `$demux`, supported `$dff`, `$dffsr`,
`$dffsre`, `$dlatch`, `$adffe`, declared module instances, and the existing
`SILICON_*` technology cells listed in `cells.hpp`, including `SILICON_ROM`.
`$add` and `$sub` retain their existing importer width handling.

Additional expression contracts:

- `SILICON_LOGIC`: `MODE` 0/1/2 means AND/OR/NOT of whole-operand truth
  values. `A_WIDTH` and, for binary modes, `B_WIDTH` equal port widths. `Y`
  is one bit. Wider raw results are zero-filled by RTLIL connections.
- `SILICON_COMPARE`: `WIDTH` equals both input port widths; `MODE` 0..4 means
  EQ/LT/LE/GT/GE; `SIGNED` is 0 or 1; `Y` is one bit. Inputs are already
  sign or zero extended according to Yosys semantics. Wider results are
  zero-filled by RTLIL connections.
- `SILICON_SHIFT`: `WIDTH` equals `A` and `Y` widths; `B_WIDTH` equals `B`
  width; `DIRECTION` 0/1 means left/right; `ARITHMETIC` 0/1 controls right
  sign fill. `A` is already truncated or extended to `WIDTH`. Signed shift
  amounts are rejected.

`silicon_validate` rejects any cell outside this subset, including writable
memories and unsupported operators. The memory pass diagnoses unsupported ROM
configuration before validation.
