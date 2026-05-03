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
- constraints and design intent

Schematics are authored views over canonical nets. A schematic wire displays a net; it
does not own electrical connectivity.

The core design rule is:

```text
Invalid kernel state should be impossible.
Bad circuit design should be diagnosable.
```

The kernel should reject structurally invalid operations at mutation boundaries. Examples
include missing IDs, dangling references, pin instances that do not belong to the circuit,
and a concrete pin connected to more than one net. Deeper design-quality issues, such as
unconnected pins, single-pin nets, incompatible electrical roles, and power-domain
problems, should be reported by diagnostics and validation layers.

Validation rules are implemented by kernel layers and operate over the canonical model.
User-authored constraints may become stored model data once the constraints layer exists.

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
an index; it is not the storage model. It should only be used for diagnostics and
reporting, not for normal traversal or mutation.

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
  connection_requirement: Required

ComponentDefinition
  name: "Resistor"
  pins: [PinDefId(0), PinDefId(1)]
```

Actual component instances, concrete pin instances, and net connections are separate
concepts introduced in later sections and layers.

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

The standalone net layer only enforces local membership rules: deterministic pin order,
no duplicate pins inside one net, and local disconnect behavior. The circuit-wide
invariant that a pin belongs to zero or one net belongs in `Circuit`, where all nets and
pins are visible at once.

## Circuit Container

`Circuit` is the first owning database for the logical model. It stores entity tables for:

- pin definitions
- component definitions
- component instances
- concrete pin instances
- nets

This layer assigns typed IDs, owns entity payloads, and returns references by ID. Explicit
mutation operations on `Circuit` preserve structural integrity. Operations reject missing
definitions, missing component instances, missing pin instances, nets that reference
unknown pins, and attempts to connect IDs that do not belong to the circuit.

`Circuit` also enforces the core connectivity invariant that a concrete pin belongs to
zero or one net. Deeper design-quality checks are reported by validation layers. Examples
include unconnected pins, single-pin nets, incompatible pin roles, and power-domain
issues.

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
