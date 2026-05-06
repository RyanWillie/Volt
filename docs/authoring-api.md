# SKiDL-Style Authoring API Design

Volt's SKiDL-style authoring layer should make circuit construction concise without
becoming the logical kernel. The authoring layer is a convenience facade over `Circuit`;
`Circuit` remains the canonical source of truth and the only owner of structural
invariants.

## Goals

- Let users define parts, instantiate components, connect nets, and attach metadata with
  minimal boilerplate.
- Keep invalid kernel state impossible by routing all structural mutation through
  `Circuit` APIs.
- Preserve deterministic output: authoring order maps to kernel insertion order unless an
  API explicitly says otherwise.
- Make the future Python API feel natural while keeping the C++ kernel small and stable.

## Non-Goals

- The authoring layer is not a second circuit database.
- Authoring handles are not storage IDs and must not outlive or bypass their owning
  authoring session.
- The first authoring layer does not model schematics, symbol placement, PCB geometry, or
  footprint pads beyond `FootprintRef` and `PinPadMapping` values already accepted by the
  kernel.

## Layer Boundary

```text
user code / Python bindings
        │
        ▼
SKiDL-style authoring facade
  - ergonomic builders
  - reference designator allocation
  - pin/net lookup conveniences
  - short-lived handles
        │ calls public Circuit APIs
        ▼
Circuit kernel
  - owns entity tables
  - validates mutation boundaries
  - returns typed IDs and const views
        │
        ▼
validation and diagnostics
```

The facade may cache names or keep builder-local handles, but committed design state lives
in `Circuit`. If facade state and kernel state disagree, the kernel wins.

## Core Concepts

### Authoring Session

An authoring session owns or references one `Circuit` and provides convenience methods:

```cpp
volt::authoring::Design design;

auto led = design.part("LED")
    .pin("A", "1", PinRole::Passive)
    .pin("K", "2", PinRole::Passive);

auto d1 = design.instantiate(led, "D1");
auto gnd = design.net("GND", NetKind::Ground);
gnd.connect(d1.pin("K"));
```

`Design` can expose `const Circuit &circuit() const` for validation, serialization, and
inspection. Mutable access to entity tables is not exposed.

### Part Builder

A part builder creates reusable logical definitions:

- `pin(name, number, role, requirement)` creates `PinDefinition` values.
- `property(key, value)` attaches component-definition metadata.
- `commit()` or first use stores the definition with `Circuit::add_component_definition`.

The builder returns a lightweight authoring `PartRef` that identifies the resulting
`ComponentDefId`. The builder should not permit editing a committed definition in place;
changing a part shape creates a new definition or is deferred until a deliberate kernel
mutation API exists.

### Component Handle

A component handle wraps a `ComponentId` and routes changes through `Circuit`:

- `set_property(key, value)` calls `Circuit::set_component_property`.
- `select_part(PhysicalPart)` calls `Circuit::select_physical_part`.
- `pin(name)` and `pin_number(number)` call kernel lookup helpers and return pin handles.

The handle never returns mutable `ComponentInstance &`.

### Net Handle

A net handle wraps a `NetId`:

- `connect(pin)` calls `Circuit::connect`.
- `disconnect(pin)` calls `Circuit::disconnect`.
- Reconnecting a pin to a different net should surface the kernel `logic_error` rather
  than silently moving the pin.

Convenience overloads may accept multiple pins:

```cpp
design.net("LED_A").connect(r1.pin("2"), d1.pin("A"));
```

These overloads are transactions only if explicitly designed that way. The first version
can be sequential and fail-fast, matching repeated `Circuit::connect` calls.

### Reference Designator Allocation

The facade may allocate references such as `R1`, `R2`, and `D1` from prefixes. Allocation
is convenience only:

- Explicit references are passed to `Circuit::instantiate_component` and remain unique by
  existing kernel checks.
- Auto-allocation should consult `Circuit::component_by_reference` before selecting a
  label.
- Collisions are authoring errors surfaced before or during kernel mutation.

The current low-level helper surface exposes this as:

```cpp
const auto r1 = volt::authoring::instantiate(circuit, resistor_definition, "R");
const auto r10 = volt::authoring::instantiate(
    circuit, resistor_definition, volt::ReferenceDesignator{"R10"});
```

The prefix overload allocates the first unused deterministic label, while the explicit
overload preserves user-provided references and relies on `Circuit` to reject duplicates.

## Error and Diagnostic Flow

Structural errors are exceptions from kernel mutation boundaries. Examples:

- missing component, pin, or net IDs
- duplicate reference designators or net names
- pin connected to more than one net
- selected physical part mappings that do not match the logical component definition

The authoring facade may add context to these exceptions, but it must not convert them
into diagnostics or continue with partial invalid state.

Design-quality findings remain diagnostics from validation passes:

- unconnected required pins
- single-pin nets
- incompatible driver roles
- future selected-part completeness or metadata compatibility checks

Recommended flow:

```cpp
auto design = volt::authoring::Design{};
// build circuit; structural mistakes throw immediately
const auto report = volt::validate_circuit(design.circuit());
// user decides whether warnings/errors are acceptable
```

Python bindings should map structural exceptions to Python exceptions and expose
`DiagnosticReport` for validation results.

## Relationship To Serialization

Serialization should read and write `Circuit`, not authoring facade internals. Authoring
objects may be recreated from loaded circuits only as lookup conveniences over the loaded
kernel model.

## First MVP Surface

A minimal useful authoring layer can be built from existing kernel APIs:

1. `Design` owning a `Circuit`.
2. `part()` builder for `PinDefinition` and `ComponentDefinition` creation.
3. `instantiate()` with explicit reference designators.
4. `net()` creation and lookup by `NetName`.
5. `ComponentHandle::pin()` / `pin_number()` lookup helpers.
6. `NetHandle::connect()` convenience overloads.
7. `ComponentHandle::set_property()` and `select_part()` delegating to `Circuit`.

The current foundation already includes data-driven component definition specs,
deterministic reference allocation, component instantiation helpers, and net connection
helpers as free functions. A richer `Design` facade can build on those helpers without
becoming a second source of truth.
