# Native physical completion contract (#403)

Status: independently reviewed finite contract (gate recorded on #403 on 2026-10-14);
native implementation and acceptance tests accompany this contract. Baseline:
`f8c428584be5c7b5bb03d81bccfc3f4bcd791fa4`, verified 2026-10-14.
Parent #389 remains open/not ready. This child proves physical completion only;
it does not implement manufacturing-target readiness or authorize manufacturing.

## Native seam and ownership

PCB provides `evaluate_board_completion(const CompiledBoard&)` returning an owned,
read-only `BoardCompletion`. No resolver, options, authoring callback, Board copy,
Circuit mutation or schema change. IO provides
`evaluate_fabrication_completion(const CompiledBoard&)`: it exports the same artifact
once, evaluates native facts, and attaches checked actual file identities/losses.
The core result cannot complete its export-fidelity row by itself. Only the IO
composition can attest that row; there is no caller-supplied export boolean.

The result contains exact artifact identity/content digest, evaluator version,
ordered component/pin inventory, required mapped terminal contacts, all analyzed
copper entities/layers/optional logical labels, physical islands, fixed coverage
rows and diagnostics. Counts are derived from those exact inventories. Each row
has `Pass`, `Failed` or `NotEvaluated`, exact scope/count and reason. Overall status
is `Complete`, `Incomplete` (any proven failure), or `NotEvaluated` (remaining
coverage gaps). Complete requires every row Pass. It is never manufacturing Ready.
Fixed rows: logical continuity; logical/physical inventory; required connected
contacts; physical group connectivity; unintended copper contacts; supported
geometry; native export fidelity. Proven failures remain reported alongside gaps.

IO writes a deterministic version-1 JSON report (`volt.board-completion`), including
full diagnostic payloads, exact provenance, inventory, island memberships, coverage
and file function/name/digest. This is a derived report, not an editable source or
new CompiledBoard format. No report reader/restoration API is necessary: reopen the
CompiledBoard and recompute. Python returns the native JSON payload without semantic
filtering/reclassification; source-free compiled artifacts have the same entry point.

## Logical and terminal inventory

Enumerate all artifact-owned components and pins first in typed ID order. Board is
one complete physical alternative. Absence of DNP means populated, following native
`component_dnp(...).value_or(false)`. Every populated component needs exact selection,
resolved footprint and placement even with optional/disconnected pins. Unplaced DNP
components need no terminals; all placed DNP geometry and connected pins participate.
DNP never removes another populated endpoint or stored copper. Legacy DRC
`PCB_COMPONENT_NOT_PLACED`, `PCB_COMPONENT_MISSING_SELECTED_PART` and
`PCB_FOOTPRINT_UNRESOLVED` for an exactly matched unplaced DNP component remain
in diagnostics but are outside this child's required physical-implementation scope; this applicability
does not change legacy DRC or waive a populated endpoint. Thermal pads require mapping
in the exact native model; non-electrical/mechanical conductive pads carry no label.

Intrinsic `MustNotConnect` or terminal kind `NoConnect`, then explicit intentional
no-connect, take precedence over default Required. A contradictory logical connection
fails even on DNP. Otherwise an unconnected Required populated pin fails; optional
unconnected pins need no route. Explicit no-connect does not assign net meaning.

Every connected populated pin and every connected pin on a placed DNP needs every
native mapped copper contact. One pin may have several pads; a pad cannot map to
several pins. Check mappings against frozen resolutions and concrete pin IDs. Missing
selection/footprint/placement/mapping/copper layer/contact is diagnosed, never omitted.
Structurally malformed exact mapping/foreign pin/pad remains a native structural
rejection at existing boundaries; acceptance tests exercise those rejections too.

Use `CompiledBoard.view().logical_continuity()` and its preserved V2 partition;
never net-name/origin inference or hierarchy restoration. All required mapped contacts
in a logical continuity group must share one physical island. A singleton needs its
endpoint but no invented destination. Detached labelled copper cannot substitute for
any endpoint. V1 inventories remain inspectable but continuity/connectivity/short
coverage is NotEvaluated with recompile remediation; ordinary V1 export stays usable.

## Copper and geometry

Collect tracks (one capsule per segment), vias (disc on native span), Solid zones
(polygon on each native layer), and every resolved footprint copper pad, including
DNP/disconnected/unmapped/non-electrical pads. Preserve optional net labels. Layer
sets use existing native pad side/all-copper and via stack-span semantics. A plated through-hole pad/via connects its occupied layers; non-plated/surface
pad layers remain separate conductors. A multilayer zone consists of
separate planar conductors and does NOT connect layers. This corrects manufacturing
analysis only; legacy DRC/routing inventories retain their accepted behavior.

Factor shared primitive distance/contact operations in native PCB; reuse a finite
union-find helper without changing legacy DRC island semantics.
Existing DRC uses the same factored distance with its old labelled-only inventory,
same-group filter and `1e-9 mm` tolerance. R1a/R1b grammar, clearance/rule resolution,
spatial obstacle semantics and unlabelled-pad route legality remain unchanged.
No second geometry engine. New all-copper inventory carries optional labels without
changing legacy `BoardCopperShape`'s mandatory net contract.

Analyze Circle (equal dimensions, exporter width diameter), Rectangle, track capsule,
via disc and Solid polygon. RoundedRectangle is reported lossy/NotEvaluated, even
though its exported rectangle can expose proven shorts. Oval is unsupported and
cannot supply a required contact. Unsupported export layer/span and any fab-critical
loss block authoritative completion. No unresolved pour/future fill interpretation.

Finite export-grid support: each emitted copper coordinate and aperture dimension
must be representable on native Gerber 4.6 grid (`1e-6 mm`, within `1e-12 mm` floating
representation error), and emitted coordinates must fit the four-digit integer
range (absolute value at most 9999.999999 mm). Preflight this bound before native
integer conversion; unsupported range returns NotEvaluated without fabricated file
evidence. Validate actual emitted coordinate-token widths too. Check transformed
pad corners/centres as actually emitted.
Off-grid geometry is NotEvaluated, rather than silently certifying authored contact
that rounding may change. IO checks this against the actual writer quantization;
synthetic tests inspect Gerber coordinates/apertures/regions, not only a coverage flag.

Robust contact uses shared native primitive geometry. Overlap beyond the existing
`1e-9 mm` numerical uncertainty is proven contact; separation beyond it is proven
separation. Tangency/near-boundary contact that depends on numerical tolerance is
NotEvaluated unless exact emitted contact agreement is proven by the implementation.
Polygon interior overlap is supported; mere shared boundary must not masquerade as
interior overlap. This finite limitation is explicit coverage, not a false pass.

Finished copper also respects emitted drills and mechanical openings. Reuse native
opening/primitive distance predicates, not a new polygon-subtraction engine. A circular
opening strictly contained within a conductor, separated from its outer boundary and
other openings, is non-severing; plated pad/via barrels keep native interlayer continuity.
An opening that can cut a conductor (boundary intersection, overlapping holes, slot,
cutout, unsupported opening treatment) makes affected geometry/connectivity coverage
NotEvaluated. For contact involving a drilled region, require a concrete shared-copper
witness outside every opening, using existing primitive point-distance predicates and
finite boundary/centre samples. Failure to find a witness is uncertainty, never proof
of contact. Common supported pad/via-to-Solid interior contacts have such witnesses.
A contact confined inside a drill must not union conductors; add drill-only contact and
opening-severed conductor negative fixtures. IO attaches actual drill/export loss facts.

Physical union considers every conductor independent of its label. Distinct preserved
logical groups on one physical island fail as a short; include complete exact island
entities and labels, including netless bridges. Same-net labels on disconnected islands
do not connect them. Contact between an intentionally isolated/no-connect pad and any
other emitted conductor is an unintended-contact failure (even a same-label/no-net
bridge); optional disconnected pins are isolated too. Contact never assigns a net.

## Diagnostics, scope and regressions

New finite stable catalog codes cover missing implementation/placement/mapping/contact,
required logical connection/no-connect contradiction, disconnected required group,
short/isolated contact, unavailable continuity, unsupported geometry/quantization and
missing export evidence. Include exact ordered entities, count and measurement where
known (mm, threshold), diagnostic source and remediation in messages. Use reporting
EntityRefs only; traversal uses typed Circuit/Board IDs and frozen pad records.
Existing DRC/export diagnostics remain attached verbatim and cannot be waived.
Within inventories use typed component/pin/placement/pad/layer order; islands use the
lowest copper item index; diagnostics/coverage use stable requirement/entity/code order.
Read-only verification compares artifact/source bytes and owner state before/after.

## Acceptance matrix

| Case | Required outcome / evidence |
| --- | --- |
| Supported synthetic two-layer Solid ground contacts, tracks/vias/pads | Complete only with IO fidelity; actual Gerber region/aperture assertions |
| Bound module groups; fresh/reopened source-free V2 | Identical full native JSON, provenance, groups and diagnostics |
| One pin/many pads; genuine singleton | Every mapped contact present/connected; singleton endpoint required |
| Remove last/all/one populated placements | Exact component/pin inventory retained; missing placement fails |
| No selection / missing footprint / malformed mappings | Missing selection/footprint fails; invalid mappings reject structurally |
| Required unconnected vs optional connected without endpoint | Distinct logical failure vs required physical-contact failure |
| DNP unplaced vs placed copper | Absent exempt; placed copper/connected pins participate and can short |
| Explicit NC/default Required; MustNotConnect; intrinsic NC | Disconnected allowed; contradictory connection and physical contact fail |
| Different layers; incorrect via span; multilayer zone | No invented interlayer contact; unsupported emitted span NotEvaluated |
| Zone removed/shrunk; contact moved across boundary | Disconnected group fails; tangent/uncertain grid explicitly NotEvaluated |
| Labelled/netless pad/zone bridges | Exact physical island short; logical snapshot unchanged |
| Oval / rounded rectangle / fab-critical feature / off-grid copper | Unsupported/lossy/not-evaluated coverage, never Complete |
| Drill-only apparent contact; opening severing copper | No false union; exact affected scope NotEvaluated, plated barrels retained |
| V1 reopened historical | Export remains available; authoritative continuity NotEvaluated |
| Native vs thin Python / repeated evaluation | Full ordered payload/message/provenance equality; deterministic bytes |
| Legacy DRC/routing/CompiledBoard | Full native/Python regressions; explicit contact-factor regression |
| Final PR head | Independent review and fresh Linux/macOS/Windows, ASan/UBSan, static analysis, coverage CI |

Public fixtures remain synthetic; no private Boards assets. No target-policy framework,
BOM/CPL rewrite, rating identity relaxation, publication changes or package graph work.
Contract review is committed and recorded on #403 before adding child ready. No merge.
