# Exact native manufacturing export and evidence V1

This is stage 4 of M1 [#410](https://github.com/RyanWillie/Volt/issues/410).
It binds finite human assertions to actual native export bytes and exact immutable
inputs. Every result remains Blocked or UnsupportedTarget. Uncovered native DFM,
material/finish/assembly-process realization and the later delivery gate remain
required work. A valid assertion is not proof a person inspected a board, that its
criterion correctly transcribes a source, or that a manufacturer accepts it.

## Native ownership and APIs

Include `volt/io/pcb/manufacturing_evidence.hpp`. PCB owns the closed 65 leaves,
review eligibility, criterion shape, exact native scope and non-waiver policy.
IO `evaluate_manufacturing_export(compiled, target, input)` exports once through
`write_pcb_fabrication_files`, using `evaluate_pcb_fabrication` to retain the actual
output and existing loss/format/quantization completion checks. Public mutable
export outputs and caller booleans are never accepted as proof.

`ManufacturingEvaluation` owns read-only output, binding, coverage and review
receipts. `ManufacturingEvidenceInput` owns optional requested basename, optional
supplied reviewed files, typed assertions and exact in-memory attachments. It reads
no paths, executes no source, and mutates no artifact. The normal fabrication writer,
diagnostic exports, ProjectResult publication and #391 safeguards keep their behavior.

The existing `evaluate_manufacturing_coverage` still emits V2 without export/review
composition. Composed coverage explicitly uses schema/evaluator V3 and policy V1;
target/profile JSON and semantic identity stay V1. No archive schema changes,
restoration API, historical report reinterpretation or report decoder exists.

## Binding

The V1 binding records exact board/provenance, artifact bytes, delivered selected
closure provenance, profile/target semantic digests, policy/evaluator/evidence and
exporter semantic versions, complete exporter metadata, requested optional basename
and every actual emitted file. File records contain filename, stable function and
SHA256 of exact bytes; canonical order is unique function order. All actual conditional
files are included. Required two-layer copper, mask, top legend/paste, profile and both
drill functions must exist uniquely and have nonempty bytes before fidelity passes.
Loss/rounding coverage is independently mandatory; a matching file digest alone
cannot pass ExportFidelity. Unsupported range/empty output remains a blocker.

The [reviewed contract](design/native-manufacturing-evidence-contract.md) pins exact
domain-separated length-token encoding and field order. Paths/checkout roots are
excluded. Explicit basename differs from null, even if sanitized filenames match.
Changing a target CPL correction alone invalidates every prior assertion. SHA256
proves correspondence, not authenticity of an original author or a human review.

## Finite assertion document

`write_manufacturing_evidence(assertions)` writes `volt.manufacturing-evidence`,
`schema_version: 1`, and one `assertions` array in native leaf order. Every assertion
has exactly these fields:

- `binding`: complete binding above, with its recomputed semantic `digest`.
- `check`, `method`, `outcome`: closed leaf key, `emitted-fabrication-inspection` or
  `independent-assembly-inspection`, and `pass` or `fail`.
- `scope`: ordered arrays of reporting EntityRefs (`kind`, `index`), with derived
  `scope_count` and distinct flattened `entity_count`. Placement/pad tuples retain
  occurrence association. References are compared, never used to traverse/mutate.
- `reviewer`, `source`, `source_key`, `criterion`, `minimum_mm`, `observation`, `rationale`,
  `evidence_key`, `evidence_digest`: complete declared human provenance and criterion.

The exact source attachment must hash to `target.profile().source_content()`.
Mask copper clearance/web/sliver and legend height/stroke/pad/mask clearance require
finite nonnegative `minimum_mm`; other eligible leaves prohibit a numeric minimum.
The human declares the complete applicable criterion and accurately transcribes its
source at the explicit trust boundary. Native checks shape, binding and source bytes;
it does not parse arbitrary source documents as manufacturer policy. Required strings
cannot be empty/whitespace. Evidence bytes are separate from their assertion document,
avoiding a self-referential digest. Keys are opaque labels, never filesystem reads.

`read_manufacturing_evidence` strictly rejects unknown/missing/duplicate JSON keys,
wrong types, unknown enums/leaves, malformed hashes/counts and duplicate/conflicting
leaf assertions. `ManufacturingEvidenceReadError.code()` distinguishes Malformed
from UnsupportedVersion. Typed malformed values reject with InvalidArgument.
Bindings are recomputed at evaluation, never trusted because a document decoded.

## Eligibility and receipts

Only 16 explicitly eligible leaves consume assertions: mask openings/clearance/web/
sliver/via covering; legend height/stroke/pad clearance/mask clearance/clipping;
required pin-one/polarity; assembly package/land-pattern/pin-map/orientation reviews.
The contract lists each obligation/method and native complete scope. A board-wide
obligation covers absent essential marks, plus all relevant native entities and exact
part closure. Zero entities never silently remove a required review. Assembly-only
rows remain native NotApplicable for fabrication goals.

Any mandatory native failure, missing authoritative physical truth, unsupported
process, target identity/limit failure or native loss/fidelity gap prevents
ReviewedPass. Other required uncovered leaves remain blockers. No expected-diagnostic,
DNP, result.ok or arbitrary NotEvaluated-to-review shortcut exists. Every supplied
assertion gets an ordered receipt:

| Disposition | Meaning |
| --- | --- |
| accepted | Eligible exact assertion/source/review bytes correspond; declared Pass or Fail applies |
| binding-mismatch | Artifact/closure/profile/target/export identities differ |
| scope-mismatch | Exact complete ordered native tuples differ |
| ineligible | Leaf, method or goal cannot consume this assertion |
| missing-bytes | Required nonempty source or review bytes absent |
| changed-bytes | Source or review byte digest differs |
| file-inventory-mismatch | Supplied reviewed files differ from complete actual output |
| native-prerequisite | Mandatory native prerequisites cannot be waived |

Invalid/missing evidence leaves the eligible row NotEvaluated. Accepted Fail makes
that row Failed. Duplicate assertion leaves or attachment keys, invalid criterion
shape, and unused attachments reject structurally rather than choosing an insertion
order. Reused attachments across leaves are allowed when explicitly referenced.

`write_manufacturing_evaluation` emits the complete V3 coverage/native diagnostics,
BOM/CPL/ratings, actual file texts/losses, binding and all receipt/assertion provenance.
Only intended composed leaves/scopes change; remaining rows retain their payloads.

## Thin Python

Python exports native enum/value/input/result classes and the same codecs. Use
`evaluate_manufacturing_export(artifact_bytes, target, evidence_input)` and
`result.to_json()` for the entire native payload. `result.files` returns copies of
actual `ManufacturingFabricationFile` values with exact `.bytes` for optional
reviewed-file verification. Altering these copies cannot alter native output.

`make_manufacturing_review(artifact, evaluation, check, outcome, reviewer, source,
source_key, criterion, minimum_mm, observation, rationale, evidence_key,
evidence_digest)` derives the exact eligible method/scope natively and binds the exact
result. It does not declare inspection happened. Supply exact byte attachments with
`ManufacturingEvidenceAttachment(key, bytes)`; reading/saving them remains caller
orchestration. Native code performs all binding/policy/byte verification.

Regression fixtures are explicitly synthetic and prove binding/policy only. Full
native/Python and installed-wheel tests compare complete deterministic reports and
file bytes after original source owners are destroyed and across working directories.
