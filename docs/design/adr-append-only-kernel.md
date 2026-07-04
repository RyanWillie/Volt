# ADR: Append-Only Kernel State

Status: accepted

Issue: VOL-271

## Decision

Volt kernel state is a build artifact, not a live document. Authoring sources such as
Python scripts and project inputs are the editable source of truth; a `Circuit`,
`Schematic`, or `Board` is the compiled result of executing those sources.

Kernel entities are append-only for the lifetime of one build. New entities may be added
through explicit kernel mutation APIs, and existing entity payloads may be updated through
explicit operations where the kernel owns the invariant. Kernel aggregates should not
grow `remove_*`, `erase_*`, or `delete_*` APIs for entities, and entity IDs must not be
recycled.

## Rationale

Local facts in the current tree support this decision:

- `rg -n "\b(remove|erase|delete)_" include/volt -g'*.hpp'` returns no matches, so
  the public `Circuit`, `Schematic`, and `Board` aggregate APIs do not currently expose
  removal, erasure, or deletion entry points.
- `include/volt/core/ids.hpp` defines `EntityId<Tag>` as a strongly typed wrapper around
  a `std::size_t` table index. Erasing rows or recycling slots would make later IDs and
  references ambiguous unless the storage model grew generations, tombstones, sparse
  allocation, or migration machinery.
- `docs/architecture.md` already describes `EntityTable<T, Id>` as vector-backed,
  deterministic, insertion ordered storage that intentionally does not support deletion,
  generations, tombstones, sparse storage, or custom allocation yet.

This keeps the authoring model clear. Scripts and project files are the editable source;
kernel state is compiled from that source, validated, inspected, serialized, rendered,
and discarded or rebuilt. A tool that wants a different design should edit the source
description and rebuild the kernel model rather than surgically deleting rows out of the
compiled model.

Append-only IDs are stable for the lifetime of one build. That stability lets validation,
serialization, schematic projections, PCB projections, diagnostics, and adapters hold
typed references without needing a second lifetime model for missing or recycled
entities.

Insertion-ordered entity tables also make deterministic serialization straightforward.
When entities are appended in a deterministic build, writers can emit stable arrays and
stable local document IDs without walking around holes or preserving tombstones.

Finally, append-only entity storage keeps invariant enforcement simpler. Mutation
boundaries can reject structurally invalid additions and updates immediately, while
validation can report bad design intent. If entities could be erased, every subsystem
that stores `EntityId` references would also need dangling-reference cleanup or repair
rules, making invalid kernel state easier to represent.

## Consequences

Interactive tools rebuild kernel state from authoring source instead of treating the
kernel as an in-place editable document database. For example, a UI undo stack should
undo edits to the authoring source or command log, then rebuild the `Circuit`,
`Schematic`, or `Board` view from that source.

Entity IDs are stable within one build, but not across builds. Persisted documents should
continue using document-local IDs and domain data such as names, references, and explicit
relationships rather than assuming a `ComponentId(12)` from one build has cross-build
meaning.

This decision deliberately avoids deletion semantics in the kernel aggregates. If a
future feature appears to require entity removal, it should first prove why rebuilding
from source is not sufficient and then revisit this ADR before introducing removal APIs.

Undo operates at the authoring-source level. The kernel may expose explicit updates for
kernel-owned meaning, such as connecting a pin or setting a property, but undoing a user
action should restore the source command stream or project description and recompile the
kernel state instead of mutating entity storage backward.

## Revisit Trigger

Reopen this decision only when a concrete UI or tool requires sub-second incremental
edits and a full rebuild from authoring source is measured to be too slow. Use the
VOL-265 benchmark work as the measurement path: capture the relevant circuit size,
authoring operation, rebuild time, and target latency, then compare that evidence against
the added complexity of generations, tombstones, sparse storage, or explicit removal
APIs.
