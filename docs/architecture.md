# Volt Kernel Architecture

Volt is organized as a layered kernel. Each layer should be testable without requiring
the layers above it.

## Source Of Truth

The canonical source of truth is the logical circuit model:

- component definitions
- component instances
- pin definitions
- nets
- pin-to-net membership
- constraints and validation rules

Schematics are authored views over canonical nets. A schematic wire displays a net; it
does not own electrical connectivity.

## Initial Layers

```text
volt-core
  typed IDs, diagnostics, results, properties, units

volt-circuit
  component definitions, component instances, pins, nets, circuit validation

volt-schematic
  pages, symbols, wires, labels, consistency checks over canonical nets

volt-io
  deterministic save/load formats

volt-python
  Python bindings over the stable public kernel API
```

The first implementation slice only builds enough of `volt-core` to prove the build and
test system. The circuit model follows after the scaffold is stable.

## Identity

Volt should avoid using strings as internal identity. Names such as `R1`, `VCC`, and
`U1.PA0` are user-facing labels. Internal relationships should use typed IDs such as
`ComponentId`, `PinDefId`, and `NetId`.

## Mutation Boundary

Kernel data should be mutated through explicit operations:

- define component
- add component instance
- create net
- connect pin to net
- disconnect pin
- validate circuit

This preserves invariants and gives future undo/redo, serialization, and Python bindings
a narrow API surface.

