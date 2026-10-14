# CompiledBoard versions and logical continuity

Native `compile_board` produces standalone CompiledBoard schema/compiler V2. Its
logical-dependency wrapper includes `realized_net_partition`: an array of groups
of original document-local net IDs. Every concrete net appears exactly once,
including singleton and unconnected nets. Members are ordered by native net ID;
groups are ordered by their minimum member ID. Empty, missing, repeated,
overlapping, foreign-ID and noncanonical groups reject structurally on reopen.

The compiler derives this partition from the source Circuit's native
`NetContinuityView` before excluding module templates and instances, and verifies
the materialized artifact against that source partition. Reopen needs no source
Circuit, authoring callback, ambient library or hierarchy reload. The partition
is part of the canonical logical-dependency digest; schema/compiler V2 also
domain-separates compilation provenance from V1. Existing physical, capability
and selected-part payload grammars are unchanged and remain independently
versioned.

The post-materialization compiler check also compares every net's reporting-only
module-origin status with the native source query, protecting parent-net labels.
The private friend-gated continuity constructor restores only an immutable derived
query. It does not restore module hierarchy or expose restoration/mutation on
`Circuit`; it is intentionally unavailable to ordinary callers.

Use a `CompiledBoard` consumer or `compiled.view()` for exact-revision physical
queries. The closed resolved view supplies the preserved historical continuity
to ratsnest, copper validation, SVG and spatial legality queries. Reconstructing
an authoring view from `compiled.board()` is not a substitute: its flattened
Circuit intentionally has no bindings. Authoring resolved views and spatial
indexes still derive continuity from their live Circuit at query time; they do
not freeze later authored bindings.

Existing validated net-origin metadata preserves the native parent-net preference
for ratsnest reporting labels, including when internal nets have lower IDs. It
never creates equivalence: only the explicit partition defines compiled logical
continuity. Geometry, names and origins cannot merge logical nets.

The reader still accepts schema/compiler V1 paired with V1 and preserves its
exact bytes and identity for ordinary export. `logical_continuity_partition()`
returns no value for V1, even for apparently flat artifacts; no partition is
inferred and no silent upgrade occurs. Consumers requiring preserved continuity
must request explicit recompilation from complete source inputs. Unsupported or
mixed schema/compiler contracts reject.

DRC on a V1 artifact (including `compiled.view()`) emits one
`PCB_LOGICAL_CONTINUITY_UNAVAILABLE` warning. Its ratsnest and copper-clearance
queries retain legacy flattened-net behavior, not authoritative source continuity.
Recompile from complete source inputs for those checks; ordinary exports remain
available. The logical-dependency digest API requires an explicit schema version
so historical callers cannot silently select the current grammar.

ProjectBundle's independently versioned outer schema is unchanged. Its compiled-board
descriptor records the actual V1 or V2 payload contract and must match the verified
payload provenance. Reopening verifies the exact logical-dependency grammar recorded
by that artifact, rather than applying the current compiler grammar to historical V1
bytes. A supported descriptor version paired with the wrong payload version rejects.

Content hashes verify declared canonical bytes, not unavailable authoring history.
A completely reauthored, structurally valid artifact with all hashes recalculated
cannot be certified against an unavailable original Circuit. This prerequisite
does not implement manufacturing readiness, select a manufacturer/process policy,
or assert that any board is ready for fabrication or assembly.

## Retained exact definitions

Both supported archive versions retain their already-verified full selected
`PartDefinition` values after materialization. `compiled.part_definitions()`
borrows a native `PartDefinitionResolver`; resolution requires every field of
`LibraryPartRef`, including library and part digests. Absent or foreign references
raise native `UnknownEntity`. Repeated selections share one retained exact
reference/definition. Selection is retained for unplaced and DNP components too.

`compiled.selected_component_definition(component)` borrows the original verified
component definition, or returns null for a valid component without a selection.
Invalid component IDs reject. This definition supplies the complete content
identity and portable `ComponentContract`, including named subjects and PinKeys.
Its `pins()` IDs are closure-payload-local, not handles into `compiled.board().circuit()`;
use the artifact Circuit's own definitions/IDs for pin traversal.

These immutable borrows reject temporary CompiledBoard owners. Moving the owner
preserves them; destruction or replacement of their owning storage invalidates
them. A moved-from owner supports destruction and assignment only. The resolver
has no single-library admission identity and cannot be used to select new parts.

Native `project_bom` and `validate_bom_readiness` accept this resolver with the
artifact Circuit, preserving native quantities, DNP, alternates and diagnostics.
Strict electrical consumers that require original complete Circuit component
identities cannot treat the symbol-stripped logical projection as the original
component: their integrity checks remain unchanged. Full contract retention does
not reconstruct a Circuit or implement an electrical/readiness evaluator.

Only footprint assets and explicitly requested models3d assets are embedded.
Full retained definitions preserve other asset references without promising their
bytes or reconstructing a complete PartLibraryBundle. No ambient asset reload
occurs. Optional absent ratings/records remain absent.

This is an in-memory ownership extension. Canonical V1/V2 archive bytes, content
and provenance identities, selected-closure grammars, and V1 continuity limitations
are unchanged.
