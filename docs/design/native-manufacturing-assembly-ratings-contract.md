# Exact assembly consistency and selected-part ratings (#408)

Status: proposed independent contract gate, 2026-10-19. Baseline main
`09380d315abb02622620cb5863e591d68ca481e4`; closed prerequisites #400/#403/#406.
This is stage 3 of the M1 design. #389 stays open without ready. No merge authority.

## Boundary and compatibility

Evaluate only the immutable CompiledBoard and its exact ManufacturingTarget. Preserve
all 65 ordered leaves, source/profile/target matching, target CPL canonicalization,
physical completion and immutable archive bytes. No new artifact schema, Circuit
mutation/restoration, library reload, callback, Python policy, or manufacturer claim.

Target/profile semantic identity, target JSON schema/policy V1, and all strict target
reader behavior remain unchanged. They already bind the complete rotation options and
required inventory. The coverage report schema/evaluator becomes V2 because newly
implemented leaf meanings and native assembly/rating evidence extend its output. Its
policy remains V1: required rows, goal applicability and non-waiver rules are unchanged.
Historical V1 reports are not read or silently upgraded; no report decoder exists.
Existing target documents reopen unchanged and are explicitly evaluated by evaluator V2.
Old diagnostics remain byte-for-byte intact at their current APIs and in completion.

Native result owns optional BOM/CPL projections and a typed selected-rating assessment;
IO writes these using existing projection/diagnostic codecs plus finite new rating
records. Thin Python evaluates native artifact bytes and exposes the identical report.
No trusted external projection/report input or result mutation API is added.

## Preconditions, inventory and assembly

Inventory every logical component in ComponentId order before consulting projections.
An unknown explicit DNP flag means populated (existing queries), but is a Failed
PopulatedQuantities assembly obligation: author explicit fitted/DNP intent. Missing
selected part for a populated occurrence fails quantities, selected identity and CPL
identity; it cannot disappear by producing no BOM line or CPL row. A selected DNP part
has zero purchase quantity; an unselected DNP occurrence needs no fabricated identity.
Missing intent never becomes a pass through an empty projection.

For assembly goals, reuse project_bom(circuit, compiled.part_definitions()) and
project_cpl(compiled, *target.cpl_options()) once. Require matching artifact identity,
supported complete process/profile and explicit CPL options before target-dependent
assembly success. Foreign/stale identity or missing preconditions yields NotEvaluated
assembly/rating rows; original native completion remains inspectable. Fabrication-only
leaves retain the existing explicit N/A, except SelectedPartRatings remains required.

- PopulatedQuantities: native BOM occurrence IDs/references/explicit intent/selection
  match the complete inventory exactly once. Each selected orderable grouping
  (manufacturer, MPN, package, DNP) has the exact sorted reference multiset and purchase
  quantity equal to non-DNP occurrence count. Compare native grouping results rather
  than creating a second BOM projection. Known failure dominates missing coverage.
- DnpQuantities: selected DNP grouping quantity is exactly zero and every explicit DNP
  occurrence is represented once in BOM components, including unselected DNP. No DNP
  flag suppresses native copper/short/required-populated-endpoint completion failures.
- SelectedAlternates: each populated occurrence preserves all fields of its exact
  selected LibraryPartRef, selected full Part/orderable identity and materialized
  physical identity. BOM approved-alternate lists and selection-override references
  equal native selected occurrences. Approved MPNs are sourcing metadata, never a
  substitute selection or resolver key; same-MPN distinct Parts remain distinct exact
  selections even if grouped into the same purchase line.
- CplIdentity: each required populated occurrence has exactly one native row, matching
  placement/component/reference, exact selected orderable identity and footprint,
  position x/y, and the artifact-owned resolved footprint. Extra/duplicate/missing
  occurrences fail. DNP rows are forbidden. An absent required placement fails every
  applicable CPL consistency leaf; an empty row set cannot establish completeness.
- CplSide: exact authored top/bottom side for each required placement.
- CplRotation: native normalized authored degrees plus the exact target-owned correction,
  normalized to [0,360), using native 1e-9 zero/360 normalization. Reuse the existing
  native rotation calculation so exporter and coverage cannot drift. Target duplicate
  footprint corrections (identical or conflicting) reject structurally as delivered;
  standalone CPL retains its existing duplicate/conflict diagnostics. No last-writer wins.
  Finite large-degree inputs must not create non-finite sums or a passed NaN observation.

Keep existing CPL diagnostics verbatim in owned assembly evidence. Numeric consistency
does not satisfy AssemblyOrientation, PartPackageReview, LandPatternReview, PinMapReview
or AssemblyProcess. These remain required NotEvaluated for assembly.

Assembly rows have sorted unique complete occurrence/placement scopes. Observations
follow native BOM line order for quantities (Count), then component ID and placement ID
for CPL x/y (Millimetres), side (Count), and authored/offset/final rotation (Degrees).
Evidence retains full native BOM/CPL occurrence identities and exact selection refs.
Zero populated/DNP inventories may pass a complete universal assembly consistency check
only with an explicit empty-inventory reason and the complete logical inventory retained.

## Narrow exact rating-subject seam

Do not adapt ElectricalInput, reconstruct Circuit, synthesize component definitions,
cast the retained resolver to ExactPartResolver, or weaken live ERC identity checks.
Add a private native bridge from CompiledBoard's verified storage into the existing
selected-part ERC owner. The bridge supplies each occurrence's exact full retained
PartDefinition, original ComponentDefinition, the artifact Circuit pin instances and
its preserved native NetContinuityView. Full component identity must equal the exact
Part implemented_component; exact LibraryPartRef integrity remains resolver-owned.
Supply an explicit original ElectricalPinIndex-to-artifact PinId table, mapping
through original ordered PinKeys to the already verified projected pin order; closure-local PinDefIds are never used as Circuit handles.
The CompiledBoard codec already verifies complete projected logical contract equality
against full original payloads. The bridge is internal/friend-gated, not a public
caller-supplied identity override, resolver callback, or parallel semantic engine.
V1 compiled artifacts remain exportable; absent authoritative continuity makes rating
coverage NotEvaluated even for apparently flat circuits.

Extend src/library/selected_part_erc.cpp with owned typed per-obligation coverage over
its existing subject projection, effective claims and comparison rules. The public live
validate_selected_part_erc boundary keeps strict identity and existing diagnostic
payload/order. New compiled assessment uses only populated claims: explicit DNP subjects
are recorded N/A and cannot supply voltage/current guarantees to populated consumers.
Missing explicit intent uses existing populated default for ratings, while assembly
intent remains a separate failure. DNP does not hide emitted copper or other native failures.

## Finite supported ratings and missing inputs

The covered families are the existing Voltage AcceptedRange, Voltage AbsoluteLimit,
Current AbsoluteLimit, and continuous Current Requirement/Capability budget. Support
only unconditional, known, conflict-free effective claims on fully projected oriented
subjects with two distinct native continuity groups. Use existing canonical record
merge/query semantics; do not infer conditions, source-sharing, current, thermal, SI,
Power, functional safety, or model-derived operating points. Characteristics are
metadata for this check, not ratings. Unsupported non-characteristic record combinations
(including Current AcceptedRange) are explicit per-subject NotEvaluated obligations.

| Obligation | Required evidence for a checked result |
| --- | --- |
| Voltage AbsoluteLimit | Known unconditional limit; known authored positive/return potentials (ground kind supplies 0), or compatible other populated ProvidedRange on same oriented native domain. Every bounded side of the limit must have a known comparison side. Check all applicable authored/provider values; missing one applicable provider bound remains a gap. |
| Voltage AcceptedRange | At least one other populated known unconditional Voltage ProvidedRange on the same oriented domain, with each required bound present; compare every applicable provider. Authored nominal voltage alone does not prove a ProvidedRange. |
| Current AbsoluteLimit | At least one other populated known unconditional Current ProvidedRange on the same oriented domain with each required bound present. No simulation or generic net-current inference. |
| Continuous Current budget | At least one known unconditional requirement, exactly one compatible unconditional populated capability, all relevant requirements known; sum native requirements and compare inclusively. Missing/conditional/unknown requirements or capability, multiple sources and non-finite sum are NotEvaluated. |

Each canonical non-characteristic semantic-key group contributes one obligation, retaining
all ordered source records/values/conditions/evidence within that group, including
providers/capabilities: a provider is checked only when it actually participates in a
supported comparison or budget. An unpaired provider/capability is NotEvaluated. Repeated
effective constraints use the existing range intersection/max-demand/min-capability
rules; conflicting values, Unknown mixed with known, and conditional inputs cannot
certify a pass. No empty range-side comparison counts as execution. Missing selected
part, missing required record shape, missing subject endpoint, absent provider/demand,
unsupported meaning or unavailable continuity records an exact gap with reason.

The authored-voltage route retains the live checker's canonical minimum-ID net
representative. If only a noncanonical continuity-group member has a known authored
potential, or any member contradicts the canonical value, that route is NotEvaluated.
Do not redefine live ERC or silently discard these assertions. Test both cases.
Native current aggregation remains once per (component, projected domain): max demand
and min capability before summing distinct occurrences; semantic-key aliases never
double-count demand. Non-finite arithmetic (including potential subtraction and demand
summation) is NotEvaluated, never a passed numeric observation.

One typed outcome records component/semantic-key group, closed family/rule, state,
exact ordered component/pin/net scope, comparison measurements/units and bounds where
known, and exact Part reference/evidence provenance. Output is component ID then record
semantic key then comparison source/domain order; deterministic explicit component gaps
precede records. Source-local keys identify records; EntityRefs remain reporting only.
The compiled assessment emits failures only for actually supported unconditional
comparisons; copied legacy conditional/collapsed diagnostics cannot establish Failed.
Native diagnostics retain existing messages/entities/rule provenance for supported failures. Do not fabricate diagnostics by parsing messages.

SelectedPartRatings row is Failed if any supported native rating comparison fails;
otherwise NotEvaluated if any populated occurrence/obligation is unknown or unsupported;
otherwise NativePass only if at least one supported rating obligation was actually
checked and every applicable subject was covered. Empty circuits, populated parts with
no non-characteristic obligations, and all-DNP circuits retain an explicit NotEvaluated
reason (no checked rating obligation); they cannot pass merely because no checks ran.
Characteristic-only parts do not imply that unknown ratings are safe. N/A is recorded
per DNP subject, without changing the existing leaf-level applicability policy.

## Acceptance matrix and delivery

| Area | Positive, negative and compatibility evidence |
| --- | --- |
| Inventory/BOM | Grouped populated refs/quantities, DNP zero including unselected DNP, mixed intent, exact selected override vs approved alternate, same-MPN distinct exact parts; missing selected part, missing explicit intent, complete empty inventory; full exact expected native rows catch omitted/duplicate/wrong quantity projections without exposing corruptible evaluator inputs. |
| CPL | Multiple top/bottom placements, negative/360/>360 degrees, explicit empty/nonempty offsets, offset-only target digest/report change; missing first/last/all required placements, unselected populated part, DNP omitted; exact position/identity/side/rotation payload assertions, target duplicate/conflicting keys and finite-overflow boundary; native occurrence cardinality remains structurally enforced. |
| Ratings | Each four supported family has genuine checked positive, exact-boundary and violation cases; framed/relation/supply subjects, shared domains and preserved hierarchical continuity; different selected part vs approved alternate, symbol-bearing original identity; missing/foreign/stale selection, unresolved endpoints, absent ratings/required data, Unknown/conflict/conditional/partial-bound inputs, canonical/noncanonical voltage assertions and arithmetic overflow, unsupported Current AcceptedRange, unpaired source, multiple current sources, DNP source/consumer, all-DNP/empty subjects; failure takes precedence over another gap. |
| Closed coverage | All 65 ordered leaves/owners/reasons/entities/counts/observations/provenance retained; goal distinction, ratings under both, native DNP copper/short/required endpoint failures remain visible; deferred review/DFM/export/delivery still block. |
| Integrity/lifetime | Re-evaluate fresh and reopened native artifact/target after original owners are destroyed; complete report equality, no ambient resolver or callback, byte-identical immutable inputs, foreign artifact/profile/target and legacy V1 continuity gaps. |
| Codec/API | Unchanged target V1 golden identities/read-write bytes; explicit report schema/evaluator V2 and policy V1, no V1 report reinterpretation; strict current target negatives/unsupported versions remain; installed native SDK and downstream link consumers; direct C++/thin Python and final source-free wheel full report parity. |
| Finish | Full relevant native/Python/architecture/docs plus routing/completion/target regression suite; separate independent contract approval before ready, blocker recheck and decision on #408; separate implementation review, actual PR feedback resolved, non-draft PR closing #408 only; all six fresh final-head CI jobs terminal SUCCESS; exact head/base/review receipts; no merge/auto-merge. |

No manual-evidence trust/export binding, file composition, missing DFM implementation,
publication/gate change, #390 graph, private Board assets, vendor order/upload, generic
safety engine, routing extension, or unrelated refactor. Legacy unplaced-DNP diagnostics
remain unchanged for later delivery refinement.
