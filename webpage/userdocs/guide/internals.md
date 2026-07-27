# Silicon Internals

This page describes how Silicon's major subsystems fit together. It is intended for
advanced users and contributors, so it focuses on behavior, boundaries, and extension
points rather than class-by-class API details. For the latter, use the
<a href="/SILICON/internaldocs/">generated class and member reference</a>.

## Simulator kernel model

Simulation does not run directly on the circuit being edited or serialized. A
`SimulationSession` owns that **source circuit**, passes it through
`CircuitElaborator`, and gives the resulting **runtime circuit** to `Simulator`.
Primitive components are cloned. `SubcircuitComponent` instances are recursively
replaced by their project definitions; parent interface wires are mapped to the
definition's boundary wires and internal wires are cloned per instance. The elaborator
only consumes Silicon core circuits and has no frontend or source-language extension
point. The source topology and graphical scene remain unchanged. Recursive subcircuit
dependencies, missing types, and mismatched interfaces fail during elaboration.

The runtime circuit is compiled into an execution plan. Strongly connected regions are
separated from acyclic regions, and the resulting steps are ordered by data flow.
Silicon also caches the forward cone reachable from each input wire, so an input change
does not require evaluating unrelated components. A topology or port-shape change
invalidates these caches: an interactive `Circuit` recompiles its plan, while the normal
UI session rebuilds its elaborated runtime circuit.

The kernel is event driven:

1. A component reads its input buses and calls `Simulator::updateWire()` for its outputs.
2. A zero-delay combinational update is applied immediately. A delayed update enters a
   time-ordered event queue at `currentTime + delay`.
3. All valid events at one timestamp are applied as a batch, then only their combined
   forward cone is evaluated. This prevents observers from seeing a half-applied
   timestamp.
4. A bounded `run(duration)` processes queued timestamps within the requested window and
   advances the clock to the end of the window. `runUntilIdle()` instead drains all
   pending delayed events.

Delayed transitions use replacement semantics per source component and output wire. A
newly scheduled transition supersedes the previous pending transition for that pair;
stale queue entries are ignored. Scheduling the wire's already-current state cancels the
pending transition. This models propagation delay without allowing obsolete work to
change the circuit later.

Acyclic steps run once in topological order. A cyclic strongly connected region is
evaluated repeatedly at the same simulation time until a pass changes no state. These
zero-time iterations are **delta cycles**: they let stable feedback converge without
inventing a propagation delay. If the region does not converge within the configured
transition limit, simulation throws an unstable-zero-delay-loop error.

Edge-triggered components opt into staged sequential outputs with
`usesStagedSequentialOutputs()`. During a reactive pass their zero-delay writes are
collected rather than exposed immediately. The simulator commits them together from the
same pre-edge state, then evaluates the downstream cone once more. This prevents one
register or flip-flop from observing another component's new state during the same
logical edge.

Interactive input toggles call `setBus()`, evaluate the affected forward cone, and then
`runUntilIdle()` so delayed propagation settles completely. The UI advances one display
tick after settling, keeping successive user changes at distinct waveform timestamps.
Long-running construction and evaluation accept a cooperative cancellation callback;
cancellation is checked between components and delta passes, but an event timestamp is
never left half-applied.

Two independent safety limits protect the application:

- `maxTransitionsPerDeltaCycle` defaults to 1,000 and bounds convergence of one cyclic
  region.
- `maxSimulationSteps` defaults to 100,000 and bounds the number of queued timestamp
  batches handled by one run. Reaching it returns `StepLimitReached`.

Both limits are configurable in the application settings. Increasing them can allow a
large, valid circuit to settle, but it does not make an oscillating zero-delay loop
stable.

Relevant source: [runtime elaboration](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/elaboration.hpp),
[simulation sessions](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/simulationSession.hpp),
and the [event-driven simulator](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/simulator.hpp).

## Project file format

A `.sil` project is a versioned ZIP archive, not one JSON document. Silicon writes the
`mimetype` entry first and stores it uncompressed with the exact value
`application/vnd.silicon.project+zip`. Readers validate this marker before parsing the
manifests.

```text
example.sil
├── mimetype
├── metadata.json
├── project.json
├── circuits/
│   ├── main.json
│   └── controller.json
├── subcircuits/
│   └── adder.json
└── hdl/
    └── adder.v
```

`metadata.json` contains the archive `formatVersion`, the Silicon version that wrote the
file, and UTC creation/modification timestamps. `project.json` contains the project name,
description, and `mainCircuit`, which must name an existing flat JSON entry below
`circuits/`. There must be at least one circuit. Additional circuit files are discovered
under `circuits/`; optional project-local subcircuits are flat entries under
`subcircuits/`, with the filename stem serving as the subcircuit slug. Nested paths,
duplicate paths, duplicate slugs, or a main-circuit reference outside `circuits/` are
rejected.

Inside a running editor, both kinds are represented by the same
`silicon::project::Document`. Its validated project-relative path is its only identity
and also determines its kind. A subcircuit slug is derived when needed:
`subcircuits/adder.json` becomes `adder`; it is not stored as independent state.
`ProjectFile::documents` is likewise the authoritative archive collection.
`mainCircuitJson` is only a compatibility mirror of the document selected by
`project.json.mainCircuit`.

Every open document lives in the ordered `DocumentStore`. Circuit and subcircuit
creation, deletion, activation, tree selection, undo restoration, and saving all use
the same canonical path and lifecycle. The store preserves insertion order, including
when an undo restores a document at its former index, and emits path-based change
notifications so components can refresh a referenced subcircuit safely.

Each circuit or subcircuit entry is a serialized editor scene. Its two main sections have
different responsibilities:

```json
{
  "circuit": { "components": ["core topology ..."] },
  "visual": { "components": ["positions ..."], "wires": ["segments ..."] }
}
```

The `circuit` object is the canonical logical graph: component type identifiers,
declared properties, input/output buses, and shared wire IDs. The `visual` object stores
graphical component positions, rotations, visual type names, and routed wire-segment
geometry. A visual component's `vertexId` associates it with the corresponding core
component. Subcircuit documents may also carry graphical boundary/shape metadata. This
separation lets simulation and native circuit deserialization remain independent of Qt
scene data.

An HDL-backed subcircuit additionally carries exactly one optional descriptor:

```json
{
  "hdl": {
    "type": "verilog",
    "path": "hdl/adder.v"
  }
}
```

The type is currently restricted to `verilog`; SystemVerilog mode is not enabled. The
path is normalized and relative to the archive root, and the referenced source is stored
as a `ProjectAsset` rather than embedded in the scene. Every descriptor must reference an
existing asset, and one asset cannot be shared by multiple subcircuits. The HDL source's
top module name is the subcircuit slug derived from its document path.

Converting a graphical subcircuit to HDL writes its generated source under `hdl/`, clears
the graphical implementation, and cannot currently be reversed. Editable code mode and
compiled mode are still distinct: editing hides the circuit and disables simulation;
leaving it runs Yosys, replaces the scene's cached core topology, makes the source
read-only, and re-enables simulation. A compile failure leaves code mode active. Saving
or switching documents also compiles pending HDL first, so invalid source blocks the
operation instead of committing a stale core circuit.

A `Document` can additionally hold prepared core-circuit JSON for a subcircuit. For a
graphical document the UI derives it from the scene; for an HDL-backed document Yosys
lowers the source into the same core `Circuit` representation before it is stored. That
payload is the implementation used by definition loading and elaboration and is never
written as another archive entry. Replacing a scene clears any older prepared payload
unless a replacement is supplied at the same time, preventing stale logical data from
surviving an edit.

Subcircuit placeholders resolve their slug to a canonical path and query the active
project `DocumentStore`. Definition loading returns a core `SubcircuitDefinition`
containing the implementation circuit and its interface. Subcircuit interfaces use
named `CircuitPort` values, keeping each port name attached to its bus for elaboration
and hierarchical Yosys export rather than maintaining parallel name arrays.
Elaboration never reads the HDL descriptor or invokes Yosys, so graphical and HDL-backed
subcircuits are indistinguishable once their core implementations have been prepared.

Compatibility is deliberately strict. The archive reader currently accepts only the
current `metadata.json.formatVersion`, and native core circuit JSON must report the
current Silicon version. Do not rewrite a `.sil` archive with a generic ZIP tool and
assume entry layout, compression, unknown fields, or future schema versions will remain
compatible. Use Silicon to migrate projects, preserve both logical and visual sections,
and treat undocumented entries as implementation details.

Relevant source: the [project archive contract](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/serialization/projectFile.hpp),
[core topology serializer](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/circuit.cpp),
and [scene serializer](https://github.com/GiulioCocconi/SILICON/blob/main/src/ui/common/diagramScene/diagramSceneSerializer.cpp).

## Waveform management

The simulation controller discovers graphical inputs and outputs, sorts each group by
scene position, and registers their buses with the simulator. The component's `name`
property becomes the signal name; unnamed signals receive deterministic names such as
`input_0` and `output_0`. Inputs come first and `inputCount` marks the boundary between
the editable input group and the observed output group. Each signal definition also
records its bus width.

A `SiliconWaveformTrace` is a list of signal definitions plus timestamped snapshots.
Each snapshot contains the encoded value of every registered bus. Appending another
snapshot at the latest timestamp replaces the existing snapshot instead of creating a
zero-width display interval. During a worker simulation, snapshots are collected into
one batch and delivered to the GUI after the worker joins; the viewer then coalesces
rapid arrivals into at most one refresh per frame. These two batching levels avoid an
event-loop post and full relayout for every transition.

Edit mode exposes only the leading input signals. It starts with zero-valued boundaries
at time zero and at the requested duration. Assigning a value to `[start, end)` inserts
or updates boundary snapshots, changes only the chosen input column inside the interval,
and compacts adjacent identical snapshots. Leaving edit mode sends the input-only samples
back to the simulation controller.

Replay builds a fresh runtime circuit, maps input columns to ordered input buses, advances
the simulator to each sample time, and applies all values for that timestamp together.
If a replay payload contains repeated timestamps, later values replace earlier values by
column before propagation. Outputs and intermediate delayed events are sampled through
the same trace-bus configuration, so the replayed display follows kernel time rather
than wall-clock time.

Waveforms can optionally be written as FST for external viewers. Live FST tracing and UI
snapshots observe the same named buses and timestamps. The waveform viewer can also
export its already-collected snapshots to an FST file; FST is an interchange/output
format, not the in-memory editing model.

Relevant source: the [waveform data model](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/siliconWaveform.hpp),
[simulation/UI bridge](https://github.com/GiulioCocconi/SILICON/blob/main/src/ui/common/diagramScene/diagramSceneSimulationController.cpp),
and [FST adapter](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/siliconFst.hpp).

## Yosys interoperability and the SILICON technology library

Yosys JSON remains the netlist transport format. `Circuit::getYosysJson()` and
`Circuit::deserializeYosys()` run in process and produce or consume the shape written by
Yosys' `write_json` command. Native builds can additionally launch an external Yosys
process: `importVerilog()` runs a controlled Verilog-to-JSON script and `exportVerilog()`
runs a JSON-to-structural-Verilog script. Yosys is invoked directly, without a shell, and
is not embedded in SILICON. By default the `yosys` executable is resolved from `PATH` on
every native platform; `ToolOptions::executable` is an explicit override.

Three layers have deliberately different jobs:

- **Yosys JSON** transports modules, ports, parameters, cells, and LSB-first connection
  vectors. It is not itself the source of SILICON component semantics.
- **The SILICON Yosys technology library** defines canonical `SILICON_*` cells for
  native identities that generic Yosys cells cannot preserve reliably. Black-box
  synthesis declarations, separate simulation models, and the technology map live in
  `resources/yosys/`.
- **The generic Yosys importer** remains a compatibility fallback for supported cells
  such as `$and`, `$add`, `$mux`, and `$dff`. It reconstructs equivalent native logic,
  but does not promise the original high-level component identity.

On export, one Silicon `Circuit` becomes a module. Named input/output components become
module ports and port netnames; shared wires receive module-local numeric signal IDs;
and each component's `serializeYosys()` writes through a
`silicon::yosys::SerializationContext`. Parameter strings use Yosys' MSB-first binary
encoding, while connection arrays follow SILICON's LSB-first bus order. Simulation-only
propagation delays are intentionally omitted. The final source uses ANSI-style module
headers, removes Yosys' redundant `wire` redeclarations for ports, and retains genuine
internal or shared nets. Two-input NAND and NOR gates use fused cells so expressions such
as `assign q = ~(set | nq);` do not expose a temporary OR-to-inverter wire.

### Technology-cell ABI

Technology-cell names, ports, and parameters are a stable interchange contract and do
not depend on graphical shapes. The initial ABI is:

| Cell | Ports | Parameters | Exact native component |
| --- | --- | --- | --- |
| `SILICON_DFF` | `D`, `CLK`, `Q`, `QN` | `CLK_POLARITY` | `DFlipFlop` without async controls |
| `SILICON_DFFE` | `D`, `EN`, `CLK`, `Q`, `QN` | `CLK_POLARITY`, `EN_POLARITY` | `EFlipFlop` without async controls |
| `SILICON_DFFSR` | `D`, `CLK`, `SET`, `CLR`, `Q`, `QN` | `CLK_POLARITY`, `SET_POLARITY`, `CLR_POLARITY` | `DFlipFlop` with async controls |
| `SILICON_DFFSRE` | `D`, `EN`, `CLK`, `SET`, `CLR`, `Q`, `QN` | clock, enable, set, and clear polarities | `EFlipFlop` with async controls |
| `SILICON_JKFF` | `J`, `K`, `CLK`, `SET`, `CLR`, `Q`, `QN` | clock, set, and clear polarities | `JKFlipFlop` |
| `SILICON_HALF_ADDER` | `A`, `B`, `SUM`, `COUT` | none; all ports are scalar | `HalfAdder` |
| `SILICON_FULL_ADDER` | `A`, `B`, `CIN`, `SUM`, `COUT` | none; all ports are scalar | `FullAdder` |
| `SILICON_ADDER` | `A`, `B`, `SUM`, `COUT` | `WIDTH`, `A_SIGNED`, `B_SIGNED` | unsigned `AdderNBits` |
| `SILICON_REGISTER` | `DATA`, `CLK`, `EN`, `CLR`, `LOAD`, `OUT` | `WIDTH`, parallel/serial mode flags, and control polarities | all four native `Register` modes |

Native flip-flop set, clear, and enable controls are active high; clocks may use either
edge. Clear and set are asynchronous, clear-only means zero, set-only means one, and a
simultaneous or indeterminate set/clear condition produces `UNKNOWN`. `QN` always
complements `Q`, including becoming unknown with it. Register clear has priority over
enable and clock, shifting moves toward bit zero, and PISO load has priority over shift.
Arithmetic cells are unsigned; `SUM` has `WIDTH` bits and `COUT` is the discarded carry
bit. `silicon_cells_sim.v` implements the same rules for external simulation, while
`silicon_cells_bb.v` prevents synthesis from inlining those models.

The custom-cell importer runs before generic dispatch. It requires the exact parameter
and connection sets, validates every width and supported polarity, registers both normal
and complementary outputs with the single-driver checker, applies shape properties, and
only then constructs the matching native component. An unknown `SILICON_*` type is an
error rather than a placeholder or registry lookup.

Import of generic JSON remains pattern based. The compatibility importer:

- selects an explicitly requested module, the sole module, or the unique module carrying
  a nonzero `top` attribute;
- maps ports to named scalar or bus input/output components;
- interns numeric net bits as shared `Wire` objects and turns supported literals into
  constant components;
- recognizes a strict set of cell types and validates required ports, widths, parameters,
  polarity, signedness, and single-driver rules; and
- reconstructs the closest native component or component network, including properties
  such as bus size, selection width, trigger edge, and zero imported delay.

### External Verilog pipelines

Verilog import reads `silicon_cells_bb.v` as a library, elaborates and flattens the
selected user top, preserves word-level arithmetic, extracts recognizable half/full
adder cones, applies `silicon_techmap.v`, and writes JSON. The map covers supported
scalar `$dff`, `$dffe`, `$dffsr`, and `$dffsre` shapes, equal-width unsigned `$add`, and
the `$fa` cells produced by `extract_fa`. A constant-zero carry input identifies a half
adder; otherwise the extracted cell becomes a full adder. Unmatched cells continue to
the generic importer.

Positive- and negative-edge D storage, enabled D storage, supported asynchronous
set/reset forms, equal-width unsigned addition, and extracted half/full adders therefore
map to custom cells. A JK flip-flop is preserved only when `SILICON_JKFF` was already
present, such as an explicit Verilog instance or SILICON export. Arbitrary next-state
logic feeding a D flip-flop is never called a JK flip-flop: equivalent logic does not
prove the original intent.

The default import script does not run ABC. Gate-level remapping would discard the
word-level and educational structure this interface is intended to preserve, and no
later optimization pass is allowed to decompose `SILICON_*` cells. Conversely, equivalent
arbitrary Verilog cannot always be raised back to a particular native component; only
the documented deterministic mappings make that promise.

Verilog export first loads the black-box interfaces, then uses `read_json`, derives
descriptive names for internal signals, sanitizes escaped identifiers, and writes
attribute-free Verilog without renaming those signals to numeric temporaries. Parameters
use decimal notation where possible. A guarded post-processing step folds complete port
declarations into ANSI module headers; it leaves a module untouched if the emitted
declarations do not match the header exactly. The result is structural Verilog containing
parameterized `SILICON_*` instances, not inlined behavioural models. Loading
`silicon_cells_bb.v` makes that output parseable by Yosys; loading
`silicon_cells_sim.v` instead supplies standalone simulation behaviour.

Every external invocation sends captured standard output and standard error to the
`yosys` logger. User-facing failures identify the failed phase and direct the user to
those logs, keeping command transcripts available without duplicating them in dialogs.

Resource lookup checks an explicit `ToolOptions::technologyLibraryDirectory` override,
then compile-time build-tree and configured installation locations, followed by paths
relative to the running executable (including a macOS bundle `Resources` candidate).
CMake installs the three files under `${CMAKE_INSTALL_DATADIR}/silicon/yosys`; paths are
normalized and quoted before being passed through direct process arguments. Technology
resource lookup is independent of Yosys executable lookup, does not depend on the current
working directory, and supports relocated Windows, Linux, and macOS installations.

Emscripten builds cannot launch an external Yosys process, so `runScript()`,
`importVerilog()`, and `exportVerilog()` report a platform-availability error. In-process
JSON import/export, including exact `SILICON_*` round trips, remains available.

Project subcircuits are hierarchical on export: each definition is emitted once as a
module and instances become module-typed cells; recursive definitions and duplicate
module names fail. Import is intentionally narrower. It reconstructs one selected module
and does not turn arbitrary module-instance cells back into project subcircuits. Flatten
or otherwise lower a hierarchical design in an external Yosys run before importing it
when the selected module contains such instances.

Unknown cells are never silently converted to black boxes. An unsupported cell, memory,
inout port, high-impedance literal, malformed connection set, ambiguous module choice, or
unsupported cell variant aborts the whole import with a contextual error. Adding a type
to `ComponentRegistry` alone therefore cannot make it Yosys-importable.

Relevant source: [serialization context and module builder](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/serialization/yosys.cpp),
[native component lowering](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/serialization/yosys_components.cpp),
and the [strict cell-pattern importer](https://github.com/GiulioCocconi/SILICON/blob/main/src/core/serialization/yosys_deserialization.cpp).

## How to add a native component

Use `Multiplexer` and `GraphicalMultiplexer` as a concrete reference. They exercise
validated properties, changing bus shapes, changing graphical ports, native persistence,
and Yosys import/export without requiring a large subsystem.

### 1. Implement the core `Component`

Add the header and implementation under `src/core/` or `src/extraComponents/`, then add
every new `.cpp` file to the appropriate source list in the root `CMakeLists.txt`.

Your type needs:

- a stable serialized identifier such as `Multiplexer::Type`, returned by `typeName()`;
- nonempty `ComponentMetadata` (display name, description, and catalog category);
- a public default constructor, because `ComponentRegistry` constructs an empty instance
  before native JSON reconnects its buses;
- clearly indexed input/output buses, preferably using scoped `Inputs` and `Outputs`
  enums;
- `defineProperty()` calls for configurable `int`, `bool`, or `std::string` values;
- validation/property callbacks that reject invalid values and reshape core buses when a
  property changes topology; and
- `simulate()` logic that reads component state and writes through
  `Simulator::updateWire()`, including the component as the source and any intended
  propagation delay.

The essential shape looks like this:

```cpp
class ExampleComponent : public Component {
public:
  static constexpr std::string_view Type = "ExampleComponent";

  std::string_view typeName() const override { return Type; }
  ComponentMetadata metadata() const override {
    return {"Example", "Explains what the component does.",
            ComponentCategory::Utils};
  }

  ExampleComponent();
  void simulate(Simulator& simulator) override;
  void serializeYosys(silicon::yosys::SerializationContext& context) const override;
};
```

`Multiplexer` declares `selectionSize`, `busSize`, and `delay`. Its callbacks validate the
candidate value and call `setSelectionSize()`/`setBusSize()` so serialized property
application and direct property edits produce the same bus topology. Keep this invariant:
properties, buses, and behavior must agree even when no GUI exists.

`ComponentCategory` controls catalog grouping; it does not define a module interface. A
component that represents a public subcircuit boundary must additionally set
`ComponentMetadata::portRole` to `PortRole::Input` or `PortRole::Output`. Keep the default
`PortRole::None` for ordinary components, including sources such as constants. This core
metadata is intentionally distinct from the UI-only `ItemCategory` flags on graphical items.
`Circuit::getInputPorts()` and `Circuit::getOutputPorts()` use those boundary components as
the named circuit interface. Names come from the component `name` property. Each direction
independently falls back to the topological circuit interface, with deterministic names,
when it has no declared boundary components. `Circuit::getInputs()` and `getOutputs()` expose
the corresponding buses.

### 2. Register native construction

Include the type and add `registerComponent<ExampleComponent>(registry)` in
`registerAllComponents()`. Registration verifies default constructibility and catalog
metadata. It also makes the type available to the catalog's core metadata lookup and,
more importantly, lets `Circuit::deserialize()` recreate a component from its native
`type` field before applying properties and reconnecting serialized buses.

Do not change an existing `Type` string casually. It is part of saved native circuit
data, so renaming it requires an explicit compatibility strategy.

### 3. Implement and register the graphical component

Create a `GraphicalLogicComponent` subclass under
`src/ui/logiFlow/components/` and register its `.cpp` file in `UI_SOURCE_FILES`. Its
constructor should create the core component, install a shape (an existing Qt resource,
SVG, or programmatic `QGraphicsItem`), and declare input/output `PortPair`s that match the
core buses. Override `type()` with a distinct value from `SiliconTypes` for Qt item
classification.

For a fixed component, the base class keeps port widths synchronized through the core
I/O listener. For a component like `GraphicalMultiplexer`, property changes can alter the
number, position, or meaning of ports as well as their width. Its graphical callbacks
delegate to the core `setSelectionSize()`/`setBusSize()` methods and then rebuild the
shape and ports. Reinstall those callbacks in `setComponent()` because scene
deserialization replaces the constructor-created core instance with the restored one.

Finally, add the type to `registerAllGUIComponents()`:

```cpp
reg(std::string(ExampleComponent::Type),
    [](QGraphicsItem* parent) {
      return std::make_unique<GraphicalExampleComponent>(parent);
    });
```

Normally the scene's visual `type` equals the core `Type`, and `reg()` records that
one-to-one mapping. When they differ, use `regMapped(guiType, coreType, factory)`.
Graphical inputs demonstrate this: the persisted GUI type `Input` maps to the core type
`DummyInputComponent`. The mapping lets the catalog ask the core registry for the right
metadata and lets scene deserialization construct both halves correctly.

### 4. Let declared properties flow through the UI and native JSON

No property-dock widget or per-property JSON code is required for ordinary properties.
The dock visits every declared `int`, `bool`, and `std::string` property and creates a
spin box, check box, line edit, or constrained string selector. `Circuit::serialize()`
writes the same property map, and native deserialization calls `setProperty()` so type
checks, allowed string values, validation, and callbacks still apply.

Add a graphical property callback only when the graphical representation must change:
shape bounds, labels, port count, port placement, or a special GUI-to-core port mapping.
That callback must preserve core validation and reshaping, usually by delegating to a
core mutator as `GraphicalMultiplexer` does. A purely behavioral property such as a
fixed-shape component's delay should remain a core-only callback.

### 5. Make three independent Yosys interoperability decisions

Adding a native component requires three separate decisions. None implies either of the
others:

1. Decide whether native export needs a canonical `SILICON_*` technology cell to retain
   identity or semantics. If so, extend all three library files and the strict custom
   importer.
2. Decide whether one or more arbitrary generic Yosys cell shapes can be imported as the
   component. Only do this when their parameters and ports prove the native semantics.
3. Decide whether Verilog synthesis can map or extract the component deterministically.
   Add a technology-map or extraction rule only when it cannot mislabel equivalent logic.

The two directions use different extension points:

- **Export** is component-driven: a native component implements `serializeYosys()` and
  emits one canonical technology cell or a useful generic network.
- **Custom import** is ABI-driven: `SILICON_*` dispatch validates the documented contract
  and reconstructs one native component directly.
- **Generic import** is pattern-driven: generic dispatch recognizes exact supported
  Yosys shapes and may construct an equivalent component network.

#### Export: validate the native component, then lower it to cells

##### 1. Add the export override

Declare `serializeYosys()` in the component header. The base implementation throws, so a
component without an override fails export instead of silently disappearing from the
netlist. Per-component implementations normally live in
`src/core/serialization/yosys_components.cpp`, alongside the shared helpers from
`yosys_helpers.hpp`.

##### 2. Choose the Yosys representation

First decide whether a generic representation is sufficient. Use a `SILICON_*` cell when
native class identity or semantics could not be recovered from the generic network.
Otherwise preserve behavior, bus width, signedness, edge polarity, enable/reset
semantics, and truncation rules with generic cells. Propagation delays remain simulation
metadata and are intentionally omitted.

##### 3. Validate the component's shape

Before emitting anything, validate the component's actual buses against its properties.
`requireBus()` handles a required nonempty bus, but component-specific invariants still
need explicit checks. For example, a multi-bus `Multiplexer` with selection width `N`
must expose exactly `2^N` equally sized data buses followed by the selection bus. Export
must reject a malformed shape rather than generate internally inconsistent JSON.

##### 4. Emit cells with `SerializationContext`

Use `SerializationContext` as follows:

- `bits(bus, nullValue)` returns the module signal numbers or constant literals for an
  existing bus. Connection arrays are LSB-first, exactly like `Bus`.
- `allocateBits(width)` reserves module-local temporary signals for a multi-cell
  lowering.
- `concatenate(vectors)` appends already LSB-first vectors in the supplied lane order.
  It does not reverse individual vectors.
- `parameter(value, width)` produces the fixed-width, MSB-first binary strings used by
  `write_json` parameters. Do not feed connection-order strings into it.
- `addCell(suffix, type, parameters, portDirections, connections)` inserts a uniquely
  named cell in the current module. Every connection and direction key must match the
  chosen Yosys cell contract.
- `addPort()` is reserved for components that represent module boundaries;
  `addSubcircuitInstance()` emits a hierarchical module instance.

For example, the central part of the `Multiplexer` lowering maps its buses to `$bmux`:

```cpp
context.addCell(
    "mux", "$bmux",
    Json{{"WIDTH", SerializationContext::parameter(busWidth)},
         {"S_WIDTH", SerializationContext::parameter(selectionWidth)}},
    directions({{"A", "input"}, {"S", "input"}, {"Y", "output"}}),
    Json{{"A", SerializationContext::concatenate(lanes)},
         {"S", context.bits(requireBus(*this, true, selectionIndex))},
         {"Y", context.bits(requireBus(*this, false, 0))}});
```

Here `lanes` is ordered by selection index. For a one-bit mux it contains the existing
packed data bus; for a wider mux it contains one `busWidth` vector per selectable input.
The resulting `$bmux.A` connection is therefore lane 0 bits, lane 1 bits, and so on,
without changing the bit order inside a lane.

##### 5. Compose generic cells only when native identity is not required

If the native behavior has no direct cell, compose it from supported cells. Use
`allocateBits()` for intermediate results and give each `addCell()` call a stable suffix
such as `fold_1` or `invert`. Cell names are generated deterministically from the circuit
vertex, component type, and suffix, which makes output reproducible and diagnostics
readable.

#### Import: recognize and validate explicit cell patterns

Yosys deserialization is deliberately ABI- and cell-pattern based. Native project
deserialization uses `ComponentRegistry`; Yosys deserialization never consults it to
infer external semantics. Dispatch known `SILICON_*` types before generic cells. Unknown
types and unsupported variants must reach the contextual failure path rather than
becoming placeholders.

##### 1. Wire in a focused importer

Add the component header to `yosys_deserialization.cpp`, implement a focused importer
method, and dispatch to it from `importCell()`. Do not add a registry lookup: an arbitrary
Yosys cell name is not proof that it has the semantics of a same-named Silicon class.

##### 2. Validate first, construct last

An importer method should perform these steps in order:

1. Call `requireConnectionSet()` with the exact accepted port names. This rejects both
   missing and unexpected connections.
2. Read parameters with `checkedWidth()`, `parameterFlag()`, or `parseUnsigned()` as
   appropriate. These helpers accept the supported `write_json` integer/binary forms and
   reject zero, oversized, non-binary, or non-Boolean values where required.
3. Read inputs with `input()`/`inputBit()` and outputs with `output()`/`outputBit()`.
   Inputs may contain supported constants; driven outputs must be numeric. Output reads
   also register drivers, so a later duplicate driver fails the whole import.
4. Cross-check every declared width against its connection length and enforce semantic
   restrictions such as unsigned-only arithmetic, supported polarities, or scalar-only
   storage cells.
5. Construct the native component, set the properties that define its shape, then attach
   the validated input and output buses. Push it into `components` only after the pattern
   is known to be valid.

##### 3. Define every accepted shape explicitly

For muxes the importer accepts two related patterns. `$mux` requires equally wide `A`,
`B`, and `Y` plus a one-bit `S`, and becomes a `Multiplexer` with `selectionSize == 1`.
`$bmux` requires `A.size() == WIDTH * 2^S_WIDTH`, `S.size() == S_WIDTH`, and
`Y.size() == WIDTH`; it splits `A` into LSB-first lanes and restores `selectionSize` and
`busSize`. Width-one lanes use Silicon's packed-data representation, while wider lanes
become separate input buses.

##### 4. Set the correct round-trip expectation

Be explicit about how exact a round trip can be. `Multiplexer` exports as `$bmux` and can
be reconstructed as `Multiplexer`, but a `NandGate` exports as `$and` followed by `$not`
and imports as those two native components. The required contract is equivalent supported
logic and connectivity, not preservation of the original class or graphical layout.

##### 5. Decide how hierarchy is handled

Finally, decide whether the new cell is valid inside a selected flat module only or also
needs hierarchy support. The current importer treats a module-instance cell type as
unsupported; adding a native component branch does not reconstruct project subcircuits.
Designs containing hierarchy must be externally flattened/lowered unless dedicated
hierarchy import is implemented as a separate feature.

This import work is mandatory: implementing export alone does not make the component
round-trip through Yosys.

### 6. Test every boundary

Add focused tests for:

- simulation behavior, including `UNKNOWN`/`ERROR`, delay, edge, or cyclic behavior that
  is relevant to the component;
- property defaults, validation failures, and every bus/port-resizing transition;
- core registry creation, catalog metadata, unknown/duplicate registration, and a native
  `Circuit::serialize()` → `Circuit::deserialize()` round trip;
- GUI factory creation, visual/core type mapping, restored `setComponent()` behavior,
  and shape/port updates after property edits;
- Yosys export cell types and parameters, import reconstruction, and a Yosys round trip;
  and
- malformed native or Yosys input: missing ports, inconsistent widths, unsupported
  parameter variants, and invalid properties must fail rather than produce a partially
  valid component.

The existing [multiplexer tests](https://github.com/GiulioCocconi/SILICON/blob/main/tests/multiplexer.cpp),
[registry tests](https://github.com/GiulioCocconi/SILICON/blob/main/tests/factory.cpp), and
[Yosys tests](https://github.com/GiulioCocconi/SILICON/blob/main/tests/yosys.cpp) provide
starting patterns. Keep tests focused on observable contracts rather than private helper
functions.

For exact declarations while implementing the checklist, return to the
<a href="/SILICON/internaldocs/">Doxygen reference</a>.
