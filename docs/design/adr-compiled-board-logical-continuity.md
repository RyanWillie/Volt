# ADR proposal: Preserve realized logical continuity in CompiledBoard

Status: proposed; requires maintainer approval, not an accepted architecture change.

Issue: [#389](https://github.com/RyanWillie/Volt/issues/389).
Related accepted decision: [Projection Ownership and CompiledBoard](adr-projection-ownership-and-compiled-board.md).
Detailed readiness proposal: [M1](../superpowers/specs/2026-10-09-manufacturing-readiness-m1-design.md).
Baseline: `c43c9f0d8d53ed32a39e55418de51f5da4170689` (2026-10-09).

## Problem

The accepted standalone artifact must preserve the logical meaning required by
physical validation. `build_logical_snapshot` currently removes module instances
and definitions. Port bindings are serialized within module instances; the loaded
artifact Circuit therefore loses them. Native `NetContinuityView` joins internal
and parent nets through those bindings. Reporting origin names do not restore this
meaning, and a clean placed-pad ratsnest cannot prove connectivity was retained.

Verified source seams:

- [Snapshot construction](../../src/io/pcb/compiled_board.cpp).
- [Logical writer](../../src/io/logical/logical_circuit_writer.cpp).
- [Hierarchy reader](../../src/io/logical/logical_circuit_hierarchy_parser.cpp).
- [Native continuity service](../../src/circuit/validation/validation.cpp).

## Proposed decision

Extend the minimum logical dependency closure with one canonical realized-net
partition, derived in C++ from the original Circuit's native `NetContinuityView`
before any hierarchy exclusion. Each group lists the original document-local
`NetId` identities; all concrete nets, including singletons, appear exactly once.
Sort members by native ID order and groups by their minimum member ID. Empty,
overlapping, missing, duplicate or foreign-ID groups reject structurally.

This is a frozen query result representing already authored logical continuity,
not new PCB netlist intent. Circuit remains the source; nets, names, membership,
assembly intent and IDs are unchanged. No new net is created, no physical contact
can alter the partition, and no Python/PCB mutation surface can author it as an
independent semantic path. No additional unconsumed module templates or ambient
library/hierarchy dependencies enter the artifact.

Use a closed native artifact/query carrier for this partition. Extend the existing
continuity-query consumer path so the resolved CompiledBoard view supplies this
verified historical partition, while authoring views still derive it from their
Circuit. Do not add a Circuit root mutation, public restoration API, generic alias
command, or second union/geometry engine. Every downstream physical query that
depends on logical equivalence (ratsnest, copper validation, shorts and readiness)
must use that same carrier; do not fix only the final ready boolean.

The partition is canonical logical-closure input, not an optional derived cache.
Compilation verifies it equals the native source-Circuit partition. Reopening
validates its structure and canonical order over the artifact-owned logical nets;
the verified compiler/schema provenance establishes its declared compilation
contract. A hash proves content identity, not an independently reconstructed
authoring history. There is no claim that a maliciously re-authored complete
artifact can be certified against an unavailable original Circuit.

## Version and compatibility

Introduce CompiledBoard schema/compiler contract V2 and domain-separated provenance
covering the added logical-closure data. Update the closed wrapper/manifest codec
and exact-revision consumers consistently. Retain V1 ordinary read/export support;
do not modify historical bytes or infer a lost partition from reporting names.

V1 cannot prove preservation of logical continuity. The first native manufacturing
readiness evaluator therefore records a non-waivable `NotEvaluated` logical-closure
blocker for V1, with source recompilation under V2 as remediation. This conservative
rule applies even to apparently flat V1 boards: do not invent a backwards-compatible
certification that the old schema never recorded. Explicit migration with complete
source inputs produces a new historical artifact and identity, never a silent
loader upgrade or execution of authoring source during read-only delivery.

The existing four semantic compile inputs remain unchanged. The new closure fact is
derived from the Circuit input, not a fifth ambient input or a new capability. Full
verified selected PartDefinition retention for existing native part/BOM consumers
does not itself require new serialized payloads; review any actual codec change
separately rather than conflating it with this continuity extension.

## Verification and acceptance

- Bind a module internal net to a parent net and prove source, fresh artifact and
  reopened artifact have the same complete partition, required terminal groups,
  ratsnest, short/clearance and readiness results.
- Multiple/chained bindings, distinct unbound nets, singletons and DNP terminals
  retain exact native continuity semantics without merging logical net IDs.
- Change a binding so the realized partition changes: logical/provenance digests
  and affected results change. A redundant binding change preserving the identical
  consumed closure need not change identity. Equivalent sources and checkout roots
  produce identical canonical V2 bytes.
- Malformed partitions and unsupported schema/compiler combinations reject with
  typed structural errors; a stale digest cannot admit tampered closure data.
- Reopened V2 consumers run after original Circuit, closure and authoring Python
  are unavailable. V1 ordinary read/export remains supported, but readiness cannot
  pass it; require an explicit new source compilation.

## Alternatives and scope

Retaining full module templates/hierarchy would also preserve bindings, but expands
the accepted minimal physical-consumption closure and introduces unnecessary
template state. Guessing continuity from names, reporting origins, copper contact,
or an empty ratsnest is incorrect. Requiring a live Circuit defeats standalone,
source-free delivery. The finite realized partition is the proposed smallest
closure extension; approval of this encoding and V1 readiness behavior is required
before implementation or `ready` admission for #389.

This decision does not alter simulation, logical authoring APIs, schematic meaning,
board partitions, routing grammar or the #382 implementation contract.
