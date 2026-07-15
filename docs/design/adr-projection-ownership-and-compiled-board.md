# ADR: Projection Ownership and CompiledBoard

Status: accepted

Issue: [#292](https://github.com/RyanWillie/Volt/issues/292)

## Decision

Volt retains three distinct kernel-owned model aggregates. `Circuit` is logical truth,
`Schematic` is a presentation projection, and each named `Board` is one physical
implementation of the same logical design. A projection may reference another owner's
identity, but it cannot acquire that owner's mutation authority.

`Schematic` and `Board` expose bounded owner-shaped roots. Operations that do not mutate
owner state belong in typed C++ queries or services. Stateful algorithms such as routing may
remain focused service objects and may mutate their owner only through its public mutation
boundary.

Board compilation is explicit. One successful compilation of exactly one named authoring
`Board` produces one complete, immutable `CompiledBoard`. The artifact owns everything needed
by physical validation and downstream Board consumers; it does not borrow a `Circuit`,
`Board`, resolver, Python object, or ambient library.

## Ownership Boundaries

`Circuit` remains the sole owner of:

- component definitions and instances, pins, nets, and pin-to-net membership;
- hierarchy and logical design intent;
- exact selected-part identity, assembly intent, and logical electrical meaning; and
- no-connect and intentional-stub intent, net classes, and other logical rules consumed by
  projections.

`Schematic` owns sheets, regions, resolved symbol presentation snapshots and placements, wires,
labels, fields, junctions, ports, no-connect presentation, and other schematic presentation
data. It may arrange and annotate existing logical entities and connectivity. The canonical
default symbol remains owned by `ComponentDefinition`, and an exact override remains owned by
`PartDefinition`; snapshotting either for presentation does not transfer ownership to
Schematic. Schematic must not create, merge, split, rename, connect, disconnect, or reinterpret
logical nets or pins; select parts or assembly intent; or create physical Board meaning.

`Board` owns its immutable name, layer stack, outline, physical rules, capability intent and
configuration, placements, tracks, vias, zones, keepouts, rooms, text, and other fabrication
geometry. It may physically implement existing logical entities and connectivity. It must not
create, merge, split, rename, connect, disconnect, or reinterpret logical nets or pins; change
selected parts, pin/package meaning, or assembly intent; or create schematic presentation
meaning.

The projections hold an explicit read-only logical dependency while authoring. `Schematic`
and `Board` are siblings: neither owns, contains, or compiles the other. In particular,
Schematic data is never an input to Board compilation and never affects a `CompiledBoard`
identity or provenance digest.

The append-only kernel decision continues to apply to authoring aggregates. Explicit typed
updates and moves may change owner-controlled payloads, but no aggregate gains entity removal,
erasure, deletion, ID recycling, generic mutation handles, or `EntityRef` traversal.

## Root APIs And Services

The accepted `Circuit` boundary remains unchanged. The bounded public vocabulary for a
projection root is limited to:

- construction with an explicit `const Circuit&` and owner identity where applicable;
- immutable identity, units, and logical-owner reads;
- complete typed additions for irreducible owner entities;
- closed typed configuration, update, and move operations over owner state; and
- generic typed `get` and lvalue-only `all` reads over canonical owner entities.

This ADR fixes those categories, not the illustrative declaration counts or signatures in the
architecture reference. Later Schematic and Board API ADRs may choose compact exact types and
names within these categories; adding another responsibility requires revisiting this
boundary.

The roots do not expose borrowed subsystem facades, per-table getter/count families, public
restoration mechanics, storage-shaped index counters, name/reference lookup families,
endpoint or route-request sugar, generic command buses, or stringly/property-bag mutation.

Typed C++ queries and services own non-root work, including:

- name and reference lookup, endpoint resolution, derived geometry, pad resolution, ratsnest,
  and other derived reads;
- routing, spatial indexing, validation, diagnostics, rendering, reports, and export;
- owner-local deterministic codecs; and
- Board compilation and selected part/asset closure verification.

A query consumes a `const` owner and cannot mutate it. A mutating service has a named domain
contract and commits through the owner's public API; private storage access does not create a
second owner. Python and UI helpers may wrap these interfaces ergonomically but must not own
hidden EDA meaning or provide an alternate semantic path.

## Multiple Named Boards

One Design exposes a collection of zero or more `Board` values over its one `Circuit`.
`BoardName` is an immutable, non-empty, case-sensitive identity value, unique within that
Design and persisted without display or path normalization. A filename slug or collection
ordinal is never Board identity. Renaming is a source change that creates a new Board on the
next build, not an in-place kernel mutation. Empty or duplicate names reject at the authoring
or load boundary.

Every v1 Board is a complete alternative physical implementation of the Design's full logical
and assembly intent. Boards are not simultaneous assembly partitions, sub-boards, harness
members, or per-Board populations. One Board cannot override another Board or alter shared
selected-part, DNP, BOM, or connectivity meaning. Missing placement, routing, or other physical
coverage remains visible through diagnostics; "complete alternative" describes ownership and
cardinality, not automatic manufacturing readiness.

Core compilation and Board-dependent services select exactly one Board by its typed,
Design-scoped name. A high-level convenience may omit the name only when exactly one Board is
available; zero or multiple candidates produce a typed selection error. There is no persistent
implicit default Board.

Authoring and bundle APIs expose exact named lookup for every Board. Enumeration and canonical
persistence use ascending unsigned UTF-8 byte order of the exact case-sensitive `BoardName`.
Each Board document persists its name, and every compiled or derived Board artifact records the
same source Board identity. Ordinal position never carries identity.

## CompiledBoard Contract

`CompiledBoard` is a standalone, immutable historical artifact. Its identity is the pair of:

- the persisted Design-scoped source Board identity; and
- a deterministic provenance digest over its complete compilation inputs and compilation
  contract.

Zero or more historical `CompiledBoard` values may coexist for one named Board. Recompiling
identical canonical inputs with the same compilation contract produces the same provenance
identity and byte-equivalent canonical artifact. Recompiling changed inputs produces a new
artifact; it never mutates or deletes an earlier one.

Compilation has exactly four semantic inputs:

1. the canonical logical `Circuit` truth;
2. exactly one authoring `Board` bound to that same Circuit;
3. an immutable, content-addressed selected part/asset closure; and
4. explicit typed compilation capabilities.

Compiler and schema identity are versioned provenance, not a fifth ambient input. The
provenance digest uses a domain-separated, versioned canonical content-hash encoding and covers
the exact logical dependency-snapshot digest, source Board name and physical snapshot digest,
every selected definition and asset digest, the capability digest, and the compiler/contract
version. Any semantic change to one of those values changes the provenance digest. Wall-clock
time, random values, cache paths, process state, and incidental iteration order are not
provenance inputs.

The selected closure starts from exact parts selected by logical instances and contains only
their required transitive component contracts, package-terminal and footprint-pad mappings,
footprint definitions, and actual asset bytes required by the declared capabilities. Every
placed footprint is resolved and embedded. The closure is neither a whole ambient library nor
a set of unresolved references. A built-in asset is admissible only when it has first been
materialized into this explicit closure with identity, bytes, digest, and provenance.

Compilation capabilities are one required, closed v1 record. It contains a concrete,
digest-bearing `BoardCapabilityProfile` snapshot and an explicit set of additional asset
capabilities. The caller must supply the record; there is no ambient or overload default. An
empty additional set means the mandatory baseline, not "no capabilities."

The mandatory baseline supports standalone pad resolution, physical geometry, ratsnest,
routing-rule inspection, DRC, SVG/rendering, BOM, CPL, KiCad PCB, and native fabrication
consumers. It always embeds exact selected-part identity, package-terminal and footprint-pad
mappings, footprint definitions, and required footprint geometry. The only additional v1 asset
capability is `models3d`, which requires referenced 3D model bytes, digests, placement
transforms, and provenance. It does not admit Schematic or simulation inputs.

Board owns the authored physical-profile intent; this explicit compile input is its resolved
immutable capability snapshot. A declared Board profile identity and the supplied snapshot
must match. Process globals, Python caches, built-in footprint fallbacks, live registries,
distributor services, exporter-time resolution, and undeclared environment discovery cannot
affect compilation. Adding or changing a capability requires a new compilation-contract
version and a focused ADR.

`CompiledBoard` owns:

- the canonical physical Board snapshot and source Board identity;
- the minimum immutable logical dependency snapshot;
- the consumed selected part/asset closure, including resolved footprints;
- the explicit capability snapshot;
- input digests plus compiler/schema provenance sufficient to verify and reopen the artifact.

It is not an editable Board, another owner of connectivity or selected parts, a generic
workspace, or a cache whose stale state may be silently used.

## Minimum Logical Dependency Snapshot

The v1 logical snapshot is one exact deterministic Board-consumption closure, not a mutable
`Circuit` clone. Because every Board is a complete alternative rather than a partition, the
closure starts from every Circuit component instance and every Circuit net, including DNP or
unplaced instances and unrouted nets. It then follows only the relationships listed below.

The snapshot contains exactly:

- logical document/schema identity and the canonical digest of exactly this dependency closure;
- each component instance's stable document-local identity, reference, typed properties,
  electrical attributes, definition identity, assembly/DNP intent, selected-part identity, and
  module-expansion/reporting provenance;
- each referenced component definition's identity, typed properties, provenance, ordered
  pin-definition identities, and pin electrical meaning, excluding its schematic-symbol
  references;
- every concrete pin's stable identity, definition and component ownership, electrical
  meaning, no-connect intent, and pin-to-net membership;
- every net's stable identity, name, kind, typed electrical meaning, intentional-stub intent,
  complete member-pin identities, and net-class assignment; and
- each assigned net class's stable identity and complete resolved physical rule facts.

Selected-part definitions, package/pad mappings, footprints, and asset bytes remain in the
separate selected part/asset closure and are joined by the selected-part identities above. No
unreferenced definitions, module templates, Schematic presentation, authoring Python, mutable
resolver state, source-library catalogue, or other Circuit table or payload enters the v1
logical snapshot.

Changing these roots, traversal rules, included payloads, or exclusions requires a new
compilation-contract version and a focused ADR. A downstream consumer cannot grow or reduce the
snapshot ad hoc.

## Compilation, Invalidation, And Failure

`compile_board` is an explicit typed C++ service. It does not mutate its `Circuit`, `Board`,
closure, or capabilities. It takes a coherent snapshot, verifies it, and either publishes one
complete immutable artifact or publishes none. No partial `CompiledBoard` is observable. Its
typed result retains deterministic compile and design diagnostics on both success and failure;
diagnostic reports are evidence keyed to the provenance digest, not canonical `CompiledBoard`
state and not an input to its identity.

An implementation may cache compilation only by the exact provenance digest. Cache lookup is
an explicit optimization and must verify all input digests. There is no eager hidden rebuild,
ambient lazy resolution, or implicit use of the last compiled value.

Any logical dependency-closure, physical, selected-part/asset, capability, or
compilation-contract change makes an older artifact non-current for those authoring inputs. The
older artifact remains valid as historical evidence. Callers select or compile the desired
identity explicitly; "non-current" never means corrupt or mutable.

Foreign-owner IDs, a Board bound to another Circuit, malformed input snapshots, a non-empty
exact selection that cannot be resolved or is incompatible with the instance's component
contract, invalid pin/package/pad mappings, missing required definitions or asset bytes, digest
mismatches, and unsupported required capabilities are typed boundary or compilation failures.
They leave every input unchanged, produce no artifact, and retain typed diagnostic evidence.
Exact carrier types may distinguish caller-contract exceptions from a failed compile result,
but absence is never an untyped null or incomplete value.

Absence of a selection is not by itself a compile failure: a populated occurrence that lacks an
exact part is a readiness diagnostic, while DNP intent does not require a selection. If a
placement or declared capability needs part-owned mappings, definitions, or assets that the
closure cannot supply, compilation fails under the missing-input rule above. Other structurally
valid but poor design remains representable and diagnosable. Unplaced components, unrouted nets,
clearance violations, capability-limit issues, and other readiness problems accompany the
immutable artifact in the compile result; delivery/export policy may reject an artifact based
on those diagnostics.

An owned `CompiledBoard` remains valid after all authoring inputs and source libraries are
destroyed or upgraded. A loader may expose borrowed typed views into an owning loaded artifact
or Project bundle, but those views cannot outlive that owner. Raw build-local entity IDs never
become cross-artifact identity.

## Atomicity, Diagnostics, And Persistence

Existing operation-specific routing contracts are preserved. `BoardRouter::connect` is
all-or-nothing: failure commits no tracks or vias. `BoardRouter::escape` is intentionally
partial-success: it rejects an unattemptable component before per-pad mutation, then valid
per-pad work and its escape room remain committed when another pad fails, and the typed result
reports every per-pad outcome. Circuit-style whole-call atomicity is not imposed on operations
whose accepted contract is partial success. Both operations retain their documented
deterministic candidate and result ordering.

Structural invalidity is rejected at mutation, load, selection, or compile boundaries. Bad
design intent and physical quality remain diagnostics. Reporting references do not become
mutation or traversal handles, and neither diagnostics nor a compiled snapshot can redefine
logical connectivity.

Circuit, Schematic, Board, and `CompiledBoard` codecs preserve owner meaning and deterministic
round trips. Authoritative documents contain canonical owner state and dependency identities;
derived geometry, indexes, views, renders, and reports remain reproducible outputs. A stale or
missing derived sidecar cannot change model loading, provenance, or projection meaning.

## Consequences

- Schematic and Board migrations can compress their roots without copying Circuit's exact
  method budget or moving algorithms into vague root verbs.
- Every Board consumer can fan out from one verified, library-independent `CompiledBoard`.
- Multiple physical alternatives remain explicit without introducing another logical or
  assembly owner.
- Historical artifacts may consume additional storage, but their meaning cannot drift after a
  library, capability profile, or authoring source changes.
- Downstream formats and services must preserve source Board identity and verify provenance
  rather than relying on filenames, optional singleton state, or ambient resolution.

## Non-Goals

- Implementing or refactoring Schematic or Board projections, bindings, services, or codecs.
- Migrating Python bindings or changing the public fluent authoring surface.
- Deleting resolvers or built-in libraries before explicit closure consumers exist.
- Implementing `CompiledBoard`, `BoardScene`, the typed artifact graph or manifest, bundle
  loading, renderers, exporters, or manufacturing cutovers.
- Designing assembly partitions, harnesses, sub-boards, simultaneous multi-board products,
  per-Board populations, or multi-board BOM/CPL semantics.
- Defining the downstream S1/S2, B1/B2, Y1/Y2, B3, or C1-C3 implementation contracts.
- Introducing a universal workspace, generic stage graph, entity base, property bag, or
  alternate semantic owner.

## Revisit Trigger

Revisit this decision only when a concrete supported product requires one Board to represent
less than the Design's complete logical/assembly intent, when a measured workflow cannot use
explicit digest-keyed compilation within its latency target, or when a supported downstream
consumer cannot operate from the frozen standalone dependency closure. Assembly partitions,
harnesses, multi-board products, or a new root responsibility require a new focused ADR rather
than an implicit extension of these owners.
