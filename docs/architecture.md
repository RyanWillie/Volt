# Volt Kernel Architecture

Volt is organized as a layered kernel. Each layer should be testable without requiring
the layers above it.

## Source Of Truth

The current canonical source of truth is the logical circuit model:

- component definitions
- component instances
- pin definitions
- nets
- pin-to-net membership
- typed attributes and selected design intent

A future constraints layer may promote more design intent into first-class constraint
entities once the model exists.

Volt layers should be organized around project-level design roots. The current kernel
owns the logical circuit plus schematic and PCB projections over that circuit, while the
Python `Design` facade gives authors one root for those kernel-owned surfaces. Future
constraints, libraries, and reports should extend that root without moving EDA meaning out
of the kernel.

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

## Kernel-Owned EDA Semantics

The kernel owns EDA meaning. Python, UI tools, file importers, schematic renderers, and
PCB tools may make authoring pleasant, but they must lower meaningful operations into
kernel-owned data or kernel mutation APIs.

Layer ownership is strict:

| Layer | Owns | References | Must not own |
| --- | --- | --- | --- |
| Logical circuit | component definitions, component instances, pins, nets, pin-to-net membership, selected parts | library/source metadata | schematic presentation or PCB geometry |
| Schematic projection | sheets, symbol placement, visual wire runs, labels, notes, drawing metadata | `ComponentId`, `PinId`, `NetId` | logical connectivity |
| PCB projection | board outline, layers, footprint placement, copper geometry, zones, vias, routing metadata | `ComponentId`, selected physical parts, `NetId` | logical connectivity |
| Constraints | declared design intent and rule parameters | logical, schematic, or physical entities | validation results |
| Validation | diagnostics and reports | all relevant kernel-owned model layers | mutation of invalid state into valid state |

This means connectivity is single-source:

```text
Circuit owns connectivity.
Schematic visualizes connectivity.
PCB implements connectivity.
Python is syntax over kernel-owned state.
```

Only the logical circuit layer may create or mutate pin-to-net membership. Schematic and
PCB layers may reference logical connectivity, visualize it, annotate it, physically
implement it, and report inconsistencies against it, but they must not create, merge,
split, or otherwise mutate nets.

This rule is intentionally stronger than many schematic-first EDA tools. It protects
code-authored circuits from accidental logical changes while drawing schematics or laying
out boards.

## First-Principles Primitives And Progressive Disclosure

Volt should be built from first-principles EDA abstractions, not from a checklist of
features in existing tools. When evaluating a proposed concept, ask what underlying
circuit meaning it represents and whether that meaning must be validated, serialized,
inspected, imported, exported, or referenced by another layer.

The kernel should expose the minimal set of general concepts required to define,
validate, serialize, and inspect electronically meaningful circuits. Convenience APIs may
make common circuits concise, but they must lower into general kernel-owned concepts
rather than introducing Python-only semantics or use-case-specific shortcuts.

Prefer general primitives over use-case-specific kernel objects. For example:

- a reusable power stage, sensor channel, or motor driver should be built from module,
  port, component, net, and constraint primitives rather than special-purpose kernel
  classes for each circuit pattern;
- SPI, I2C, UART, memory, or connector helpers should build on generic net bundles,
  interfaces, ports, and nets rather than requiring protocol-specific kernel concepts;
- USB, clock, analog, high-current, or power-rail behavior should be expressed through
  reusable net classes or constraints rather than one-off APIs for every case.

Advanced features should use progressive disclosure. Simple circuits should remain simple
to author, while additional electrical intent, constraints, hierarchy, physical-part data,
or projection data can be added only when needed for correctness or interoperability.
The existence of a richer model should not force every author through verbose ceremony.

In short:

```text
Minimal primitives, progressive disclosure, kernel-owned meaning.
```

## Projection Layers

Schematics and PCB design are projection layers. Both now have kernel models: the
schematic layer owns sheets, symbols, wires, and labels over canonical nets, and the PCB
layer owns the board outline, layers, footprint placement, copper geometry, and routing.
Each projection was designed after the logical circuit generation, serialization,
validation, and Python logical-authoring foundation was stable enough to support it.

Projection-layer workflow is kernel-first:

1. Design the C++ kernel representation for the projection layer.
2. Add kernel mutation APIs and validation for that representation.
3. Add serialization and round-trip coverage for the kernel data.
4. Add deterministic renderers or adapters that consume the kernel model without owning
   EDA meaning.
5. Then add Python bindings and Python syntax over that kernel-owned data.

The schematic and PCB Python authoring APIs were added only after their C++ kernel models
existed. Future projection layers should follow the same kernel-first ordering: no Python
authoring API before the C++ model, mutation API, validation, and serialization for it
exist.

The current thinking is:

- Schematics should support multiple sheets.
- Schematic entities should reference existing logical entities.
- A schematic wire or visual run should represent an existing `NetId`; it should not be
  the source of electrical connectivity.
- PCB routes and copper should implement existing `NetId` values; they should not define
  the netlist.
- Schematic and PCB validators should report projection consistency issues, such as a
  missing symbol for a component, an unrouted net, or visual geometry that references the
  wrong logical net.

Detailed wire-graph auto-layout is intentionally deferred. The schematic implementation
starts with kernel-owned presentation data: typed schematic IDs, structured symbol
geometry, sheets, symbol instances that reference logical component instances, and
wire/label projection objects that reference canonical nets. The PCB layer follows the
same pattern with kernel-owned board, footprint, and copper geometry. Deterministic SVG
rendering is an output path over each model, not a source of projection truth.

## Initial Layers

```text
volt-core
  typed IDs, diagnostics, results, properties, units

volt-circuit
  component definitions, component instances, pins, nets, circuit validation

volt-authoring
  component library presets, reference allocation, and ergonomic connection helpers
  over volt-circuit

volt-schematic
  schematic projection data over logical components and canonical nets

volt-pcb
  board outline, layers, footprint placement, copper geometry, routing, and 3D geometry
  projection over canonical components and nets

volt-io
  deterministic save/load formats for logical circuits, schematic projections, and PCB
  projections

volt-adapters/kicad
  KiCad schematic and PCB export adapters over the kernel projection models

volt-python
  Python bindings and authoring syntax over kernel-owned state
```

The CMake targets mirror these boundaries:

```text
Volt::Core
  lowest-level primitives and version API

Volt::Circuit
  logical circuit model; depends on Volt::Core

Volt::Authoring
  component library, reference allocation, and ergonomic connection helpers;
  depends on Volt::Circuit

Volt::Schematic
  schematic projection model; depends on Volt::Circuit

Volt::PCB
  PCB projection model; depends on Volt::Circuit

Volt::IO
  deterministic logical circuit, schematic, and PCB projection persistence; depends on
  Volt::Circuit, Volt::Schematic, Volt::PCB, and owns JSON dependencies

Volt::Volt
  umbrella target for applications that want the full public surface
```

The KiCad export adapters and the Python bindings are separate targets that depend on the
stable kernel layers (`Volt::Circuit`, `Volt::Schematic`, `Volt::PCB`) instead of pushing
dependencies back into them. Future projection layers should follow the same rule.

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

Schematic projection entities use their own typed IDs, separate from logical circuit IDs.

## Entity Storage

The first storage primitive is `EntityTable<T, Id>`. It is vector-backed, deterministic,
and insertion ordered. IDs are typed indexes into their owning table.

The table intentionally does not support deletion, generations, tombstones, sparse
storage, or custom allocation yet. Those features only become justified when the kernel
has concrete mutation and undo/redo requirements. The append-only decision is recorded in
[Append-only kernel state](design/adr-append-only-kernel.md).

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

## Properties

Properties are extensible metadata, not replacements for typed kernel relationships.
They are useful for facts such as values, tolerances, datasheet links, lifecycle notes,
and contributor-defined annotations.

The first property layer lives in `volt-core`:

```text
PropertyKey
  value: "value"

PropertyValue
  string | boolean | integer | number

PropertyMap
  deterministic key-ordered map from PropertyKey to PropertyValue
```

Typed model fields still carry structural meaning. For example, component-to-definition
links, selected-part references, package references, footprint references, and pin/pad
mappings should not be hidden inside generic properties.

## Typed Electrical Semantics

Volt should become simulation-ready without becoming simulation-specific yet. That means
core electrical meaning must become kernel-owned typed data before ERC expands into
voltage, current, rating, and power-domain checks.

`PropertyMap` remains useful for arbitrary metadata. Electrical facts that affect
validation, persistence, Python bindings, or future simulation contracts should move
toward typed fields owned by the C++ kernel:

- quantities and units such as `330 ohm`, `3.3 V`, `100 nF`, and `0.1 W`
- design-defining component values and physical-part ratings
- pin terminal kind, direction, signal domain, drive, polarity, and connection requirement
- declared voltage domains, accepted/produced ranges, and drive behavior

Do not store every electrical value that can be derived or simulated. Runtime quantities
such as calculated current, node voltage, charge, temperature, or power dissipation belong
in future analysis results unless they are explicit design constraints.

Future ERC should consume those typed semantics instead of accumulating ad hoc checks over
labels or Python authoring preset names. The detailed trajectory is captured in
[typed-electrical-semantics.md](typed-electrical-semantics.md).

## Physical Part Selection

Logical component definitions describe electrical shape. Physical part selection
describes one manufacturable implementation of that logical shape without introducing PCB
geometry yet:

```text
ManufacturerPart
  manufacturer: "Yageo"
  part_number: "RC0603FR-07330RL"

PackageRef
  value: "0603"

FootprintRef
  library: "passives"
  name: "R_0603_1608Metric"

PinPadMapping
  pin: PinDefId(0)
  pad: "1"

PhysicalPart
  manufacturer_part
  package
  footprint
  pin_pad_mappings
  properties: {"tolerance": "1%"}
```

This layer keeps the distinction between:

- the reusable logical device definition
- the selected manufacturer part
- the selected physical package
- the referenced footprint definition
- the logical-pin to physical-pad mapping

`FootprintRef` is only a reference. Footprint geometry, pads, courtyards, layers, and
board placement remain outside the current circuit-kernel scope.

Selected physical parts are assigned to component instances through `Circuit`. The
component stores the selected value, but the mutation boundary validates that the
`PhysicalPart` pin/pad mappings cover the component's logical pin definitions and do not
reuse a physical pad label. A logical pin may map to more than one physical pad when the
selected package exposes tied pads.
This keeps invalid selected-part state out of the owning circuit database.

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
  connection_requirement: Required
  terminal_kind: Power
  direction: Input
  signal_domain: Unspecified
  drive_kind: Unspecified
  polarity: None

ComponentDefinition
  name: "Resistor"
  pins: [PinDefId(0), PinDefId(1)]
  properties: {"category": "passive"}
```

Actual component instances, concrete pin instances, and net connections are separate
concepts introduced in later sections and layers.

## Circuit Instances

`ComponentInstance` and `PinInstance` represent concrete design occurrences:

```text
ComponentInstance
  definition: ComponentDefId(0)
  reference: "R1"
  properties: {"value": "330 ohm"}

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
include unconnected pins, single-pin nets, incompatible pin electrical semantics, and power-domain
issues.

## Authoring Helpers

Authoring helpers make the logical kernel usable without changing the source of truth. The
programmatic authoring facade is specified separately in
[authoring-api.md](authoring-api.md).
They return typed IDs and route structural mutation through `Circuit`.

The first authoring helpers are deliberately small:

- data-driven component definition specs and catalog presets
- deterministic reference designator allocation from prefixes such as `R` and `D`
- component instantiation helpers that delegate to `Circuit::instantiate_component`
- connection helpers that delegate to `Circuit::connect`

`ReferenceDesignator` values and `NetName` values are unique within a `Circuit`. The
internal ID remains the engine identity, while the label remains the electronics-facing
authoring handle:

```text
component_by_reference("R1") -> ComponentId(0)
net_by_name("GND") -> NetId(2)
```

`instantiate_component` creates one `ComponentInstance` and concrete `PinInstance` values
for each ordered pin definition in the component definition. This is still logical model
authoring; it does not create schematic symbols, wires, footprints, or PCB objects.

The first implementation uses deterministic table scans instead of cached lookup indexes.
Secondary indexes can be introduced later if profiling shows the circuit authoring path
needs them.

## Logical Validation

Logical validation is the first electrical-rule-checking layer over the canonical circuit
model. It returns a `DiagnosticReport`; it does not mutate the circuit and it does not
make structurally invalid states valid.

The initial `validate_circuit` pass intentionally checks only facts represented by the
current model:

- required pins that are not connected
- pins marked no-connect or must-not-connect that are connected
- empty nets
- single-pin nets
- multiple output-like pins connected to one net

These findings are bad circuit design, not invalid kernel state. More domain-specific
ERC, such as voltage compatibility, missing pull-ups, current limiting, power-domain
checking, and component-specific rules, should be added only after the model contains the
typed kernel-owned data needed to express those checks.

## Mutation Boundary

Kernel data should be mutated through explicit operations:

- define component
- instantiate component
- create net
- connect pin to net
- disconnect pin
- validate circuit

This preserves invariants and gives future undo/redo, serialization, and Python bindings
a narrow API surface.

## Structural Error Taxonomy

Structural rejections at migrated mutation boundaries throw typed kernel errors declared
in `volt/core/errors.hpp`. A typed kernel error derives from `volt::KernelError`, which
carries a machine-readable `volt::ErrorCode` and, when one is naturally at hand, an
`EntityRef` identifying the rejected entity. Callers branch on `code()` instead of
parsing message strings.

The migration is incremental. Core entity storage, the `Circuit` aggregate root, and the
connectivity, hierarchy, electrical, net-class, parts, BOM sourcing, schematic, assembly
CPL option, KiCad adapter, and authoring boundaries throw typed kernel errors today;
validation reports design-quality findings through diagnostics, and design intent has no
subsystem-local structural throw sites beyond the root preflights. The remaining
subsystems (PCB outside the assembly CPL option boundary and IO) still throw raw
`std::logic_error`, `std::invalid_argument`, or `std::out_of_range` until their migration
lands. Until then, catching `volt::KernelError` alone does not cover every
mutation-boundary failure.

Each error also derives from the std exception type its throw site historically used:

- `KernelLogicError` derives from `std::logic_error`
- `KernelArgumentError` derives from `std::invalid_argument`
- `KernelRangeError` derives from `std::out_of_range`

so pre-existing `catch` sites and test assertions keep working during the incremental
migration.

`ErrorCode` values are families, not per-message identifiers: `UnknownEntity`,
`DuplicateName`, `CrossReferenceViolation`, `InvalidArgument`, and `InvalidState`. Add a
new code only when callers need to distinguish a failure kind, and keep existing message
text stable when migrating a throw site.

This taxonomy classifies structural errors only. Design-quality findings still flow
through diagnostics and validation, never through exceptions.
