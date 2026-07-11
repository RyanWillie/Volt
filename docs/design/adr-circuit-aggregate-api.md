# ADR: Typed Circuit Aggregate API

Status: accepted

Issue: [#261](https://github.com/RyanWillie/Volt/issues/261)

## Decision

`Circuit` remains Volt's sole canonical logical aggregate. Its public API will be
replaced with a small domain-shaped surface built from complete typed specifications,
irreducible graph operations, typed progressive updates, generic typed reads, and free
derived queries.

The current borrow-only mutation facades are transitional and frozen. They may remain at
existing call sites until their scheduled migration, but must not gain new methods or be
treated as the target architecture.

The intended public shape is approximately fifteen declarations:

```cpp
class Circuit final {
  public:
    ComponentDefId define_component(ComponentSpec spec);
    ModuleDefId define_module(ModuleSpec spec);
    NetClassId define_net_class(NetClassSpec spec);

    ComponentId instantiate_component(ComponentDefId definition,
                                      ComponentInstanceSpec spec);
    ModuleInstanceId instantiate_module(ModuleDefId definition,
                                        ModuleInstanceSpec spec);
    NetId add_net(NetSpec spec);

    bool connect(NetId net, PinId pin);
    bool disconnect(PinId pin);
    PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                            NetId parent_net);

    void update(ComponentId component, ComponentUpdate update);
    void update(NetId net, NetUpdate update);
    void mark_no_connect(PinId pin);

    template <typename Id>
        requires CircuitEntityId<Id>
    [[nodiscard]] const entity_type_t<Id> &get(Id id) const;

    template <typename Id>
        requires CircuitEntityId<Id>
    [[nodiscard]] entity_range_t<Id> all() const;

    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const;
};
```

Names may be refined while implementing the contract, but the categories and public API
budget are architectural requirements. A new root method is admitted only when it is an
irreducible structural operation that cannot be expressed through a complete typed spec,
a typed update, generic `get`/`all`, or a free query.

## Ownership

`Circuit` owns:

- component definitions and component instances;
- component-owned pin definitions and concrete pin instances;
- nets and pin-to-net membership;
- module definitions, instances, ports, bindings, and expansion provenance;
- typed component, pin-definition, selected-part, and net electrical meaning;
- selected physical parts and assembly intent;
- explicit no-connect and intentional-stub intent;
- net-class definitions and assignments.

Schematic and PCB models reference this logical truth but do not create, merge, split, or
reinterpret nets. Validation and analysis consume `const Circuit&`. Python and importers
lower syntax or external data into the same typed kernel operations.

Private storage may remain decomposed into connectivity, hierarchy, electrical, intent,
and net-class state. That decomposition is an implementation detail and does not create
public subsystem owners.

## Complete Definitions

`ComponentSpec` carries the complete reusable definition: name, component properties,
provenance, schematic symbol references, ordered pin definitions, and each pin's typed
electrical attributes. A successful `define_component` preflights and commits the whole
definition atomically.

A committed component definition is immutable. Changing its pin shape or pin electrical
meaning creates a new definition; there is no public post-commit pin-definition setter.
`PinDefId` remains canonical internal identity for concrete pins, module templates,
selected-part mapping, validation, and persistence. The authoring shape does not require
the logical JSON shape to change: complete pin values can still lower deterministically
into the existing top-level pin-definition table.

`ModuleSpec` similarly carries its complete template nets, ports, component occurrences,
and internal connections. The kernel preflights the complete module before committing any
entity.

## Typed Progressive Updates

Facts naturally discovered after creation use closed typed update values rather than one
root setter per field. The expected forms are equivalent to:

```cpp
using ComponentUpdate = std::variant<
    SetComponentProperty,
    SetComponentElectricalAttribute,
    SelectPhysicalPart,
    SetSelectedPartElectricalAttribute,
    SetAssemblyIntent>;

using NetUpdate = std::variant<
    SetNetElectricalAttribute,
    AssignNetClass,
    MarkIntentionalStub>;
```

Each alternative is a strong domain type with typed values and invariant checks. This is
not a string property bag, generic command bus, or `EntityRef` mutation surface. Adding a
new alternative requires kernel-owned meaning, persistence, inspection, and validation
semantics.

No per-instance `PinId` electrical-attribute model is introduced by this ADR.

## Reads And Queries

`get(Id)` provides invalid-ID-checking access to a canonical entity. `all<Id>()` provides
deterministic range-for iteration and `size()` without a separate getter and count method
for every table. `net_of(PinId)` remains a fundamental relationship primitive.

Lookup by name/reference, pin lookup, hierarchy traversal, origin lookup, net-class
resolution, adjacency, and other derived reads remain free functions in `volt::queries`
over `const Circuit&`. They return `optional` when absence is normal. They must not require
public `ConnectivityModel` or `HierarchyModel` accessors. An indexed internal
implementation may be retained where measurement justifies it without exposing storage.

## Error Contract

- Unknown or foreign IDs, dangling references, duplicate unique names, invalid
  ownership, invalid selected-part mapping, and mutations that would create partial or
  inconsistent structure throw typed Volt exceptions at the boundary.
- `connect(net, pin)` returns `false` only when the pin is already on that same net; it
  throws when the pin is on a different net or either ID is invalid.
- `disconnect(pin)` returns `false` only when a valid pin is already disconnected.
- `get(id)` throws the approved unknown-entity typed error for an invalid ID.
- `all<Id>()` is deterministic and does not represent absence as an error.
- `net_of(pin)` throws for an invalid pin and returns `nullopt` for a valid unconnected
  pin.
- Free name/reference queries return `optional`; authoring handles may translate absence
  into a contextual typed error.

Exact error codes and compatibility-sensitive messages are inventoried and locked before
each legacy entry point is replaced.

## Persistence And Restoration

The logical writer continues to serialize canonical kernel state deterministically. The
reader may use a private restoration interface to preserve document-local identities and
relationships, but raw `add_component`, `add_pin`, and root-instance restoration are not
general public authoring operations.

API compression must not imply model compression. Pin semantics, quantities, tolerances,
selected-part ratings, pin-pad mappings, assembly intent, net classes, no-connect/stub
intent, hierarchy, provenance, and their diagnostics remain loadable, serializable, and
inspectable.

## Migration

The native GitHub dependency graph under #261 is the execution source of truth:

1. [#234](https://github.com/RyanWillie/Volt/issues/234) fixes the live facade lifetime
   hazard without beginning the redesign.
2. [#262](https://github.com/RyanWillie/Volt/issues/262) locks this contract and the
   transition policy.
3. [#236](https://github.com/RyanWillie/Volt/issues/236) adds complete typed specs and
   failure-atomic construction.
4. [#263](https://github.com/RyanWillie/Volt/issues/263) adds generic reads and removes
   public query-model leakage.
5. [#264](https://github.com/RyanWillie/Volt/issues/264) migrates C++ adapters and tests
   incrementally.
6. [#265](https://github.com/RyanWillie/Volt/issues/265) migrates Python bindings after
   [#238](https://github.com/RyanWillie/Volt/issues/238).
7. [#266](https://github.com/RyanWillie/Volt/issues/266) deletes the facades and replaces
   architecture enforcement.
8. [#267](https://github.com/RyanWillie/Volt/issues/267) proves semantic, persistence,
   diagnostic, Python, and CI parity.

The old and new APIs may coexist only as a time-bounded migration mechanism. Tests move
incrementally from the first implementation phase so facade deletion is not a thousand-
call-site big bang.

## Consequences

- The C++ API will break before 1.0; Volt does not retain compatibility shims after the
  migration completes.
- The current Python authoring surface is preserved unless separately reviewed.
- Internal state remains free to evolve without creating a new public facade.
- The root API stays small without replacing clarity with a generic stringly mechanism.
- Some operations require new typed spec/update types, but those types make electrical
  meaning explicit, persistable, and testable.
- VOL-268 remains useful history: it proved that grouping storage operations behind
  borrowed forwarding facades did not solve the public-model problem and introduced a
  lifetime hazard.

## Non-Goals

- Entity deletion, erasure, undo, or mutable document-database semantics.
- A generic command bus, ECS, repository layer, event-sourced kernel, or plugin system.
- A generic `EntityRef` traversal or mutation API.
- Moving logical meaning into Python, schematic, PCB, importers, or UI.
- Redesigning the Python project/CLI experience in this migration.
- Changing electrical diagnostic policy or removing rich semantic data.

## Revisit Trigger

Revisit this decision only when a concrete, measured requirement cannot be represented by
complete specs, typed updates, generic reads, or free queries without violating a kernel
invariant or creating unacceptable performance. Method-count preference alone is not a
reason to add an untyped escape hatch.

