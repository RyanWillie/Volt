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

The first logical-kernel IDs are deliberately narrow:

- `ComponentDefId`
- `ComponentId`
- `PinDefId`
- `PinId`
- `NetId`

Schematic IDs such as page, symbol, and wire IDs are intentionally deferred until the
schematic view layer exists.

## Entity Storage

The first storage primitive is `EntityTable<T, Id>`. It is vector-backed, deterministic,
and insertion ordered. IDs are typed indexes into their owning table.

The table intentionally does not support deletion, generations, tombstones, sparse
storage, or custom allocation yet. Those features only become justified when the kernel
has concrete mutation and undo/redo requirements.

This gives the logical model stable internal identity while keeping electronics meaning
in domain data:

```text
ComponentId       internal engine identity
ReferenceDesignator("R1")  electronics meaning

NetId             internal engine identity
NetName("GND")    electronics meaning
```

## Diagnostics

Diagnostics are generic reports, not validation-specific reports. The same primitive
should support future logical validation, electrical checks, physical DRC, import/load
errors, and editor consistency checks.

Each diagnostic carries:

- `Severity`: info, warning, or error
- `DiagnosticCode`: machine-readable string code
- message: human-readable explanation
- `EntityRef` list: logical entities related to the finding

`DiagnosticCode` is a value wrapper rather than a central enum because codes will grow
across independent kernel layers. `EntityRef` is a reporting type using `EntityKind` plus
an index; it is not the storage model.

## Circuit Definitions

`PinDefinition` and `ComponentDefinition` describe reusable part shapes. They do not
represent actual design instances such as `R1` or `U1`; those come in a later layer.

Definition objects do not store their own IDs. Their IDs are table keys assigned by
`EntityTable`, which avoids duplicated identity state inside the entity payload. The
definition payload stores electronics meaning:

```text
PinDefinition
  name: "VDD"
  number: "17"
  role: PowerInput
  required: true

ComponentDefinition
  name: "Resistor"
  pins: [PinDefId(0), PinDefId(1)]
```

Actual component instances, concrete pin instances, and net connections are intentionally
deferred to subsequent layers.

## Circuit Instances

`ComponentInstance` and `PinInstance` represent concrete design occurrences:

```text
ComponentInstance
  definition: ComponentDefId(0)
  reference: "R1"

PinInstance
  component: ComponentId(0)
  definition: PinDefId(1)
```

Reference designators are domain data, not identity. `ComponentId` remains the internal
engine identity, while `ReferenceDesignator("R1")` is the electronics-facing label.

Pin instances are first-class entities because nets should connect concrete pins, not
reusable pin definitions. Net modeling is intentionally deferred until the instance layer
is stable.

## Nets

`Net` is the canonical logical connection data structure. It stores electronics meaning
and local pin membership:

```text
Net
  name: "GND"
  kind: Ground
  pins: [PinId(0), PinId(3)]
```

A net contains concrete `PinId` values, not reusable `PinDefId` values. This keeps the
model grounded in actual design occurrences.

The current net layer only enforces local membership rules: deterministic pin order,
no duplicate pins inside one net, and explicit disconnect behavior. The circuit-wide
invariant that a pin belongs to zero or one net belongs in the future `Circuit` container,
where all nets and pins are visible at once.

## Circuit Container

`Circuit` is the first owning database for the logical model. It stores entity tables for:

- pin definitions
- component definitions
- component instances
- concrete pin instances
- nets

This layer is intentionally storage-oriented. It assigns typed IDs, owns entity payloads,
and returns const references by ID. It does not yet validate cross-entity references or
enforce the circuit-wide invariant that a pin belongs to at most one net. Those behaviors
belong in later editor and validation layers, where mutations can be checked with the full
circuit context.

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
