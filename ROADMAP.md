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

### Logical Circuit Kernel

Goal: finish the canonical logical circuit foundation before projection or simulation
layers build on it.

Planned work:

- add a minimal component factory/library layer
- add reference designator allocation
- add ergonomic net connection helpers

The authoring facade should remain a convenience layer over `Circuit`; it must not become
a second source of truth.

### Typed Electrical Semantics

Goal: make electrical meaning typed and kernel-owned before expanding ERC.

Planned work:

- design quantities, units, ranges, and tolerances
- move common component values and ratings toward typed fields
- define richer pin electrical specs beyond the current broad `PinRole`
- keep the circuit model limited to design-defining inputs and constraints
- update serialization once the C++ model exists
- define natural Python authoring syntax over typed kernel-owned values

This is the required foundation for richer ERC and future simulation readiness. See
[typed-electrical-semantics.md](docs/typed-electrical-semantics.md).

### Logical ERC And Constraints

Goal: expand validation while keeping structural invariants in the kernel mutation API.

Planned work:

- create a diagnostic code catalog
- validate selected part compatibility as diagnostics where appropriate
- add typed-semantics-aware power and ground sanity checks
- add rating, domain, and drive-compatibility checks after typed semantics exist
- add validation pass composition API

Do not expand ERC as ad hoc checks over today's broad `PinRole` model. Basic
power/ground checks should wait until the typed electrical semantics design lands.

### Python Authoring Refinements

Goal: keep Python expressive while preserving the C++ kernel as the owner of EDA meaning.

Planned work:

- plan the Python bindings boundary
- implement and refine the first Python logical-authoring MVP
- add typed quantity/value authoring once the kernel model exists
- keep Python-authored simulation behavior behind kernel-owned model contracts when that
  layer is eventually designed

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

### Simulation Foundation

Goal: become simulation-ready without choosing a simulation-specific architecture yet.

Planned work:

- define kernel-owned simulation model contracts after typed electrical semantics and ERC
  are stable
- define units, state/result data, scheduling semantics, and validation boundaries before
  any backend work
- treat SPICE as a possible future backend/export adapter, not Volt's canonical
  simulation architecture

No simulation engine, SPICE integration, or solver API is planned before those contracts
exist.

### PCB Foundation

Goal: defer board-specific modeling until logical and schematic foundations are stable.

Planned work:

- draft PCB kernel architecture
- design footprint geometry primitives
- implement an initial board model

### Serialization And Round-Trip Loading

Goal: make each kernel-owned model layer deterministic, inspectable, and portable.

Completed work includes the v1 logical circuit format spec, deterministic writer,
structural reader, schema compatibility policy, and golden fixtures. Future work should
add typed electrical semantics, projection data, and migrations only after the owning C++
model exists.

## Design Rules For Roadmap Work

- Keep invalid kernel state impossible.
- Report bad design intent through diagnostics.
- Keep authoring conveniences separate from the canonical model.
- Prefer deterministic output and stable tests.
- Avoid speculative abstractions until the next layer needs them.
