# Native manufacturing targets V1 and coverage V2

`Volt::PCB` owns `ManufacturingTarget`, `ManufacturingProfile`, the finite typed
process selections and `evaluate_manufacturing_coverage`. `Volt::IO` owns strict
standalone target JSON and complete deterministic report JSON. The Python exports
are direct native bindings. Evaluation reads only immutable `CompiledBoard` facts;
it performs no filesystem, library, website or manufacturer lookup.

Coverage evaluator V2 always reports **Blocked** or **UnsupportedTarget**. There is no Ready value.
Passing physical completion or an implemented limit does not satisfy the complete
manufacturing inventory. Synthetic profiles demonstrate evaluator behavior and do
not establish manufacturer acceptance. Exact native assembly consistency and four supported rating families are checked below.
Export/review binding and the delivery gate remain separate work under #389.

## Target identity and process

Include `volt/pcb/compiled/manufacturing_target.hpp` and
`volt/io/pcb/manufacturing_target.hpp`. A target binds the exact board name,
compilation provenance digest, artifact byte digest, goal, complete capability
snapshot, source-content digest, exact process applicability and explicit choices.
A target profile must equal every capability field frozen into the artifact.
Compile separate matching artifacts to compare profiles; substituting a foreign
profile makes `ArtifactIdentity` fail. Additional process limits and captured-source
identity participate in the profile digest even though the artifact codec is unchanged.

Every process field is required for a supported selection. Optional typed fields
represent **unknown**, without manufacturing defaults. V1 supports the following
finite synthetic process family:

| Field | Supported selection / unit |
| --- | --- |
| family / material | RigidFr4 / Fr4Tg135 |
| copper_layers | 2, enabled top then bottom in the stack; no extra copper layers |
| top_copper_weight_oz / bottom_copper_weight_oz | Explicit finished weight, oz/ft² |
| thickness_mm | Explicit finished board thickness, mm |
| finish | LeadFreeHasl |
| mask_sides / mask_colour | Both / Green |
| via_treatment / drilling / holes / slots | Exposed / MechanicalThrough / RoundOnly / NoSlots |
| assembly | NoAssembly for Fabrication; SmtAndThroughHole for FabricationAndAssembly |
| dimension_x_mm / dimension_y_mm | Explicit axis-aligned outline bounding-box extents, mm |

Other finite enum choices report unsupported. Missing fields, limits or implementations
report NotEvaluated. Board dimensions, thickness, layers and weights must match the
explicit selections independently of satisfying profile ranges or availability.
Numeric limits are inclusive with the existing native `1e-9` DRC tolerance. The
via outer copper diameter differs from radial annulus `(outer - drill) / 2`.
Track width uses the same highest-applicable copper-weight refinement as native
capability validation, bounded below by the profile base width.

Material/finish selections describe requested applicability; their realization is
still a required unevaluated leaf. Mask/legend/required pin-one and polarity checks
also remain explicit, even when output files could be generated. Rating coverage remains required under both goals and includes explicit per-subject gaps. Fabrication-only keeps assembly-only leaves
visible as policy-owned NotApplicable; it does not erase them. `SlotTreatment` is
NotApplicable under either goal when `NoSlots` is selected and no native slots exist.

Assembly targets retain native `CplProjectionOptions` as target data. Missing options
block; explicitly empty offsets are distinct from missing. Corrections sort by
footprint library/name and use native finite degree values. Duplicate keys reject,
including identical repeats. Native CPL evaluation uses these exact retained options.

## Standalone target codec

`write_manufacturing_target` emits format `volt.manufacturing-target`,
`schema_version: 1`, `policy_version: 1`, and exactly these payload fields:

- `source`: `board`, `provenance_digest`, `content_digest`.
- `goal`: the native goal enum name.
- `process`: all fields above; an unknown field value is JSON null.
- `profile`: the existing standalone `capability` document, `source_content`,
  `applicability` process, optional `dimension_x_mm` and `dimension_y_mm` inclusive
  ranges (`minimum_mm`, `maximum_mm`), and optional
  `minimum_via_radial_annulus_mm`. Missing limits are JSON null.
- `cpl_options`: null or an object containing `rotation_offsets`, whose entries
  are `library`, `name`, `rotation_deg`.

`read_manufacturing_target` rejects duplicate/unknown/missing keys, malformed shapes,
enums, hashes, booleans used as numbers, overflowing integers, invalid ranges and
non-finite/nonpositive process values. It throws `ManufacturingTargetReadError` with
code `Malformed` or `UnsupportedVersion`; embedded capability versions participate.
Native typed constructors reject invalid typed values through kernel errors.
There is no report decoder and no waiver, evidence, N/A or required-row input.

Target/profile identity uses the precise domain-separated token encoding in the
[reviewed contract](design/native-manufacturing-target-contract.md), independent
of JSON key order, whitespace, locale and checkout paths. Golden digests pin that
encoding. Reports are recomputed after reopening artifact and target bytes.

## Closed result

`ManufacturingCoverage.coverage()` returns all 65 leaves in the reviewed fixed order.
Rows cannot be removed or changed through the result. Each row records its key,
NativePass/Failed/NotEvaluated/NotApplicable state, owner, applicability, explanation,
remediation, sorted unique reporting EntityRefs and ordered measurements with typed
units and optional inclusive limits/selected values. One known failure takes
precedence over a missing measurement in that row.

`write_manufacturing_coverage` includes the exact target, target/profile digests,
full source/compilation provenance, all rows/counts, and complete retained native
completion diagnostics including original messages, entities, measurements, overlays
and rule provenance. The nested `completion` is the existing PCB physical report;
its ExportFidelity stays NotEvaluated here. Existing IO fabrication-completion
remains available independently. The report embeds `volt.board-completion/1`;
the writer checks that version and its required source/provenance fields before
copying them to the report envelope. Completion diagnostics stay at `completion.diagnostics`; assembly diagnostics stay
in the native CPL evidence, and rating diagnostics are at `ratings.diagnostics`.
The single top-level `profile_digest` applies to every coverage row.

Coverage blocker codes are row classifications qualified by the closed check key:

| Code | Meaning |
| --- | --- |
| MANUFACTURING_MISSING_INPUT | Required selection, limit or native prerequisite is unknown |
| MANUFACTURING_NOT_EVALUATED | Checker or required exact evidence is not delivered |
| MANUFACTURING_CONSTRAINT_FAILED | A mandatory native constraint fails |
| MANUFACTURING_UNSUPPORTED_SELECTION | Requested or actual process is outside V1 |
| MANUFACTURING_IDENTITY_MISMATCH | Exact artifact or immutable capability snapshot differs |
| none | Checked pass or policy-owned non-applicability |

These codes do not replace or filter the retained native diagnostic payload.
`observation_count` counts measurement records; `entity_count` counts distinct row
EntityRefs. A track-width observation has one measurement and the tuple
`(board-track, board-layer)`, so its entity count is two. Via measurements have one
via ref; board dimension/count/thickness have the board ref; copper weights have the
actual side layer ref. Deferred assembly/rating scope includes every logical component,
including DNP. No external reporting reference becomes a mutation/traversal handle.

## Native assembly and selected ratings

Coverage reports explicitly use `schema_version: 2`, `evaluator:
volt.manufacturing-coverage/2`, and row rules under that evaluator. Target JSON,
profile/target semantic digests and `policy_version: 1` remain unchanged. Existing
V1 target bytes reopen and are evaluated by V2; historical V1 coverage reports are
not evaluator inputs or silently upgraded. No CompiledBoard archive change occurs.

For assembly, the evaluator inventories every logical occurrence before projecting
native BOM/CPL. Missing explicit fitted/DNP intent fails PopulatedQuantities;
missing selection or required placement cannot disappear through empty projections.
It checks grouped fitted quantities/references, zero DNP purchase quantities, full
exact selection/override/approved-alternate metadata, and one populated CPL row per
required occurrence with exact part, resolved footprint, position, side and normalized
rotation plus the pinned correction. A missing exact resolved selected footprint
fails the CPL rotation prerequisite; matching its unresolved zero fallback never
produces passing rotation observations. Known side coverage remains independent.
DNP rows are omitted. `assembly.bom` and
`assembly.cpl` embed the existing native projection documents. Numeric rotation
consistency never proves correct package orientation or independent footprint review.
An approved alternate MPN never becomes the selected exact Part implicitly.

`ratings` owns one outcome per occurrence gap or canonical non-characteristic
semantic-key group, including all original group records/evidence, complete exact
selection, native rule, ordered comparisons and sorted component/pin/net scope.
Group `records` are an array of original owner-validated records with `pin_count` and
`record_count`; they are not a standalone record-set document. Conditions may reference
other groups in the same exact selected Part. Counts derive from those records. Four finite unconditional, known, conflict-free
families reuse the existing ERC owner:

- Voltage AbsoluteLimit against authored oriented potentials or compatible provided ranges.
- Voltage AcceptedRange against other populated Voltage ProvidedRange claims.
- Current AbsoluteLimit against other populated Current ProvidedRange claims.
- Continuous Current requirements, max per occurrence/domain then summed, against
  exactly one populated compatible capability (min per occurrence/domain).

Each constrained range side needs a known provider side. Unknown/conflicting/conditional
claims, unpaired providers/capabilities, unsupported Current AcceptedRange, unresolved
endpoints, missing selected parts/ratings and non-finite arithmetic remain NotEvaluated.
Live ERC keeps its strict full identity checks and diagnostics. The compiled bridge
verifies retained full original definitions and maps original ordered PinKeys to
artifact PinIds; it consumes preserved V2 continuity without restoring hierarchy.
Authored-voltage comparisons retain the canonical minimum-ID representative limitation:
only noncanonical or contradictory group-member voltage assertions produce a gap.
Compiled V1 has no authoritative continuity and cannot establish rating coverage.

Explicit DNP occurrences stay in the audit as NotApplicable subjects, supply no
installed voltage/current guarantees, and never hide emitted copper or another
populated endpoint. The rating leaf itself remains required under both goals. It
fails on a proven supported violation, stays NotEvaluated on any uncovered populated
subject, and passes only when at least one supported obligation was actually checked
and every applicable subject is covered. Empty, all-DNP and characteristic-only
inventories cannot acquire a ratings pass from an empty diagnostic list.

C++ exposes read-only optional `bom()`, `cpl()` and `ratings()` evidence on the owned
result. These are absent when exact supported target preconditions prevent evaluation;
BOM/CPL are also absent for fabrication-only. No caller-supplied projections, overrides,
manual evidence or report decoder exist. The 65 leaves remain: package/land-pattern/
pin-map/orientation reviews, assembly-process acceptance, deferred DFM, export fidelity
and delivery still block. See the [reviewed contract](design/native-manufacturing-assembly-ratings-contract.md).

## Source-free Python

```python
import json
from pathlib import Path
import volt

artifact = Path("board.compiled").read_bytes()
target = volt.read_manufacturing_target(Path("target.json").read_text())
result = volt.evaluate_manufacturing_coverage(artifact, target)
assert not result.ready  # Required unevaluated leaves still block.
report = json.loads(result.to_json())
assert report["target_digest"] == target.digest
```

Python exposes result status, readiness and digests as read-only typed properties.
Rows, observations and retained diagnostics are available through `to_json()`;
Python does not expose typed row or EntityRef objects. C++ retains typed row access.

To author inputs, populate native `ManufacturingProcess` fields with the exported
`Manufacturing*` enums. `ManufacturingProfile(capability_json, source_content,
applicability, dimension_x_mm, dimension_y_mm, minimum_via_radial_annulus_mm)` takes
the existing capability document plus native `ManufacturingRange` values.
`make_manufacturing_target(artifact, goal, process, profile, cpl_options=None)` binds
the verified artifact identity natively. Typed inputs are copied into immutable
profile/target owners. `ManufacturingCplOptions.rotation_offsets` contains native
`ManufacturingCplOffset(library, name, rotation_deg)` values.

This API does not change ProjectResult.ok, manufacturing-package publication or
ordinary diagnostic/export availability. No order or upload is performed.
