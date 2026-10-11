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
