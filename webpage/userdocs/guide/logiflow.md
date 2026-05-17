# LogiFlow Basics

LogiFlow is the circuit design and simulation area in SILICON. It is meant for building combinational and sequential logic with a graphical editor.

## Core Concepts

### Components

Components are the active objects in a circuit. Common examples include inputs, outputs, logic gates, buses, multiplexers, and flip-flops.

### Wires

Wires connect component ports. A valid circuit needs each signal source to connect to the destination that consumes it.

### Inputs and Outputs

Inputs let you drive the circuit manually during simulation. Outputs show the resulting signal state.

## Recommended Workflow

1. Sketch the circuit at a high level.
2. Place inputs and outputs first.
3. Add gates and stateful components.
4. Connect wires in small groups.
5. Simulate after each meaningful change.
6. Rename or reorganize components before the circuit becomes hard to scan.

## Simulation Checks

When a circuit does not produce the expected result, verify these points first:

- Every required input is connected.
- No output depends on an unfinished wire.
- Gate inputs are connected in the intended order.
- Sequential elements have the expected initial state.
- The circuit has been saved after major edits.

::: warning
Some LogiFlow features are still work in progress. If a component is marked WIP on the homepage, expect behavior and file compatibility to change.
:::
