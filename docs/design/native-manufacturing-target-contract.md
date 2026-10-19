# Native manufacturing target and closed coverage v1 (#406)

Status: contract for independent review before implementation. Baseline main
`77a7743aa081a86e07555cf43291aa901fbfb66e`. #400/#403 are closed.
This bounded child implements stage 2 of the M1 spec, not readiness delivery.

## Native boundary and identities

`ManufacturingTarget` is a validated, owned typed value in Volt::PCB. It binds exact
CompiledBoardIdentity **and artifact byte digest**, goal, policy V1, process selections,
and a ManufacturingProfile. The profile owns the existing complete BoardCapabilityProfile,
its source-content SHA256, exact process applicability, and optional additional typed
limits (dimension X/Y ranges and minimum via radial annulus). Existing source/as-of are
retained; synthetic sources are explicitly labelled. No ambient reads or vendor branches.
Profile applicability is an exact selection record, not a generic predicate language.
Target process and profile applicability must agree; unknown required selections block.
The capability snapshot must match every field of the artifact's immutable profile,
including name, source/as-of, clearances, weight refinements and optional/empty fields.
Foreign profile/board/bytes yields a Failed identity row, not substituted evaluation.
Targets for different profiles require separately compiled matching artifacts.

All process choices are optional typed input fields so missing differs from unsupported.
Non-finite/nonpositive dimensions, thickness or weights and invalid enum representations
are structural InvalidArgument errors. Supported V1 choices are: RigidFr4, Fr4Tg135,
two copper layers (ordered top/bottom), explicit finished copper oz/ft² per layer,
explicit finished thickness mm, LeadFreeHasl, Both mask sides, Green mask,
Exposed vias, MechanicalThrough drilling, RoundOnly holes, NoSlots, and
Fabrication goal with NoAssembly or FabricationAndAssembly with SmtAndThroughHole.
The finite enums also represent Flex, OtherFr4, Enig, SingleSide/NoMask, OtherColour,
Tented/Filled vias, Laser drilling, BlindBuried holes, Routed/Plated slots and
OtherAssembly: these produce UnsupportedTarget. There are no defaults for omitted fields.
This is synthetic process selection, not approval of a real process. Declared finish,
material and mask choices establish requested applicability only; their actual
realization/review remains an explicit uncovered requirement.

Selection dimensions X/Y are board-axis bounding-box extents, with no rotational
normalization. Board extents, finished thickness and each top/bottom copper weight
must equal the explicit selection independently of capability range/availability.
The stack must contain exactly the two enabled top/bottom copper layers, in that
order, with no additional copper layers outside the stack. Missing metadata is
NotEvaluated; contradictory known metadata is Failed. Numeric comparison uses native board DRC epsilon (1e-9 mm); limits
are inclusive. Native weights are oz/ft² and counts dimensionless. All optional limits
remain unknown when absent; empty capability counts/weights are unknown, never unlimited.
Profile process scope includes thickness, weights and dimensions explicitly, so ranges
are not silently applied to another process selection.

Native semantic canonical encoding V1 uses domain tags, length-prefixed UTF-8 strings,
ordered fields/vectors, explicit optional markers, canonical integers and IEEE double
round-trip decimal representations (zero normalized). SHA256 covers every target field,
including source content, profile provenance and assembly goal. It is separate from
existing artifact capability/provenance digests; no CompiledBoard schema change.

## Fixed leaf inventory

Result is an owned read-only `ManufacturingCoverage`, with exact source identity/bytes,
full compilation provenance, target/profile digests, policy/evaluator V1, an uneditable
fixed array in the order below, and retained native diagnostics. Each leaf has a stable
key, state, owner, reason, ordered scoped observations and provenance. Observations
carry exact ordered EntityRefs, measurement and optional inclusive limits plus units;
counts are derived from observations and reporting entities, not caller assertions.
Required/applicable flags are policy-derived. Rows cannot be removed or overridden.
States: NativePass, Failed, NotEvaluated, NotApplicable. No ReviewedPass input or waiver
API exists in this slice. Required missing inputs, limits or checker/evidence coverage
are NotEvaluated; unsupported selections give UnsupportedTarget and no successful
process-dependent leaves. Failed mandatory leaves cannot be waived. N/A is allowed
only for assembly-only leaves under Fabrication, and NoSlots when no slots exist.
Empty geometric inventories may pass a fully implemented universal check, with zero
observations and an explicit reason. Missing footprint geometry is instead a gap.

Closed ordered inventory (separate leaves, never collapsed to a family pass):

1. ArtifactIdentity; ProcessSelections; ProfileApplicability.
2. LogicalContinuity; RequiredInventory; RequiredContacts; PhysicalConnectivity;
   UnintendedContacts; PhysicalGeometry; ExportFidelity (the seven existing completion rows).
3. CopperWidth; TrackTrackSpacing; TrackPadSpacing; TrackViaSpacing; TrackZoneSpacing;
   PadPadSpacing; PadViaSpacing; PadZoneSpacing; ViaViaSpacing; ViaZoneSpacing;
   ZoneZoneSpacing.
4. ViaDrill; ViaCopperDiameter; ViaRadialAnnulus; PadPthAnnulus; PthDrillRange;
   NpthDrillRange; DrillSeparation; SlotWidth; SlotLength; SlotTreatment;
   OutlineClearance; MechanicalClearance.
5. DimensionX; DimensionY; CopperLayerCount; BoardThickness; TopCopperWeight;
   BottomCopperWeight; MaterialRealization; FinishRealization.
6. MaskOpenings; MaskCopperClearance; MaskWeb; MaskSliver; ViaCovering.
7. LegendHeight; LegendStroke; LegendPadClearance; LegendMaskClearance; LegendClipping;
   RequiredPinOne; RequiredPolarity.
8. PartPackageReview; LandPatternReview; PinMapReview; AssemblyOrientation;
   PopulatedQuantities; DnpQuantities; SelectedAlternates; CplIdentity; CplSide;
   CplRotation; SelectedPartRatings; AssemblyProcess.

First implementation evaluates identity, process/profile applicability, six native
physical-completion rows, copper width (actual tracks, including copper-weight refinements),
via drill and copper diameter (existing profile minima), radial annulus (additional profile
limit, `(outer diameter - drill)/2`), board X/Y dimensions, layer count, thickness,
per-side finished weight, and NoSlots applicability. PTH/NPTH drill-range implementation
is deferred rather than reimplementing all pad/feature coverage here. Every other leaf
remains explicitly NotEvaluated, including ExportFidelity: this slice does not compose
actual export/review evidence. Existing IO fabrication-completion remains available.
PhysicalGeometry's pass is only the existing physical geometry scope, never evidence
that all target DFM leaves passed. Native completion diagnostics are retained verbatim.
No aggregate Ready exists: status is Blocked or UnsupportedTarget, and `ready()` is
always false in V1. Later full M1 readiness requires its own reviewed extension.

Physical rows retain exact completion entities/reasons and diagnostics in their native
order. New numeric observations are in Board ID order; weights are in top/bottom order.
Diagnostics are retained as the existing native ordered payload, including severity,
code, message, entities and overlays. New blockers are leaf records (stable requirement
key plus reason and observation scope), not guessed mappings of old diagnostic strings.
One failing observation fails its row; a gap cannot hide a known failure. Unknown target
identity prevents all source-dependent comparisons; physical evidence may still be
reported as source facts but cannot establish target satisfaction. Unsupported process
prevents process-dependent NativePass. Rows for deferred leaves retain board scope;
assembly leaves have full logical component scope. SelectedPartRatings remains
required under both goals; the other assembly-only leaves are the final block
PartPackageReview through AssemblyProcess excluding SelectedPartRatings. Each leaf
records a stable blocker classification (missing coverage/input, failed constraint,
unsupported selection or identity mismatch) plus actionable remediation. Aggregate
counts report rows per state and exact observation/entity counts. Row entity scope
is a sorted unique EntityRef list; observations preserve source ID order and
compound placement/pad identity where relevant. No dynamic leaf injection.

## IO and Python

Volt::IO supplies deterministic versioned target JSON read/write and report JSON writer.
Target JSON embeds the existing capability-profile document unchanged. Strict format,
schema and policy versions; a domain-specific ManufacturingTargetReadError carries
Malformed or UnsupportedVersion (no generic kernel ErrorCode expansion); unknown/missing keys, duplicate keys, invalid enums/types,
non-finite numeric values, bad hashes and invalid ranges fail with typed kernel/IO errors.
Unknown supported document versions are UnsupportedVersion. No result decoder is needed:
reports cannot be trusted as evaluator inputs. A reopened target plus independently
verified CompiledBoard bytes recomputes the same canonical report without source access.
Python exposes native typed target/process/profile/enum/result objects and codec functions;
existing compiled artifact bytes are decoded by native IO. Python performs no filtering,
applicability, numeric comparisons or EDA policy. No project writer/gate changes.

## Acceptance matrix

| Area | Required cases |
| --- | --- |
| Each implemented numeric leaf | Above/below and exactly at every minimum/maximum; correct units, exact ordered entity scopes and derived counts; empty inventory and missing prerequisite coverage |
| Process/profile | Every missing field, each finite unsupported choice; mismatched applicability; malformed typed enums/nonpositive/non-finite fields; same evaluator with two labelled synthetic profiles, changed limit outcome and target/profile digests on matching compiled artifacts |
| Immutable identity | Foreign board, artifact bytes and profile; change every profile provenance/limit field and target goal/selection; no artifact mutation |
| Physical reuse | Full completion row/diagnostic payload parity; missing all/last placement, short/unrouted, unsupported geometry and V1 authoritative incompleteness; existing routing/completion full regressions |
| Closed inventory | Exact complete ordered key list, no missing/unknown/duplicate rows accepted, no caller N/A/waiver/manual override; deferred mask/legend/polarity/assembly/export checks block even when all implemented checks pass |
| Goal applicability | Fabrication keeps assembly-only rows explicit N/A with native reason; assembly goal keeps every such row NotEvaluated; rating identity not weakened |
| Canonical codecs | Byte-stable write/read/write, reordered JSON same semantic digest; source-free fresh/reopened targets/artifacts same report; strict malformed/unsupported version/unknown key/duplicate key/type/hash cases |
| Cross-language | Direct C++ fixture emits report; Python evaluates identical artifact/target bytes and compares entire JSON bytes including statuses/messages/entities/counts/measurements/provenance/diagnostics |
| Delivery | Full native/Python, architecture, docs, package/downstream, routing/completion; independent implementation review; non-draft PR closes #406 only; six fresh exact-final-head CI jobs; no merge |

No later assembly/rating seam, human evidence trust/binding, export refusal, package graph,
private assets or real manufacturer claim. Parent #389 stays open and not ready.
