# Volt Roadmap

Volt is building a modern electronics design kernel from the logical model outward. The
maintainer-approved backlog is the set of open GitHub issues carrying the
[`roadmap` label](https://github.com/RyanWillie/Volt/issues?q=is%3Aissue%20state%3Aopen%20label%3Aroadmap).
Autonomous agents may select or start only issues that also carry `ready` and have no
open native GitHub blockers, unless the maintainer explicitly directs otherwise. This
document explains product direction; labelled issues own current scope, status,
dependencies, and acceptance criteria.

## Recently Completed Foundation

The current kernel foundation includes:

- typed entity IDs and deterministic entity storage
- logical component and pin definitions
- component instances and concrete pin instances
- canonical nets and circuit-wide connectivity invariants
- logical validation diagnostics
- named validation entry points for general, connectivity, ERC, and PCB readiness
- property metadata
- typed quantities and electrical attributes
- unified pin electrical semantics
- typed net voltage and pin voltage-range checks
- physical part selection values and selected-part mutation API
- logical pin to physical pad mapping validation
- selected-part voltage rating diagnostics
- LED logical circuit example with selected physical parts
- logical hierarchy primitives: module definitions, module instances, ports, and template
  nets
- explicit no-connect assertions as stored logical design intent
- net classes for reusable net design intent
- schematic projection layer: kernel-owned sheets, symbols, wires, and labels over
  canonical nets, schematic readability/consistency validation, and deterministic SVG
  rendering
- PCB projection layer: board outline, layers, footprint placement, copper geometry and
  routing, board 3D geometry projection, and deterministic PCB SVG output
- KiCad export adapters for schematic and PCB projections, with a structured loss report
- deterministic JSON writers and structural readers for the logical circuit, schematic,
  and PCB projections
- Python authoring bindings over kernel-owned logical, schematic, and PCB state
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
layers build on it. The next kernel work should add only first-principles primitives that
make circuits more electrically meaningful without forcing verbosity on simple designs.

Completed since the original plan:

- minimal logical hierarchy: module/block definitions, module instances, ports, and
  template nets (see [hierarchy-scoped-net-design.md](docs/hierarchy-scoped-net-design.md));
- net classes: named logical/electrical design intent assigned to nets,
  initially limited to constraints Volt can validate soon.

Remaining logical primitives:

- scoped net naming: global nets, module-local nets, port-bound nets, and explicit
  aliases/conflicts without making names internal identity;
- generic net bundles/interfaces: ordered or named groups of existing `NetId`s for buses,
  protocols, connectors, and repeated signal groups;
- expand net-class constraints toward voltage, source requirement, current limits, and
  selected-part rating requirements.

The authoring facade should remain a convenience layer over `Circuit`; it must not become
a second source of truth. Protocol-specific helpers such as SPI, I2C, USB, memory buses,
or motor channels should live above generic module, port, net, bundle, and constraint
primitives.

### Typed Electrical Semantics

Goal: make electrical meaning typed and kernel-owned before expanding ERC.

Completed foundation:

- quantities, units, ranges, and tolerances
- typed electrical attributes with owner and dimension checks
- unified pin electrical semantics over `PinDefinition`
- typed net voltage and pin voltage-range validation
- selected-part electrical attributes and voltage-rating diagnostics
- validation entry points for general, connectivity, ERC, and PCB readiness
- deterministic serialization of typed electrical data
- Python authoring syntax over kernel-owned typed values

Remaining work:

- current limits and power capability attributes/checks
- selected-part compatibility diagnostics beyond voltage-rating checks
- richer drive/domain compatibility after the general constraint model is clearer

This is the foundation for richer ERC and future simulation readiness. See
[typed-electrical-semantics.md](docs/typed-electrical-semantics.md).

### Logical ERC And Constraints

Goal: expand validation while keeping structural invariants in the kernel mutation API.
Validation should consume general kernel-owned design intent rather than inventing
feature-specific checks that cannot compose.

Planned work:

- validate selected part compatibility beyond voltage rating as diagnostics where
  appropriate
- validate scoped-net and hierarchy issues once those primitives exist
- add typed-semantics-aware current, power, domain, and drive-compatibility checks

Do not expand ERC as ad hoc checks over today's broad `PinRole` model. Prefer typed
attributes, net classes, constraints, and explicit design-intent entities that can be
serialized and inspected.

### Part Libraries And BOM

Goal: make parts declarative, trustworthy, and reusable across designs, with the kernel
owning part meaning (see [part-library-design.md](docs/part-library-design.md)).

Planned work:

- kernel-owned part artifact: canonical serialization, loader, and content hashing
- lineup-contract diagnostics validating symbol and footprint projections against the
  part pin map
- library build lifecycle: standalone `.voltlib` bundles, in-project source builds
  through the same pipeline, and design-manifest lock entries
- BOM projection over selected parts with DNP, per-instance overrides, and approved
  alternates, plus a BOM-readiness validation entry point
- reviewed package generators, part provenance, verification tiers, and design-side
  sourcing snapshots

### Python Authoring Refinements

Goal: keep Python expressive while preserving the C++ kernel as the owner of EDA meaning.

Completed:

- planned and implemented the Python bindings boundary
- Python logical, schematic, and PCB authoring over kernel-owned state
- typed quantity/value authoring over the kernel electrical-semantics model

Remaining work:

- continue refining authoring ergonomics as new kernel primitives land
- keep Python-authored simulation behavior behind kernel-owned model contracts when that
  layer is eventually designed

### Schematic Projection And Rendering

Goal: add schematic views over the canonical logical circuit without making schematic
wires the source of electrical truth.

Completed:

- C++ schematic kernel model vocabulary, including sheets, symbol definitions, and symbol
  instances over canonical components, pins, and nets
- schematic projection data (wires, labels, notes) that references canonical `NetId`s
  without owning connectivity
- schematic readability and consistency diagnostics with serialization round-trip coverage
- Python schematic drawing bindings/syntax over kernel-owned schematic state
- a deterministic schematic SVG renderer/export

Remaining work:

- represent schematic projection data over future port and bundle primitives once those
  logical primitives exist
- richer auto-layout and wire-graph tooling

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

Goal: physically implement existing logical connectivity without letting the board layer
define the netlist.

Completed:

- PCB kernel architecture and board model: outline, layers, footprint placement, copper
  geometry, and routing
- footprint geometry primitives
- board 3D geometry projection
- deterministic PCB JSON persistence and PCB SVG output
- KiCad PCB export adapter with a structured loss report
- Python PCB layout authoring over kernel-owned board state

Remaining work:

- richer DRC and PCB-readiness validation as the typed-semantics and constraint model grows
- additional footprint library coverage and conventions

### Serialization And Round-Trip Loading

Goal: make each kernel-owned model layer deterministic, inspectable, and portable.

Completed work includes the v1 logical circuit format spec, deterministic writer,
structural reader, schema compatibility policy, and golden fixtures. Future work should
add typed electrical semantics, projection data, and migrations only after the owning C++
model exists.

## Design Rules For Roadmap Work

- Keep invalid kernel state impossible.
- Report bad design intent through diagnostics.
- Build from first-principles EDA primitives, not feature mimicry.
- Prefer general primitives over use-case-specific kernel objects.
- Use progressive disclosure: simple circuits stay concise; advanced correctness is opt-in.
- Keep authoring conveniences separate from the canonical model.
- Keep Python as syntax over kernel-owned meaning.
- Prefer deterministic output and stable tests.
- Avoid speculative abstractions until a concrete validation, serialization, import/export,
  or projection need justifies them.
