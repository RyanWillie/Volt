# Volt Roadmap

Volt is building a modern electronics design kernel from the logical model outward. The
roadmap is tracked in Pebble; this document summarizes the current milestone structure for
contributors.

## Recently Completed Foundation

The current kernel foundation includes:

- typed entity IDs and deterministic entity storage
- logical component and pin definitions
- component instances and concrete pin instances
- canonical nets and circuit-wide connectivity invariants
- logical validation diagnostics
- property metadata
- physical part selection values and selected-part mutation API
- logical pin to physical pad mapping validation
- LED logical circuit example with selected physical parts
- deterministic logical circuit JSON writer and structural reader
- golden round-trip fixtures
- schema versioning policy
- cross-platform CI for formatting, build, tests, and docs

## Active Milestones

### Open-Source Readiness

Goal: make the repository straightforward for contributors and downstream users.

Remaining work:

- publish generated API docs
- continue improving packaging and release ergonomics

### Programmatic Authoring Layer

Goal: provide an ergonomic logical-circuit authoring layer without weakening kernel
invariants.

Planned work:

- add a minimal component factory/library layer
- add reference designator allocation
- add ergonomic net connection helpers
- plan the Python bindings boundary
- implement a first Python logical-authoring MVP

The authoring facade should remain a convenience layer over `Circuit`; it must not become
a second source of truth.

### Serialization And Round-Trip Loading

Goal: make the logical circuit model deterministic, inspectable, and portable.

Completed work includes the v1 format spec, deterministic writer, structural reader,
schema compatibility policy, and golden fixtures. Future work may add migrations and more
fixture coverage as the model grows.

### Logical ERC And Constraints

Goal: expand validation while keeping structural invariants in the kernel mutation API.

Planned work:

- create a diagnostic code catalog
- validate selected part compatibility as diagnostics where appropriate
- add basic power and ground sanity checks
- add metadata-aware ERC checks
- add validation pass composition API

### Schematic Projection And Rendering

Goal: add schematic views over the canonical logical circuit without making schematic
wires the source of electrical truth.

Planned work:

- design the C++ schematic kernel model vocabulary
- add C++ symbol definition and instance model
- represent schematic projection data over canonical nets in the kernel
- add schematic consistency diagnostics and serialization coverage
- then add Python schematic drawing bindings/syntax
- then add a first simple schematic renderer/export

### PCB Foundation

Goal: defer board-specific modeling until logical and schematic foundations are stable.

Planned work:

- draft PCB kernel architecture
- design footprint geometry primitives
- implement an initial board model

## Design Rules For Roadmap Work

- Keep invalid kernel state impossible.
- Report bad design intent through diagnostics.
- Keep authoring conveniences separate from the canonical model.
- Prefer deterministic output and stable tests.
- Avoid speculative abstractions until the next layer needs them.
