# M1: Native manufacturing-target readiness

Status: proposed refinement for [#389](https://github.com/RyanWillie/Volt/issues/389).
This document does not implement readiness, approve a manufacturing process, or mark
the issue `ready`. Acceptance of the decisions below is the implementation gate.

Baseline: `origin/main` at `c43c9f0d8d53ed32a39e55418de51f5da4170689` (2026-10-09),
including package-publication #394 and visual-regression #395.

## 1. Outcome and boundary

Answer whether one immutable, named `CompiledBoard` satisfies a declared supported
fabrication target, or that target plus populated-component assembly requirements.
Return explicit missing checks and actionable design blockers. A successful build,
empty ratsnest, or successful export is not this answer.

C++ owns requirement classification, rule evaluation, physical completeness and the
readiness result. IO owns canonical codecs, checked evidence-file identity and native
fabrication output. Python supplies typed inputs and orchestrates project tests and
publication; it does not filter blockers or implement geometry/manufacturer rules.

No new `Circuit` methods, mutation facade, netlist definition, entity deletion, live
manufacturer lookup, autorouter, universal DFM framework or electrical-function claim.
The historical Board remains unchanged on every evaluation.

## 2. Verified starting points

These are reuse seams, not evidence that every manufacturing requirement is checked:

- [CompiledBoard](../../../include/volt/pcb/compiled/compiled_board.hpp) freezes logical,
  physical, selected-closure and capability provenance. Its logical snapshot retains
  component instances, pins, nets and assembly intent, not only placed components.
  It currently drops module instances and their port bindings: required logical
  continuity is not fully preserved. Section 4's versioned closure prerequisite
  must be delivered before claiming completeness on reopened hierarchical boards.
- [CompiledBoard consumers](../../../include/volt/pcb/compiled/board_consumers.hpp)
  provide exact-revision native validation and CPL projections.
- [Unrouted validation](../../../src/pcb/copper/board_copper_validation.cpp) starts
  from placed-pad ratsnest edges; missing pad endpoints are skipped. It emits a
  warning. This is insufficient as a completeness inventory.
- [Copper geometry](../../../src/pcb/copper/board_copper_geometry.cpp) treats current
  Solid-zone polygons as copper; [fabrication](../../../src/io/pcb/pcb_fabrication_writer.cpp)
  emits their outlines as filled Gerber regions. Its current labelled shape inventory
  skips netless zones and disconnected/unmapped pads although export can emit them;
  extend that native inventory rather than claiming the existing check is complete.
- [Capability validation](../../../src/pcb/validation/board_capability_validation.cpp)
  checks existing profile fields. Several optional fields or missing layer values
  skip a check; readiness must distinguish that from a checked pass.
- [BOM](../../../src/circuit/bom/bom.cpp) already owns quantity, explicit DNP and
  approved-alternate projection. [CPL](../../../src/pcb/assembly/cpl.cpp) already
  diagnoses unplaced populated components and projects side/rotation.
- [CompiledBoard IO](../../../src/io/pcb/compiled_board.cpp) verifies and decodes full
  selected `PartDefinition` payloads, but the current public consumer exposes the
  materialized `PhysicalPart`, not a full exact-part resolver for BOM/P1 validation.
- [Manufacturing publication](../../../python/volt/manufacturing.py) now safely
  publishes schema-v2 packages. Its current `result.ok` and export-loss checks do
  not supply the proposed readiness contract.

## 3. Proposed native input and result

Introduce one domain-specific free evaluator; names below are proposed, not callable
API examples:

`evaluate_manufacturing_readiness(CompiledBoard, ManufacturingTarget, ReviewEvidence)`

The core evaluation accepts an immutable artifact and finite typed records, never an
authoring callback, ambient library resolver, filesystem path or Python predicate.

### Target

`ManufacturingTarget` contains:

- Goal: `Fabrication` or `FabricationAndAssembly`.
- Versioned supported process-policy identity and a digest of its canonical bytes.
- Exact capability-profile snapshot, source URL, source capture/content identity and
  as-of date. Its native profile must match the CompiledBoard capability input.
- Explicit process selection: material, copper layers/weights, thickness, finish,
  mask colour and through-hole/via treatment. Units and enum values are typed;
  omitted required selections produce blockers, not inferred manufacturer defaults.
- For assembly, exact native `CplProjectionOptions` (including an explicitly empty
  rotation-offset set). Canonicalize and include these in target identity; a
  conflicting offset is a native assembly diagnostic, not last-writer-wins data.

Propose the first supported family as conventional two-layer rigid FR-4, mechanically
drilled through holes/vias, current native export geometry and no impedance/HDI,
blind/buried vias, castellations, plated edges or panelization requirements. Other
well-formed process combinations return `UnsupportedTarget`, never a partial pass.
Assembly means complete populated BOM/CPL plus declared assembly-review requirements,
not automatic eligibility for any vendor's assembly service.

The accepted target policy has a closed required-row inventory (section 6); callers
cannot delete rows, switch a native failure to manual, or assert arbitrary N/A.
Process-dependent numeric limits are pinned data with cited applicability, not Volt
constants or live website queries. A profile name/date alone is not full coverage.

### Result

`ManufacturingReadiness` records the exact `CompiledBoardIdentity`, target digest,
evaluator/schema version, ordered coverage rows, ordered blockers and advisories.
Its status is `Ready`, `Blocked` or `UnsupportedTarget`; `ready()` is derived.

Each row has a closed requirement key, required/applicable flag, enforcement owner,
source/rule provenance, entity scope, measurement/units where applicable, and state:
`NativePass`, `ReviewedPass`, `NotApplicable`, `Failed` or `NotEvaluated`.
Required rows pass only in the first three states, with N/A allowed solely by the
native policy's applicability predicate and a recorded reason. Missing evidence,
missing process data and incomplete checker coverage are `NotEvaluated` blockers.

A blocker retains a stable diagnostic code, exact ordered reporting entities/count,
rule source, measurement when known and suggested remediation. Reporting `EntityRef`
values do not become traversal handles. Reuse existing diagnostics where applicable;
add readiness-specific codes for uncovered requirements and physical completeness.
Ordering is requirement key, typed entity tuple, then stable code; do not sort on
localized messages or filesystem paths. Existing diagnostic payloads remain intact.

Malformed IDs, dangling references, inconsistent verified artifacts and invalid typed
records retain kernel/IO structural errors. Representable bad board design yields a
blocked result, not a structural exception or a partially successful artifact.

## 4. Physical-completeness semantics

Each named Board is a complete physical alternative for its Circuit in this slice.
No board-local omission list, implicit multi-board partition or test that simply
declares unfinished components out of scope is introduced.

1. Inventory every logical component, selected exact implementation, assembly intent
   and pin from the artifact-owned Circuit before consulting the ratsnest.
2. Every non-DNP component requires an exact selected part, resolved footprint and
   placement on this Board. Absence of DNP means populated, matching existing native
   queries, but missing explicit assembly intent blocks the assembly goal.
3. DNP affects assembly quantity, not emitted copper or netlist meaning. DNP parts
   already placed still contribute pads/clearance/shorts. An unplaced DNP part is not
   a required assembly terminal; DNP may not suppress another populated endpoint.
4. An explicitly no-connect or `MustNotConnect` pin must remain logically disconnected;
   a contradictory connection is a diagnostic. Preserve native explicit no-connect
   intent precedence over a pin's default `Required` classification. Otherwise a
   `Required` non-DNP pin without a logical connection blocks. Optional disconnected
   pins do not demand routes.
5. Every connected pin of a non-DNP physical component, including optional pins that
   were connected, requires its valid native pin-to-pad mapping and placed endpoint.
   Honor the selected part's supported mapping cardinality; do not assume one pin
   equals one pad. Every mapped contact required by that mapping participates.
6. Use native `NetContinuityView` semantics, including bound module ports, to form
   logical groups from the preserved exact-revision closure described below; do not
   merge nets in PCB/Python. All required terminal contacts
   in each group must belong to one physical copper-connected group. A genuine
   single-terminal connected group needs no invented destination, but its endpoint
   must exist and unrelated ERC diagnostics still apply.
7. Unmapped conductive pads are never silently assigned a net. Follow native exact
   mapping semantics and check their copper interactions; an ambiguous required
   contact blocks. Detached same-net copper cannot substitute for a missing pad.
8. Short detection and clearances cover all emitted copper, not only required pads,
   and no DNP/no-connect intent waives a physical short. Preserve optional logical
   labels on netless zone/pad geometry: a physical copper group reaching two distinct
   logical continuity groups is a short even when its bridge has no net label.
   Contact to an intentionally isolated/no-connect pad is not an implicit net
   assignment or a valid implementation of another terminal.

### Required logical-continuity closure decision

The current snapshot builder removes `module_instances`/`module_definitions`, while
the logical writer stores port bindings inside module instances and native
`NetContinuityView` derives continuity from `PortBindingId` rows. The remaining
module origin names are reporting provenance, not connectivity. Reusing only
`compiled.board().circuit()` cannot recover those lost relationships.

Propose the focused [logical-continuity ADR](../../design/adr-compiled-board-logical-continuity.md):
add the canonical, native-derived realized net partition to the logical dependency
closure under a new CompiledBoard schema/compiler contract. Derive it from the
original Circuit's existing native continuity service at compilation; consume it
through the same native query/validation semantics after reopen. It never merges
or changes original nets/pin memberships and is not a PCB-authored connectivity
definition. Do not add hierarchy mutation/restoration APIs to public Circuit.

The accepted [CompiledBoard ADR](../../design/adr-projection-ownership-and-compiled-board.md)
requires focused review and a new compilation contract when closure contents change.
This proposal is that explicit review, not an ad-hoc downstream snapshot extension.
All schema-v1 artifacts remain readable/exportable, but native manufacturing
completeness is `NotEvaluated` with a recompile remediation because v1 does not
prove that logical continuity was preserved. No guessing from net names/origins,
ambient hierarchy reload, or silent version upgrade is permitted.

Factor the existing copper-connected-component calculation for reuse rather than
building a second geometry engine. Solid zones count only on exported supported
layers with actual pad/via contact; polygon boundary-contact tolerance must agree
with native export quantization. Existing outline, mechanical, clearance and export
loss checks must pass. Future unresolved pours, islands or intended boundaries never
count as connections. R2 is not a prerequisite for current Solid-zone truth.
Unsupported oval-pad export remains a non-waivable fab-critical blocker in the
current exporter; the copper check's enclosing-disc approximation must not bypass
that gate. Other lossy geometry is classified against actual emitted shape and
target requirements, not merely its authored ideal outline. Contact that depends on
unproven export rounding must block as not evaluated until parity is established.

## 5. Exact parts, BOM, CPL and supported ratings

Reuse the native exact selected-part validation and BOM/CPL projection. The first
implementation must preserve full verified `PartDefinition` access from the selected
archive closure for those consumers: retain already decoded definitions under the
CompiledBoard owner and provide only the narrow read-only/resolver seam they need.
No ambient bundle reload or host path lookup; a missing selection remains a design
blocker. This is an owner-storage/consumer extension, not a new Circuit mutation API.
Existing canonical part payloads must round-trip unchanged; a format extension, if
actually needed, requires an explicit versioned compatibility test and review.

For the assembly goal, prove BOM quantities equal the non-DNP component inventory,
DNP quantities are zero, and every populated component has exactly the native CPL
row with selected identity, side and rotation/correction from the target's pinned
`CplProjectionOptions`. The native evaluator and delivered CPL use that same option
set; changed options require a new target/readiness identity and orientation review.
An approved alternate MPN
is not a selected substitute: changing the selected part requires a new artifact.
Reject conflicting CPL offsets; default numeric rotation is not proof of correct
physical orientation. Datasheet/pin-one/polarity review remains explicit evidence.

Run existing native supported electrical/part-rating checks against that exact
closure. Do not infer temperature rise, generic current capacity, unknown ratings,
SI or thermal safety from generic attributes. Applicable but unsupported requirements
need an eligible review method or block; later hazard work stays in #392.

## 6. Required coverage inventory and first-slice disposition

The target-specific implementation must expand these families into the closed leaf
rows below. Passing one check in a family does not pass every leaf. Numeric limits,
units and applicability are attached to each leaf, with referenced source content.

| Required leaf rows | First-slice owner / coverage rule |
| --- | --- |
| Required placements; exact selections/footprints; connected pin-pad contacts; complete physical groups; unintended shorts | Native; missing endpoints and any failure are non-waivable |
| Copper width; each applicable copper-spacing pair; via drill/finished copper diameter/annular dimension; pad PTH annulus | Native where actually supported; distinguish finished diameter from annular-ring width and expose any missing geometry check |
| PTH/NPTH drill ranges and separation; slot width/length/range; outline and mechanical-opening clearances | Native checks plus eligible target-bound review for explicitly unsupported measurements; never treat a skipped limit as pass |
| Board minimum/maximum physical dimensions; layer count; declared thickness; each copper-layer weight; material/finish/process applicability | Native comparisons where modeled, otherwise eligible target-bound review; dimension applicability includes thickness/material; missing required values block; supported process selection checked independently of profile range |
| Emitted copper/drill/layer/outline fidelity and fab-critical export loss | Native IO export evidence; mandatory, non-waivable |
| Mask openings; mask-to-copper clearance; web/sliver limits; declared via covering | Explicit reviewed evidence initially; current mask emission is not DFM validation, and unsupported requested treatment blocks |
| Actual emitted legend height and feature/stroke width; pad/mask-opening clearance; clipping; survival of required pin-one/polarity marks | Explicit reviewed evidence initially, against the native fabrication output rather than SVG appearance alone |
| Exact-part package/land-pattern/pin-map comparison; polarity/assembly orientation | Explicit independent reviewed evidence for assembly; native consistency is not independent part-definition correctness |
| Populated quantities/DNP/selected alternates; CPL identities/side/rotation; supported selected-part rating checks | Native; vendor assembly-process applicability remains explicit reviewed evidence where not modeled |

There is no blanket DRC waiver. Informational/export-neutral cosmetic findings can
remain advisories when the fixed target policy classifies them as such. Loss of an
essential polarity marking cannot be relabelled cosmetic. Manual review of an
unimplemented check is not permission to override a failed mandatory native check.
No structural error, short, required missing connection/part/placement, declared
target-limit violation or fab-critical loss has an exception in this slice.

The [JLCPCB capability source](https://jlcpcb.com/capabilities/pcb-capabilities/),
consulted 2026-10-09, is a process-dependent source to pin during target definition,
not a certification or universal rule set. The Badge's older partial profile must
not be promoted to a complete process policy without the missing process selections
and review rows. This public spec does not copy private Badge source/assets or claim
that the current Badge already passes.

## 7. Bounded manual evidence and trust

Propose a finite `ManufacturingReviewEvidence` record, not a plug-in framework. Each
record names one eligible leaf, exact entity scope/count, reviewed outcome and
method, reviewer, source/observation, rationale, and canonical evidence-file digest.
It binds the CompiledBoard identity, target/policy digest and, for emitted-geometry
checks, native exporter version plus the complete reviewed fabrication-file digest
map. The IO boundary checks supplied file bytes against that record; no path is
canonical and the evaluator does not read arbitrary files.

Evidence must cover every scoped entity demanded by the leaf, not merely a matching
code. Additional same-code entities escape an older review. Foreign board/part
closure/profile, incomplete scope, missing bytes or changed export bytes cannot
satisfy a row. Canonical evidence order and exact counts are reproducible.

`ReviewedPass` means a documented human assertion at a declared trust boundary,
not a cryptographic proof of inspection or an automated manufacturer check. Native
evaluation checks eligibility and binding, not whether a reviewer told the truth.
Failed review blocks; missing review is `NotEvaluated`. No purchases or external
uploads are needed to test this contract. #390 verifies delivery/reopen links;
#247 supplies the actual independent consumer review and prediction plan.

## 8. Export and project integration

Keep core checks in `Volt::PCB`; assemble native export evidence in `Volt::IO` from
`write_pcb_fabrication_files` for the exact same artifact. Proposed IO convenience
entry point exports/evaluates in memory once and returns the fabrication result plus
readiness, avoiding a PCB-to-IO dependency or caller-forged export-loss booleans.

Ordinary diagnostic export remains available for blocked designs. Dedicated
manufacturing delivery requires native `Ready`, the existing successful project
diagnostic/test gate, required review artifacts and zero native fab-critical loss.
Project tests stay orchestration evidence: the native physical result does not
claim to rerun Python product tests or prove electrical function. A passing project
test cannot suppress a native readiness blocker. #390 verifies persisted project
test/evidence identities and source-free delivery; this does not replace its graph.

M1 adds its finite versioned readiness/evidence codec and thin Python parity plus
the native gate call in the existing writer. Do not rewrite publication: retain
#391's ownership markers, schema-v2 output, staging and failure guarantees. Any
package descriptor addition is versioned and tested in coordination with #390;
blocker discovery occurs before accepted output publication. Do not claim complete
package/source-free verification until #390 is delivered.

## 9. Acceptance tests before implementation is complete

- Native positive fixture: complete populated two-layer board, exact part closure,
  accepted target selections, supported Solid-zone contacts and all required rows.
- Remove a required placement (including the last/all placements), selection or pad
  contact: fail even if no ratsnest/unrouted warning remains. Missing required
  logical connection and optional-connected endpoint cases are distinct tests.
- DNP missing placement vs DNP placed copper; multi-pad mapping; single-terminal
  group; bound module continuity; intentional no-connect contradiction.
- Fresh and reopened hierarchical artifacts preserve identical native continuity
  partitions, ratsnest/completeness/short results and provenance. Invalid partitions
  reject structurally; v1 remains usable for ordinary export but cannot pass readiness.
- Remove/shrink a Solid zone, move a contact across its edge, separate layers, break
  a via span, add a foreign-net short and exercise export-grid boundary cases.
  Native connectedness and exported region/contact expectations must agree.
- Connect two logical groups through netless exported zone/pad copper; detect the
  short. Contact an intentional no-connect pad, and prove no logical net mutation.
  Unsupported oval export must block even if a disc approximation appears connected;
  exercise supported lossy geometry against actual emitted shape.
- Violate every native-covered target leaf; omit thickness/weight/profile values;
  violate target board-size limits with thickness/material-specific applicability;
  use unsupported processes. Expected state distinguishes failed/not-evaluated.
- Correct native board with absent mask/legend reviews stays blocked. Add valid
  exact-snapshot evidence to pass; change one entity/part/profile/export file or
  add another same-code entity and prove the old evidence no longer suffices.
- Wrong BOM quantities/DNP/selection and CPL identity/side/offset fail; test both
  fabrication-only and assembly goals, including independent review requirements.
  With the same CompiledBoard, change only a CPL rotation offset and prove the
  target/result identity changes and prior orientation evidence cannot be reused.
- Bare `ProjectResult.ok`, warning-only unrouted, broad expected diagnostics or
  manual evidence cannot waive a required native failure. Project-test failure
  separately prevents delivery even when the physical native result is Ready.
- Direct C++/Python parity for statuses, messages, provenance and ordered payloads;
  deterministic codec round-trip, typed malformed/unsupported-version rejection;
  native export-loss and writer refusal tests preserve previous owned outputs.
- Source-free reopened artifact evaluation equals fresh evaluation without any
  authoring callback or ambient library. Equivalent inputs in different checkout
  roots give identical readiness/evidence bytes. #390 tests the full package graph.

## 10. Delivery sequence and approval

1. Accept the proposed finite target, evidence trust, DNP/completeness and no-waiver
   decisions and the focused logical-continuity ADR/schema contract. Pin the first
   process policy and all leaf limits/applicability with
   reviewed sources; map existing check coverage without claiming skipped checks.
2. Implement versioned logical-continuity retention and full exact-part reuse, then
   native inventory/connectedness, coverage result
   and deterministic codec; prove the native positive/negative fixtures.
3. Implement native IO export composition, eligible review binding and thin Python
   parity; add the gate to the existing publication path and run platform CI.
4. Independently review the implementation and source references, then deliver a
   tested non-draft implementation PR. Keep #389 open until its actual acceptance.

This proposal may be split into bounded implementation PRs after approval, but no
intermediate partial-check result may be called manufacturing-ready. #382's route
contract can proceed independently; #390 depends on the accepted readiness shape,
and #247/#248 remain the consumer and physical-proof gates respectively.
