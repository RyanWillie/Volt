# R1b: Native symbolic targets and single-route gap feasibility

Status: proposed for independent contract review. Tracking: #383.
Baseline: main `6e7f3de06c92b7c3800a2dd4fd9c90d8513eed0e`, 2026-10-13.
R1a's native request, ownership, rules, results, chronology and publication contract
remain normative. This slice adds no sessions, cache, fluent migration or search.

## Finite grammar and context

`RouteStep` additionally accepts `RouteExit{operand, direction, distance_mm}`,
`RouteBeside{operand, direction, distance_mm}` and
`RouteGap{lower, upper, axis}`. Axis is X or Y; directions are PositiveX,
NegativeX, PositiveY, NegativeY in board coordinates. No direction is inferred.
Distances are finite nonnegative extra copper-edge separation, in mm; no snapping.
These steps move along only the specified board axis, preserving the other current
coordinate. They are nonterminal; the existing final `RouteTo` remains required.
Literal R1a endpoints, corner policies and vias are unchanged. Target operands do
not infer the route net: their copper may deliberately belong to other nets.

`BoardRouter::query_gap(RouteSpec)` reads a route prefix whose last step is Gap,
instead of To, and requires an explicit owner-bearing net. It plans exactly that
prefix, including the starting endpoint, layer changes and earlier candidate
copper. It returns the existing owned RouteResult with no publication and no
committed IDs. Absence of failure means this one route prefix is feasible; it
is neither a bus count nor a complete-route legality certificate. The same prefix
inside route() resolves through the same implementation. Prefix current point,
active layer, width, net, preceding candidates and rule geometry are identical;
a later terminal endpoint cannot change the explicit net. Query structural
failures throw KernelError; query design failures return native Report payloads.
Queries do not mutate Board or router caches. Calls require fresh resolved inputs
under R1a's existing synchronous lifetime contract.

## Closed operands

All operand identities borrow the Board, which must outlive the call; pads retain
R1a's full copied exact FootprintDefinition identity. EntityRef is reporting only.
All references and grammar are structurally preflighted before planning, including
operands in later steps. Unknown ID, wrong owner, foreign exact footprint, invalid
enum/index/distance is a typed KernelError under either policy.

| Operand | Active geometry | Supported shape / boundary |
|---|---|---|
| RoutePad | Actual transformed active-layer pad copper | Circle and Rectangle only; rotation and bottom-side transform native |
| RoutePlacedCopper{Board, placement, exact footprint} | Union of each active-layer pad's actual copper, preserving each net/kind | Circle/Rectangle only; mixed nets keep independent constraints |
| RouteTrackCopper{Board, track} | All stored polyline segment capsules with actual width | Arbitrary finite slopes; conservative axis projection |
| RouteViaRef | Actual outer copper disc on active span layer | Native disc/span |
| RouteKeepout{Board, keepout} | Polygon exterior on a layer restricting Copper or All | All native valid polygons; axis projection conservative |
| RouteBoardEdge{Board, edge_index} | One actual stored outline segment, interior side determined from rectangular outline | Axis-aligned rectangular BoardOutline only; segment must be normal to requested movement axis |

No body/courtyard fallback, invented net, zone/fill operand or placement envelope
is provided. Body/courtyard are separate placement concepts outside this routing
slice. Oval/native conservative pad models, absent physical closure, unconnected
active copper, no active-layer copper, nonapplicable keepout, nonrectangular or
wrong-axis edge and unsupported geometry return GeometryUnresolvable with precise
native text. A placement cannot silently omit unconnected or unsupported active
pads. Rotation and angled tracks use min/max projections of actual transformed
geometry plus native radius; projecting separated shapes may conservatively close
a corridor even when another spatial path exists. No search is attempted.

## Coordinate and boundary convention

Copper and keepout operands are occupied exterior obstacles. Positive direction
selects their maximum axis projection and negative selects their minimum. Exit
requires the current centre to lie in the operand's projected box (both axes),
then sets the selected coordinate beyond that side. Beside has no containment
requirement and sets the same selected coordinate from any current point. Neither
is proof that the connecting leg avoids that operand; native final legality applies.

For each copper shape independently, reserve half the route width plus its native
pair clearance plus the explicit extra distance. Same historical continuity group
has zero copper clearance but still reserves the route half-width. Mixed-net
placement bounds take the outermost of the individual constrained shapes. Keepout
reserves half-width plus extra distance and an explicit `2 * board_drc_epsilon`
open-boundary margin (native keepouts reject contact within epsilon). Board edges
select only the interior half-plane and reserve half-width plus Track/BoardEdge
clearance plus distance; a direction pointing outside is unresolvable. Exit from
an edge is unsupported; Beside and Gap use its interior boundary.

Gap lower/upper are ordered, never silently swapped. Occupied lower contributes
its maximum projection; occupied upper contributes its minimum. An edge contributes
its selected segment coordinate and must have the appropriate interior side.
The unconstrained projected interval must be strictly positive; overlapping,
reversed or equal projections are GeometryUnresolvable. Each side applies its own
clearance and width reserve. The centre is the midpoint of the remaining interval;
exact fit is accepted with native `board_drc_epsilon`, one micron short fails.
A negative remaining interval beyond epsilon is RuleUnresolvable. Full finite
arithmetic is checked before BoardPoint construction; overflow is GeometryUnresolvable.
The transverse coordinate is preserved; no overlap/sweep axis is guessed. Whole
leg legality checks include both named obstacles, all other copper, all mechanical
openings, all keepouts and the actual outline. Same group never exempts mechanical
constraints. Zero motion uses a local Track-kind disc of the actual width for
rule/legality evaluation; no zero-length BoardTrack is published.

## Finite final-geometry rule strategy

Resolution enumerates only authored native rule regimes: inherited widths from
max(board minimum, effective class width) and finite room width values at least
that base (or the one explicit override); per-shape copper clearance from the
native kind-pair/class base and finite room clearance values, or zero for same
group; mechanical edge and keepout reserves are fixed. This is a bounded rule
state calculation, not coordinate sampling or spatial routing search.

For each tuple, compute the analytic target, construct the actual axis segment
(or zero-motion disc), then resolve its width through R1a's native room width
iteration / explicit replacement and evaluate each shape's clearance through the
shared native check_copper_clearance using resolved historical continuity. Retain
only tuples matching those final native values exactly. Duplicate identical
coordinates/width/side values collapse. Exactly one self-consistent target resolves;
none is RuleUnresolvable and multiple different solutions GeometryUnresolvable.
No iterative target nudging, scalar clearance floor or fallback is allowed.

Width provenance and pair provenance reuse R1a controlling/enforced source rules,
including room replacement of global/class clearance and explicit width minima.
The owned result additionally records target bounds and ordered side constraints
(lower then upper; each shape in native deterministic order), actual effective
clearance, reporting identities and native sources, including same-group exemption
without a fabricated board rule. Final legality continues through the R1a spatial
predicate against preceding candidate copper and normal append-only publication.
Target failure retains the candidate prefix with candidate_complete=false. Start
failures precede target step failures. Legal Report routes still commit; every
rejection leaves canonical bytes, generation and existing borrowed references intact.
Resource exceptions retain the same strong guarantee and are never design failures.

## Acceptance matrix

| Area | Required fixtures |
|---|---|
| Recalculation | width/class/rooms, priority/ties/replacement, placement/rotation/side, unchanged literals |
| Gap math | exact fit, one micron short, unequal sides, mm/full coordinates/epsilon, negative and overflow |
| Copper | pad/placement/track/via, same/different/historical group/mixed nets, absent layer/unconnected/closure |
| Geometry | rotated rectangle/circle, angled polyline, conservative projection, unsupported oval/zone/outline, reversed/overlap/wrong edge/Exit outside |
| Mechanical | both edge sides/interior/outside, keepout layer/restriction/contact margin, openings, same group mechanical rejection |
| Rule states | final containment affecting width/clearance, zero/one/multiple fixed points, explicit below-floor, deterministic sources |
| Parity | direct prefix query vs route prefix, complete native/Python owned payload and message parity, Strict vs Report |
| Atomicity | read-only query bytes/generation/references; rejected target after prior tracks/vias; new preparation allocation fault sweep |
| Regression | full native/Python, architecture/docs, existing R1a endpoint/corner/via fixtures, fresh exact-head platform/sanitizer/coverage CI |

Independent contract and implementation reviews and their resolutions are recorded
on #383 / the implementation PR. No merge is authorized.
