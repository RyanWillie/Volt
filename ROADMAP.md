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
- named validation entry points for general, connectivity, ERC, and PCB readiness
- property metadata
- typed quantities and electrical attributes
- unified pin electrical semantics
- typed net voltage and pin voltage-range checks
- physical part selection values and selected-part mutation API
- logical pin to physical pad mapping validation
- selected-part voltage rating diagnostics
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
layers build on it. The next kernel work should add only first-principles primitives that
make circuits more electrically meaningful without forcing verbosity on simple designs.

Immediate work should refresh the docs and design the next logical primitive before
starting a large model expansion:

1. Update documentation to reflect the typed electrical semantics foundation that has
   landed.
2. Write a dedicated hierarchy/scoped-net design page.
3. Only then implement the smallest hierarchy/scoped-net vertical slice.

Planned logical primitives, after that design step:

- minimal logical hierarchy: module/block definitions, module instances, ports, local
  nets, and hierarchical paths;
- scoped net naming: global nets, module-local nets, port-bound nets, and explicit
  aliases/conflicts without making names internal identity;
- generic net bundles/interfaces: ordered or named groups of existing `NetId`s for buses,
  protocols, connectors, and repeated signal groups;
- netclasses/rule classes: named logical/electrical design intent assigned to nets or
  bundles, initially limited to constraints Volt can validate soon such as voltage,
  source requirement, current limits, and selected-part rating requirements.

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
- no-connect assertions as explicit stored design intent
- selected-part compatibility diagnostics beyond voltage-rating checks
- netclasses/rule classes for reusable logical/electrical constraints
- richer drive/domain compatibility after the general constraint model is clearer

This is the foundation for richer ERC and future simulation readiness. See
[typed-electrical-semantics.md](docs/typed-electrical-semantics.md).

### Logical ERC And Constraints

Goal: expand validation while keeping structural invariants in the kernel mutation API.
Validation should consume general kernel-owned design intent rather than inventing
feature-specific checks that cannot compose.

Planned work:

- add no-connect assertions as stored design intent, distinct from no-connect pin roles
- validate selected part compatibility beyond voltage rating as diagnostics where
  appropriate
- validate scoped-net and hierarchy issues once those primitives exist
- add typed-semantics-aware current, power, domain, and drive-compatibility checks
- add netclass/rule-class validation once rule classes exist, limited at first to
  logical/electrical constraints Volt can validate without PCB architecture

Do not expand ERC as ad hoc checks over today's broad `PinRole` model. Prefer typed
attributes, netclasses, constraints, and explicit design-intent entities that can be
serialized and inspected.

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

- defer implementation until logical hierarchy, scoped nets, and generic bundles/rules
  have a stable design
- design the C++ schematic kernel model vocabulary
- add C++ symbol definition and instance model
- represent schematic projection data over canonical components, pins, ports, bundles,
  and nets in the kernel
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
- Build from first-principles EDA primitives, not feature mimicry.
- Prefer general primitives over use-case-specific kernel objects.
- Use progressive disclosure: simple circuits stay concise; advanced correctness is opt-in.
- Keep authoring conveniences separate from the canonical model.
- Keep Python as syntax over kernel-owned meaning.
- Prefer deterministic output and stable tests.
- Avoid speculative abstractions until a concrete validation, serialization, import/export,
  or projection need justifies them.
