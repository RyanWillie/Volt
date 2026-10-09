# R1a: Native explicit-route contract

Status: **proposed for maintainer review; not approved for implementation**.

Date: 2026-10-09. Source baseline: `c43c9f0d` (main after #394/#395).
Tracking: [#382](https://github.com/RyanWillie/Volt/issues/382), under
[#381](https://github.com/RyanWillie/Volt/issues/381).

This is the bounded repository proposal extracted from the amended, local-only
2026-10-04 R1 draft. It does not adopt that draft's full R1–R5 program or declare
any issue ready. All decisions below, including Report behavior and atomic storage
publication, require maintainer approval before implementation dispatch.

## 1. Scope and ownership

R1a plans, resolves rules, checks and atomically appends **one author-directed route
for one existing logical net**. C++ owns its description, endpoint resolution,
corner geometry, sizing, provenance, legality, result and rejection message. The
thin Python binding translates typed values and exceptions only. Existing
`Board::add_track` / `Board::add_via` remain permissive for structurally valid bad
design; this helper's strict policy does not turn all Board mutations into DRC
guards. No logical net or pin membership is created or changed.

The new native corner family is classified as a focused reusable authoring
operation under [#233](https://github.com/RyanWillie/Volt/issues/233). Previously
accepted Python simple-corner lowering is not retroactively a defect. Moving this
family and cutting over its consumers are separate decisions: #382 defines the
native contract; #384 owns replacing the existing fluent lowering.

Excluded: symbolic Exit/Beside/Gap targets and capacity queries
([#383](https://github.com/RyanWillie/Volt/issues/383)); persistent sessions,
cache invalidation and consumer/API migration
([#384](https://github.com/RyanWillie/Volt/issues/384)); planes, pairs/buses,
package patterns, assisted legs, route-all/search, rip-up and route-intent
serialization. No Circuit API change, borrowed logical facade, entity deletion,
generic transaction framework, public restoration API or new friend is proposed.

The accepted [Circuit ADR](adr-circuit-aggregate-api.md) and the supplied task's
older facade guidance disagree. This routing proposal does not settle that
logical API conflict; it consumes the existing read-only Circuit boundary.

## 2. Request grammar

The following is a proposed typed value schema, not existing API documentation.
`BoardRouter::route(const RouteSpec&, RoutePolicy = Strict) -> RouteResult` is the
single synchronous entry point. The router does not retain the request or publish
an externally committable plan. Authoring inputs must not mutate concurrently.

```text
RouteSpec
  board: borrowed identity of the destination Board
  net: optional RouteNetRef{borrowed Circuit identity, NetId}
  start: Endpoint
  layer: RouteLayerRef{borrowed Board identity, BoardLayerId}
  width: Inherit | Override{mm}                         # default Inherit
  via_size: Inherit | Override{drill_mm, diameter_mm}    # default Inherit
  steps: nonempty ordered vector<RouteStep>

Endpoint = Point{BoardPoint}
         | Pad{borrowed Board identity, ComponentPlacementId,
               resolved footprint identity, FootprintPadId}
         | Via{borrowed Board identity, BoardViaId}
         | TrackEnd{borrowed Board identity, BoardTrackId, First | Last}

RouteStep = Through{Endpoint, CornerStyle}
          | ToX{finite coordinate_mm}
          | ToY{finite coordinate_mm}
          | Via{destination RouteLayerRef}
          | To{Endpoint, CornerStyle}

CornerStyle = Chamfer45 | Orthogonal | Direct           # default Chamfer45
RoutePolicy = Strict | Report
```

Exactly one `To` terminates the request and must be its last step. `Through`
continues; axis steps use literal board coordinates in R1a. `Via` is at the
current point, changes the active layer, and adds no lateral motion. No per-step
width or via-size override is accepted in this slice. A corner policy describes
each Through/To leg independently. Values are copied/const-consumed, with no
Python callback or dynamically resolved object handle in a native request.

Owner identities are route-specific borrowed references, not serialized tokens
or new generic kernel handles. They and the referenced models must outlive the
call. They let native validation reject wrong-owner inputs even when two Boards
have the same local numeric IDs. Existing `EntityId` values are only table indices:
an unscoped foreign ID that happens to equal a local ID cannot reveal its origin.
Do not claim the existing IDs already provide cross-owner detection. Python bound
objects lower their actual owner identities; raw public traversal/mutation does
not switch to `EntityRef` (which remains reporting-only).

### Endpoint, net and layer integrity

Preflight the entire grammar, owner identities and lookups before planning. Pad
references also carry the actual immutable resolved footprint identity, checked
against that placement's selected footprint before the local pad index is used.
Numeric FootprintPadId alone cannot reveal a foreign pad's origin when both
footprints have pad 0; the owner-bearing value must reject that case. The precise
identity representation must reuse the native resolved footprint/part closure
(not a new global ID registry), live through the call, and have a parity fixture.
Pad IDs must belong to that footprint. A Pad endpoint resolves position/net from native
physical truth; the caller cannot supply a competing position. A Via resolves its
centre/net/span; TrackEnd resolves the first/last stored vertex, net and layer.

Collect net evidence from every identity-bearing endpoint, including Through.
An explicit net must exist in this Board's Circuit and match each endpoint's
exact NetId. Without it, all connected endpoint evidence must agree;
choose the first identity-bearing endpoint's NetId in traversal order. This uses
the existing routed-track net-resolution contract, not name comparison, continuity
group inference or net merging. Shared native DRC may exempt same-continuity-group
copper from clearance; that exemption does not make conflicting endpoint IDs a
valid request or change which single net the route implements.
At least one connected endpoint or an explicit net is required. Unconnected pads,
ambiguous inference and conflicting endpoint groups are structural request errors.
A Point contributes no net evidence; it is not proof of contact with a pad.

Missing physical closure for an otherwise valid placement is `EndpointUnreachable`
with no complete candidate; it must not be disguised as an invalid foreign ID.
The start/step layer must exist, be enabled copper and belong to the committed
stack. Via destination must differ from the active layer. All layers occupied by
a via's native span participate in allowed-layer and legality checks, not just
its two ends. At the incoming/outgoing leg a Pad must have copper on the active
layer, a Via must occupy it, and a TrackEnd must be on it; otherwise the resolved
request cannot connect and yields `EndpointUnreachable` (a design failure). `ToX`
and `ToY` do not change layer. No silent fallback to a different layer or endpoint.

## 3. Corner and degeneracy convention

Coordinates and distances are millimetres; no grid snapping occurs here. Given
`a=(ax,ay)`, `b=(bx,by)`, `dx=bx-ax`, `dy=by-ay`, let `sx=sign(dx)` and
`sy=sign(dy)` denote the signs for nonzero components.

- Direct: emit `[a,b]`; arbitrary slopes are explicitly permitted. This is not
  the assisted router's octilinear search and adds no obstacle-avoidance search.
- Orthogonal: aligned legs emit `[a,b]`; otherwise emit
  `[a,(bx,ay),b]` (horizontal first).
- Chamfer45: aligned or exactly 45-degree legs emit `[a,b]`. Otherwise, if
  `abs(dx)>abs(dy)`, emit `[a,(bx-sx*abs(dy),ay),b]`; if
  `abs(dy)>abs(dx)`, emit `[a,(ax,by-sy*abs(dx)),b]`.
  Thus the axial leg is first and the diagonal enters the target.

Here `sx = sign(dx)` and `sy = sign(dy)`. Equal magnitudes are the 45-degree case.
These branches use exact finite input comparisons; they do not silently perturb
an endpoint to make it aligned. Computed duplicate vertices are removed and
non-finite arithmetic results yield `GeometryUnresolvable`. No epsilon-length
segment is silently dropped. A zero-motion Through/axis/To produces no track;
the complete route must still contain at least one nonzero track or a via, else
`NoCandidate`. Via-only movement with coincident final To is valid. Consecutive
vias are checked, not collapsed. Collinear steps remain separate primitives so
step attribution and local room rules do not disappear.

Each nonzero lowered line segment becomes one candidate BoardTrack with two
vertices. Each Via becomes one candidate BoardVia. Candidate ordinals follow
step order, with tracks before the subsequent via that switches layer. This
freezes the tested geometry/width on each primitive and the later commit order.

## 4. Rules and provenance

Reuse native `resolve_net_class_rules`, `BoardRouter::resolve_parameters`, the
room resolver, span computation and shared DRC predicates; do not build parallel
Python rules or a route-only alternative to DRC. These existing APIs need focused
native provenance output, not a new generic rule framework.

Proposed sizing rules:

- Inherited width starts at `max(board minimum, effective net-class width)`.
  Each two-vertex segment then uses the existing room width resolver's finite
  upward iteration, including its whole-copper containment predicate. A room
  crossing does not automatically split a segment or use centreline-only lookup.
- Explicit width is the actual requested width. Evaluate the native width DRC
  requirement on that geometry: the board minimum always applies; an applicable
  room requirement replaces the class requirement, otherwise the effective class
  requirement applies. Below either applicable minimum (with native DRC epsilon)
  report `RuleViolation`; never silently raise an explicit override. Room
  applicability is evaluated with that actual width. An authored room can relax
  class width, but never the board minimum, just as current native DRC permits.
- Drill and outer copper diameter are resolved **independently** from board/class
  floors. A supplied via pair below either floor yields `RuleViolation`. A positive
  drill/diameter pair with `diameter<=drill` is structurally invalid. If inherited
  rules cannot yield `diameter>drill`, report `RuleUnresolvable`, not an invented
  diameter. Rooms currently supply no via-sizing override.
- Allowed layers reuse the existing class resolver (explicit names, otherwise
  semantic layer scope), intersected with enabled stack copper. No name silently
  resolves to a different layer; no supported stack layer means `LayerNotAllowed`.

**Copper clearance is not a simple global floor chain in current code.**
`required_copper_clearance` first uses an applicable room's clearance value; absent
that, it uses `max(board kind-pair clearance, both nets' effective class values)`.
Rooms apply only when both copper shapes satisfy their outline/layer predicate;
priority wins, then lowest room ID. This room value can be below the board/class
base. Width rooms only increase inherited authoring width, while width DRC replaces
the class requirement with the selected room width (not the board minimum).
Conflating those behaviors would make explicit override checking disagree with
native DRC.

The proposal is to preserve that existing copper-clearance contract, explicitly
documenting an authored room as a local rule rather than an acknowledgement of
illegal copper. Approval must confirm this interpretation of #382's “board
floors”; if an absolute clearance floor is required, the shared DRC rule must be
changed and reviewed separately before implementation, not overridden only by
the router. Likewise, approval must confirm room replacement of class width for
explicit override checks; imposing an absolute class floor would be a changed
rule contract. Mechanical/outline/keepout constraints and same-continuity-group
copper treatment continue through their native predicates; a mechanical obstacle
has no net and never receives a same-net clearance exemption.

Every resolved numeric value records `value_mm`, ordered controlling sources and
the relevant requested/default value. A source is typed: BoardRule (including
clearance-kind pair), NetClassRule (class ID and existing effective derivation),
RoomRule (room ID), or ExplicitOverride (request field). Max ties retain all tied
sources, ordered board, then class ID, then room ID. Explicit sizing records both
the selected override and enforced floor sources; it never erases the floor's
provenance. Width is recorded per candidate, via drill/diameter separately, and
clearance per queried pair/blocker, not as one invented route-wide clearance.
Allowed layers record the governing class/scope or board default and exclusions.
Non-numeric keepout conditions use a typed constraint source, not fake distances.

## 5. Planning, legality and policy

1. Preflight structural request integrity; structural errors use existing
   `KernelError` families and are not suppressible by policy.
2. Resolve endpoint/net truth and lower the complete geometry. Retain only a
   diagnostic prefix if construction fails; `candidate_complete=false` then.
3. Resolve per-primitive sizing/layers. A complete but under-floor explicit
   candidate can be reported; inherited unresolvable sizing cannot fabricate one.
4. Query every representable candidate primitive against existing board copper,
   outline, openings and keepouts, and against **earlier candidate primitives**.
   Use a private copied/staged native spatial index; inserting hypothetical shapes
   never appends to the Board or poisons the router's committed index. Shared
   same-group copper rules still apply: legal joins/overlap on this single net are
   not falsely reported as shorts. Check vias over their whole span. Preserve
   each transient primitive's clearance kind in staged obstacle metadata: current
   conversion discards `clearance_kind`, and an unlabelled via obstacle would be
   classified as Track by `shape_clearance_kind`. Extend that native metadata
   narrowly instead of inventing committed IDs or changing the shared predicate.
   Existing mechanical openings are Board features; this slice adds no new
   via-drill-to-via-drill rule unsupported by the native DRC model.
5. Only a complete, structurally valid, legal, nonempty candidate can publish.
   Success under either policy appends exactly the geometry checked above.

| Outcome | Strict | Report | Board mutation |
|---|---|---|---|
| Structurally invalid request | existing native KernelError | same KernelError | none |
| Complete legal candidate | return committed result | return committed result | whole append |
| Design failure/blocker, complete candidate | throw RouteRejected with result | return uncommitted result | none |
| Absent/partial/unresolvable candidate | throw RouteRejected with result | return uncommitted result | none |

**Proposed first-slice policy:** no violating commit, acknowledgement string,
eligible-violation list or suppression API. Report differs only in returning
design rejections instead of throwing them; it is not a dry-run mode for legal
routes. This deliberately narrows the broad draft's acknowledgement option and
requires explicit approval. Later deliberate violating commits need their own
reviewed eligibility/audit contract. Resource failures remain resource exceptions,
not “blocked” results. No canonical route metadata or diagnostic suppression is
introduced.

R1a creates fresh resolution/index input for each thin binding call; it makes no
session freshness or reuse claim. #384 must supply lifecycle/input-identity proof
for external Board, part closure and Circuit-rule mutations. Reusing a native
router with stale external input is not the R1a binding contract.

## 6. Append-only atomic publication

The [append-only ADR](adr-append-only-kernel.md) rules out deleting appended rows
to roll back. Current `BoardRouter::commit` loops over `Board::add_track` /
`add_via`, index insertion and result-vector insertion. Checking all geometry
first prevents design rejection, but allocations can still fail after one append.
This proposal does not claim those loops already supply a strong guarantee.

Propose one domain-shaped Board addition, `append_copper(CopperAppendSpec)` with
ordered typed tracks/vias, returning typed IDs in candidate order. It validates
net/layer/primitive structure, **not DRC**, so lower-level authoring stays
permissive. It is neither a generic transaction nor public restoration mechanism.

For the first bounded implementation, stage copies of only the existing track
and via tables, append the already constructed payloads there, and allocate the
complete result/ID vectors before publication. All rows retain their old indices;
new IDs are their predicted appended indices. Do not copy/replace the Circuit or
whole Board, or expose staging state externally. Then move-publish those two
tables through Board-owned private storage, advance geometry generation once,
invalidate the router's cached index without allocation, and return by no-throw
move. No callbacks, lookups, checks, allocations, index insertions or message
formatting occur between the first publication and return.

Implementation must statically prove table replacement and native result moves
are noexcept and test injected preparation failures before both tables publish.
On any native preparation exception, canonical Board bytes, counts, generation,
existing IDs and existing borrowed references remain unchanged. On success,
structural mutation invalidates borrowed ranges/references by the existing Board
contract. No deletion API, ID reuse, partial visible append, or failed-attempt
generation advance is permitted. Freeing replaced backing memory is not deleting
entities from the observable canonical model.

This costs O(existing tracks + vias + candidate) copy work/memory per call. Record
the cost in implementation tests/benchmarks; do not advertise session speedups.
A reserve-and-noexcept-append optimization may replace staging only after a proof
covering all tables, references and post-commit work, not merely vector capacity.
#384 must prepare its incremental index update before commit or use no-throw
invalidation; inserting into an allocating index after publication is not atomic.

The guarantee covers synchronous native operations, not process death or a Python
allocator failure while materializing an already committed native success.
Binding tests must distinguish that resource boundary from a native design
rejection; no Python exception may be reclassified as RouteError to imply rollback.

## 7. Typed result and errors

`RouteResult` is runtime-only, read-only to consumers and not serialized:

It owns candidate geometry, resolved coordinates and reporting data; no borrowed
Board/footprint pointer or index-backed view is required to inspect a rejection
after the router/resolution is destroyed. Reporting IDs remain build-local.

```text
committed: bool
candidate_complete: bool
net: optional NetId                         # absent if not resolvable
candidates: ordered typed track/via values  # prefix allowed only when incomplete
resolved_steps: step index, endpoints, active layers, candidate ordinals
resolved_rules: per-primitive sizing and per-check provenance
failure: optional {kind, step_index?, native message}
blockers: ordered RouteBlocker records
tracks/vias: committed IDs only; always empty when committed=false
committed_order: typed track/via IDs in candidate order
message: native UTF-8 summary
```

Proposed `RouteFailureKind`: `Blocked`, `EndpointUnreachable`, `LayerNotAllowed`,
`RuleViolation`, `RuleUnresolvable`, `GeometryUnresolvable`, `NoCandidate`.
Structural failures do not return an invalid result. The earliest failing step
and then the listed failure-kind order select the primary failure. Complete
representable candidates retain all queried blockers, not only the first one.
An incomplete candidate cannot be resubmitted as an authorized commit object.

A blocker has step/candidate ordinal, kind, applicable layer, native message,
optional witness point, optional required/actual distance, typed rule sources,
and either existing obstacle references/labels or earlier candidate ordinal.
Existing `BoardSpatialBlocker` has no universal witness point; don't promise one
without a native geometric witness computation. Outline/keepout/opening blockers
do not invent a copper EntityRef; transient candidates do not acquire fake
committed IDs. Convert spatial shape indices to owned reporting values while the
index lives; do not expose a dangling index lookup as the public result.

Sort by step index, candidate ordinal, layer ID (absent last), blocker-kind order
(CopperClearance, BoardOutline, MechanicalOpening, Keepout), then existing spatial
obstacle order or earlier candidate ordinal. Exact duplicates are removed; distinct
checks are not coalesced away. Message formatting and numeric precision are native
and locale-independent, with golden tests. An example shape is:

```text
route OUT step 0 blocked on F.Cu: needs 0.20 mm to U2 pad 1 (VCC),
has 0.12 mm [board track-pad clearance]; board unchanged
```

`volt::RouteRejected` derives from `std::runtime_error`, not `KernelError`, owns
the uncommitted `RouteResult`, and exposes it by const reference. Its `what()` is
exactly that result's native message; construction happens before mutation. A
direct C++ Strict caller receives this exception, not a returned false result.

The pybind11 translator creates `volt.RouteError` (RuntimeError subclass), retains
the exact native message and attaches the read-only native result as `.result`.
Existing structural error translation is unchanged. Python does not compare
blockers, synthesize rejection, translate rule meaning, or rewrite text. The
translator must avoid swallowing unrelated exceptions. Minimal callable/value
bindings and exception parity belong in #382; fluent builders, persistent sessions
and consumer rewrites belong in #384.

## 8. Implementation acceptance after approval

- Golden corner fixtures: all quadrants, horizontal/vertical/45-degree, dominant
  axes, negative coordinates, Direct arbitrary slope, orthogonal horizontal-first,
  zero-motion steps, via-only route, non-finite arithmetic, deterministic vertices.
- Grammar/owner tests: terminal To, empty steps, wrong-owner colliding numeric IDs,
  unknown IDs, wrong placement/pad association, disconnected pads, inference from
  all endpoint kinds, exact-net agreement/conflict (including grouped but distinct
  endpoint IDs), disabled/noncopper/
  off-stack layer, Via to current layer, pad copper layer, track layer and via span.
- Rules: absent/assigned/default/derived class values, board floors/ties,
  independently sourced via sizes, explicit under-floor rejection without clamping,
  room priority/ties/containment and inherited width iteration versus explicit
  room/class replacement, copper room overrides below the global base,
  exact-fit/native-epsilon boundaries, allowed intermediate layers.
- Legality: existing copper, within-candidate track/via checks, allowed same-group
  joins, board edge/opening/keepout cases, deterministic blocker ordering and
  optional metrics, transient Track/Via obstacle-kind retention; parity with final
  native DRC for supported predicates.
- Atomicity: every Strict/Report failure has unchanged canonical Board bytes and
  empty committed IDs; a late blocked leg does not leak early tracks/vias; allocation
  fault injection exercises staging table/result failures. Successful IDs, counts,
  generation and geometry match the tested candidate, including interleaved vias.
- Native/Python: direct C++ throws RouteRejected; Python receives RouteError with
  identical text, failure/provenance/blockers and uncommitted result. Structural
  failures retain KernelError metadata and Report failures return the same payload.
  No Python routing/rule/rejection arithmetic is added. Existing helpers stay
  unchanged until the separately approved #384 cutover.

This spec PR is documentation-only. Tests above are obligations for the future
implementation, not tests claimed to have run. Approval should explicitly settle
Report's no-violation policy, native corner convention, owner-bearing request
values, preserved room-clearance semantics and the staged atomic append tradeoff.

## 9. Inspected baseline references

- [BoardRouter declarations](../../include/volt/pcb/routing/board_router.hpp) and
  [implementation](../../src/pcb/routing/board_router.cpp): sizing, endpoint-aware
  add_track, existing evaluate/commit and escape staging.
- [Board mutations](../../src/pcb/board.cpp),
  [private Board storage/ranges](../../include/volt/pcb/board.hpp),
  [EntityTable](../../include/volt/core/entity_table.hpp), and
  [IDs](../../include/volt/core/ids.hpp): append/storage/owner limitations.
- [Spatial query contract](../../include/volt/pcb/routing/board_spatial_index.hpp)
  and [implementation](../../src/pcb/routing/board_spatial_index.cpp): native
  blockers, ordered candidates, copy/move and legality predicates.
- [Room rules](../../src/pcb/copper/board_room_rules.cpp),
  [copper-clearance predicates](../../src/pcb/copper/board_copper_geometry.cpp),
  [width/via DRC](../../src/pcb/copper/board_copper_validation.cpp),
  and [net-class resolution](../../src/circuit/constraints/net_class_resolution.cpp):
  actual width/clearance precedence and derivation.
- [Native continuity](../../include/volt/circuit/validation/validation.hpp),
  [route net resolution](../../src/pcb/queries/board_queries.cpp),
  [existing Python corners](../../python/volt/_pcb_layout.py), and
  [exception translation](../../src/python/bindings.cpp): parity and migration seams.
