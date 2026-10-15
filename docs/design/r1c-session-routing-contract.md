# R1c: retained routing session and consumer cutover

Status: independently reviewed, no unresolved blocking findings, 2026-10-14. Tracking #384.
Base: Volt f8c428584be5c7b5bb03d81bccfc3f4bcd791fa4. R1a/R1b remain normative.
Private consumer base and detailed source inventory are retained in the private
consumer repository; no private source or assets belong in this document.

## Ownership and finite input mechanism

The existing native BoardRouter is the retained routing session. It borrows one
stable Board and that Board's Circuit; it owns its ResolvedBoardView, resolved-pad
snapshot, spatial index and narrow input snapshot. It does not borrow a temporary
BoardResolution or footprints. Board and Circuit must outlive it and must not be
moved, assigned or replaced during that borrow. Delete router copy/move initially;
no change to Circuit mutation or public root API. Owned RouteResults survive the
session and later mutations. Captured owner-bearing pad identities continue to
reject a changed exact footprint rather than silently retargeting. Placement moves
resolve live positions on the next call. Existing standalone ResolvedBoardView and
BoardSpatialIndex stale rejection contracts stay intact.

PyBoard retains its PyCircuit and owns one native router lazily. Exact physical
resolution is refreshed by native IO only when selected inputs change; Python
never resolves geometry, rules or freshness. The binding copies the current
immutable closure digest, not a raw dangling pointer to a replaceable bundle.
The current bundle is borrowed only during resolve. A new selected closure or
selection prepares a new owning resolved view/router before replacing the cache.
An unchanged digest means unchanged immutable closure bytes. Capability profile
changes also refresh native resolution, since its validation consumes that input.
A query uses a temporary fresh router if needed and does not publish a replacement.

The complete consumed-input inventory and freshness mechanism is:

| Input | Tracking | Consequence |
|---|---|---|
| Board layers, enabled/role/side/weight; ordered stack, thickness/dielectrics | Existing Board geometry-range token | lazy pads/index refresh |
| Outline; holes/slots/cutouts; feature geometry | same token | lazy pads/index refresh |
| Placement transform/side; footprint cache definitions | same token | lazy pads/index refresh and exact closure validation |
| Tracks (including external batches), vias, zones, keepouts; room outline/layers/rules/priority; text conservatively | same token | lazy pads/index refresh |
| Board design rule minima and complete clearance matrix | set_design_rules advances same token after successful replacement | lazy pads/index refresh |
| Capability profile, including provenance/refinements | set_capability_profile advances same token; binding profile input snapshot | refresh physical resolution and lazy index |
| Component/definition/pin append; exact selected LibraryPartRef | explicit ComponentId/ComponentDefId/PinId counts and copied selected references | physical selection refresh in binding; stale native fixed view rejects new closure |
| Net append/kind/default class; class definitions (explicit/derived width/clearance/via/layers/priority/default and provenance) | immutable append-only records: Net/Class counts; copied per-net assignment | lazy pads/index refresh even with unchanged copper |
| Pin-to-net membership | copied per-pin net_of | lazy pads/index refresh |
| PortBinding continuity and module origins | append-only binding/module-instance counts | lazy pads/index refresh |
| Immutable selected closure and its exact footprint bytes/mapping/component identities | copied semantic digest plus selection snapshot in native binding | physical view replacement |

Counts are sufficient only for immutable append-only families. Snapshot comparison
uses current typed Circuit reads, not serialization, hashes of whole Boards,
new Circuit mutation/restoration methods, friends or generic cache frameworks.
Comparison is allocation-free for unchanged inputs; snapshot creation may allocate.
Runtime snapshots/counters do not enter canonical Board/Circuit/compiled bytes.
No concurrency: authoring inputs cannot mutate during a synchronous call.

## Preparation, queries, rejection and publication

For a valid warm session, route preparation copies the retained index and consumes
retained resolved pads. For stale/missing state it prepares fresh pads/index locally.
The original retained cache is never made to claim an uncommitted state. Queries
and rejections discard private prepared state; canonical bytes and cache state are
unchanged. A successful route adopts the private state. Cold/stale queries may need
a temporary rebuild each time; they do not silently mutate a read-only session.
Test-only counters distinguish resolution creation, pad resolution and full index
builds from index copies/inserts; no performance claim uses route-call counts alone.

Every candidate is checked/inserted into the private staged index as in R1a.
Before atomic Board::append_copper, prepare actual predicted append IDs/reporting
references on the accepted staged shapes, reorder shapes/boxes into the native
fresh index order (tracks, vias, zones, pads; stable segment/pad order), and remap
cell ordinal lists. This is an incremental publication update, not re-resolution,
full index construction or geometric reindexing. All allocating work happens before
Board publication. Use a private BoardRouter/BoardSpatialIndex collaboration seam,
not an externally committable plan or generic transaction. After append, only
noexcept token/count rebase and state move/swap occur. Failed allocation, malformed
request and design rejection leave counts, generations, borrowed ranges, canonical
bytes and retained state intact. Never insert into an allocating index after copper
publishes. Preserve every R1a/R1b ordered result field and native message.

Native connect/add_track/escape remain their existing distinct low-level/assisted
contracts; no search or fanout redesign. They participate in freshness preparation
and invalidate the retained explicit-route state on their own publications. Direct
Board::add_track/add_via stay structurally checked but DRC-permissive. Alternating
these native callers with route/query must never reuse stale evidence. The R1a
strong allocation guarantee applies to explicit routes; do not claim a new strong
guarantee for historical assisted/escape publication loops.

## Finite native grammar amendment

Consumer inspection and existing fluent helpers require these three additions:

* RouteAlign{endpoint, axis}: move on X or Y to the natively resolved endpoint's
  corresponding coordinate, preserving the current transverse coordinate. Preflight
  all owner/pad identity and enum checks. Alignment is a geometry-only reference like an R1b operand: it contributes no
  route net evidence and imposes no guide-copper active-layer reachability. A pad
  of another net, a front-only pad used as a back-layer guide, or a physically
  resolved unconnected pad is permitted; it is no claim of connection to that pad.
  Missing physical position/closure is EndpointUnreachable; wrong owner/identity
  remains structural. The generated leg is checked normally against all copper. Zero motion
  is skipped exactly like literal ToX/ToY. This replaces Python `.tox(pad)/.toy(pad)`.
* RouteOffset{axis, distance_mm}: finite signed axial displacement from native current
  point. No grid snapping, overflow nudging or search. Axis validation is structural;
  nonfinite input is structural and arithmetic overflow GeometryUnresolvable. Zero
  motion is skipped. Required only to retain directional fluent route/fanout helpers;
  no Python route offset arithmetic survives.
* RouteFinish{}: terminal end-current intent, without adding motion. Exactly one final
  To or Finish is required for routes. Finish-only with no track/via is NoCandidate;
  a via followed by Finish is valid. Queries still terminate solely in Gap. This permits
  native standalone via/stitch and fanout stub termination without fabricating a Python
  coordinate or requiring a top-side pad to have back-side copper after a via.

No new obstacle target, planes/pairs/bus/search or package fanout primitive. R1b
Exit/Beside/Gap semantics are unchanged. If consumer proof reveals another essential
primitive, stop that dependent migration, amend this finite contract and independently
review it before implementation; do not disguise missing semantics in Python helpers.

## Exact Python/helper cutover

`layout.route(net=None, layer=..., width=None, mode="octilinear", policy=STRICT)`
builds native steps. `.at(endpoint)`, `.through(endpoint, mode=...)`, `.tox(endpoint|x)`,
`.toy(endpoint|y)`, `.left/right/up/down(distance)`, `.via(layer)`,
`.exit(operand,direction,distance=0)`, `.beside(...)`, `.gap(lower,upper,axis)` and
`.to(endpoint,mode=...)`/`.finish()` lower to the closed native grammar. Point inputs
are explicit low-level endpoints; Pad anchors preserve actual owner/placement/pad
and exact footprint identity. Native TrackEnd/Via endpoints and operands pass through.
Numeric tox/toy become literal native ToX/ToY; pad/endpoint versions, including plain
point tuples lowered to RoutePoint, become Align.
Modes map to native Chamfer45/Orthogonal/Direct; old Python octilinear horizontal-first
fallback is removed. No routing grid snap remains. Placement/grid helpers stay separate.

Terminal calls return owned RouteResult, never one arbitrary track ID. Native Strict
is default; Report returns the same native typed rejection and legal Report commits.
Native RouteError text/result passes unchanged. `.query_gap()` returns native prefix
result without closing/mutating builder or layout cursor. A successful terminal updates
layout cursor from native resolved_steps; rejection does not. Explicit retries can be
made on an uncommitted builder; committed builder is closed.

All helper widths default to None and policies default to STRICT; connect, bundle,
via, stitch and fanout accept the same explicit policy keyword. Paired via overrides
require both drill and annular values or neither; one-sided sizing is a Python
argument-shape error before native publication. No inferred Python floor arithmetic.

| Helper | Result and finite signature additions |
|---|---|
| connect(start,end,layer,net=None,through=(),mode) | RouteResult; width=None, policy=STRICT |
| bundle(pairs,layer,net=None,mode) | tuple[RouteResult,...]; width=None, policy=STRICT |
| via(net,at,start_layer,end_layer) | RouteResult; drill=None, annular=None, policy=STRICT |
| stitch(net,at,start_layer,end_layer) | tuple[RouteResult,...]; same sizing/policy |
| fanout(anchors,layer,direction,distance,net=None,via_layers=None) | tuple[BoardFanout,...]; width=None, paired sizing, policy=STRICT |

BoardFanout is a frozen Python presentation value with exactly source: BoardAnchor | None (from the first native resolved step),
end: BoardAnchor | None (from native final resolved step), and result: RouteResult. Remove
old singular track/via fields; callers inspect result.tracks/result.vias. Rejected
Report fanout carries its native result; source and end are None when no final native step was
resolved, otherwise the reported native step source/end (never guessed by Python).

`layout.connect` returns RouteResult; `bundle` returns tuple[RouteResult,...] with
per-item atomicity (no whole-batch claim). `layout.via/stitch` use native Via+Finish,
returning RouteResult / tuple of results; sizing absent inherits native rules and a
paired explicit via override is kept only when both drill/diameter are supplied.
`layout.fanout` uses native Offset(+optional Via)+Finish and returns an owned per-item
result with source and native end, replacing its single-track assumption. Preserve
no fictitious single-ID compatibility layer. Board.add_track/add_via are documented
permissive primitive APIs and stay separate; remove `_add_track_for_route` only once
its superseded fluent callers/tests have migrated. Board.assisted_connect/escape
remain native bounded helpers; they invalidate or refresh retained routing input.

## Private consumer scope and evidence

Review all current trace nets, including existing explicit ground-return traces.
No exclusion is justified merely because a ground net could later be a plane or
USB could later receive pair intent. Existing individual USB traces are supported
single-net routes; future pair constraints are outside this change. Preserve actual
product intent, parts, placement constraints and existing native escape where needed.
One consumer currently cannot import against merged Volt because it uses removed
mutable part-selection/FootprintDefinition APIs. Translate those exact existing
parts/footprints/maps into current immutable Library Parts as a required private
compatibility prerequisite; preserve identity, pin roles/mappings and product tests.
Record its unavailable raw-baseline timing honestly; measure a minimally translated
pre-routing baseline separately, never present that as the unchanged remote base.
Do not relax rules to make Strict routing pass. Supported trace source must contain
zero route-width arguments/constants and zero literal absolute route/via coordinates,
including helper indirection and numeric lane tables. Rules carry intended widths;
routing uses actual pad/copper/edge/keepout operands and native projections.
Mechanical/outline/text/placement coordinates remain legitimate.

The private matrix lists exact nets/required pad connections and source locations;
static inventory follows helper calls, defaults, constants and numeric lane data.
Record before/after inventory, Strict results and native physical connectivity by net.
Build the merged baseline and final version with the same interpreter/build settings;
measure board-stage duration, report samples without asserting speedup. Two fresh
complete builds must produce identical canonical/artifact bytes. View front/back
rendered layers and rerun product-intent tests. Routing scope does not establish full
manufacturing readiness or replace M1's physical-completion inventory (#403/#389).
Shared seam: retained spatial shapes represent the same existing native copper truth;
no new completion definition, zone-fill truth or pair exemption is introduced.

## Required test matrix and delivery

| Area | Evidence |
|---|---|
| Reuse | N successful routes reuse resolution/pads/index; test counters distinguish copies/inserts |
| Incremental parity | mixed tracks/vias with old zones/pads; subsequent full ordered results equal fresh native callers |
| Lazy freshness | every inventory row including Circuit class assignment/default append with copper unchanged, connectivity/port binding, selected closure, profile, placement/rotation/side, stack/layers, outline/features, rooms/rules and external copper |
| Atomicity | cold/warm/stale query, Strict/Report rejection, malformed preflight and allocation fault sweeps: bytes/generation/references/cache unchanged; successful publication no allocating tail |
| Lifetime | constructor foreign Board, temporary/replaced fixed views, Python GC owner retention, stale exact pad identity and borrowed-owner prohibition |
| Grammar | Align endpoint net/layer/owner/identity, literal unchanged, Offset signs/zero/overflow, Finish ordering/no candidate/via-only; full native/Python payload/message parity |
| Cutover | native rule inheritance, all modes/symbolic/inline vias, direct helper parity, Report, cursor/retry/closed builder behavior; removed Python arithmetic |
| Consumer | reviewed full exact net matrix and source inventory, zero targets, Strict and required physical connectivity, product tests, layers, two clean deterministic builds, stage timings |
| Package | tested wheel filename/hash, extension hash/build interpreter/platform/ABI and import location, pinned consumer dependency; source SHA alone insufficient |
| Finish | full relevant native/Python/docs/architecture/package and private checks; independent implementation review resolved; fresh exact-final-head CI |

Commit contract and matrix, record independent findings/resolutions on #384 before
ready/implementation. Deliver linked non-draft public Volt and private Boards PRs,
attach both, keep #384 open while either required consumer/native acceptance remains.
No merges or unrelated cleanup. Public PR/issue evidence is sanitized/synthetic.

## Reviewed pad-factory amendment — 2026-10-14

Independent reviewer approved BoardRouter::resolve_pads() const returning an owned
vector<PadResolution>. Python pad factories consume this same native session rather
than repeatedly decoding selected closure or resolving all pads. The native retained
state may contain current inputs/pads with an absent index. Physical snapshot factories
may lazily warm/refresh runtime pad state and remain canonical read-only; route/query_gap
preparation and rejected routes keep their stronger retained-cache invariance. Cold
pad factories do not construct an index. A refreshed owned return is copied before
noexcept state adoption, so allocation failure preserves the old retained state.
A route privately constructs a missing index, reuses retained pads, and adopts it only
on successful publication. Test repeated factories: one physical/pad resolution and
zero premature index builds; relevant mutation freshness and subsequent route reuse.
No second binding pad cache, borrowed internal accessor or Circuit API change.

## Reviewed RoundedRectangle operand amendment — 2026-10-14

Independent review approved exactly RoundedRectangle in pad/placement symbolic
operands. Use the same transformed rectangular envelope already owned by native
copper collection/DRC; rounded corners do not open a corridor. Rotation and bottom
transforms remain native. No Oval, zone/body, other-layer guide or new physical
completion meaning is introduced. Preserve the original selected private parts.
The unsupported-shape message names Circle/Rectangle/RoundedRectangle. Tests cover
complete query/route payload parity, rotation/bottom, conservative narrow-gap rejection
and retained unsupported Oval behavior. R1b's original bounded scope remains historical;
this dated R1c amendment is the explicit extension.
