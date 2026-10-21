# Exact native export and review evidence (#410)

Status: independent GPT-6.1 Sol contract approval at `bebb9143`, 2026-10-21;
[decision and resolved findings](https://github.com/RyanWillie/Volt/issues/410#issuecomment-6311535120). Baseline main
`1069b4706fc8e6c7c2fa926d32072b26791c064a`. Stage 4 only of M1 #389;
#403/#406/#408 are closed. No aggregate Ready, delivery gate, persisted package
graph, vendor acceptance, actual consumer review or fabrication claim.

## Ownership and finite native seam

PCB owns the unchanged 65-leaf inventory, applicability, native checks, finite
review method eligibility, exact scope derivation and non-waiver transition.
IO owns actual native export composition, byte verification, canonical binding
and review documents. Python binds native values and native calls only.

Add an in-memory IO composition returning actual fabrication output plus immutable
BoardCompletion from one invocation of write_pcb_fabrication_files and the existing
completion_export_grid/loss logic. The ordinary evaluate_fabrication_completion
wrapper delegates to this seam. No public consumer accepts a mutable export result
as proof. Preserve all loss/rounding checks (including informational LossyGeometry),
physical diagnostics and source-free V1 export behavior. A narrow private native
composition function may assemble immutable results; no public restoration or
mutable coverage API. PCB has no IO link dependency or filesystem reads.

IO evaluate_manufacturing_export(compiled, target, input) returns an immutable
ManufacturingEvaluation: actual output, exact binding, immutable native coverage,
checked review receipts and every row. It computes existing native coverage first,
then maps actual IO completion ExportFidelity into that row. PCB applies the closed
review transition only after IO verifies bytes. Output objects are never evaluator
inputs. No caller loss-free flag, exporter callback, ambient resolver or alternate
geometry/export engine. Native physical/rating/BOM/CPL checks retain their payloads.

## Identity and canonical encoding

A binding records board name and provenance digest, exact artifact-byte digest,
the delivered compiled.provenance().selected_closure_digest() (not a new closure
hash; exact retained definition bytes are also pinned by artifact digest), profile and target semantic digests, target policy
V1, evaluator volt.manufacturing-coverage/3, evidence schema V1 and native exporter
semantic revision volt.native_fabrication/1. The existing exporter metadata schema
is separately retained in full (name, schema, Gerber and drill metadata). Future
algorithm/format changes affecting review meaning require a new semantic revision.
No CompiledBoard or target/profile schema change. Target already binds complete CPL
options; changing only a correction invalidates every previous review.

Canonical inventory is sorted by unique stable native file function; records carry
function, actual emitted filename and SHA256 of exact bytes. Filenames are native
basename-derived tokens, never checkout paths. Bind requested basename (null differs
from explicit input), the sanitized actual filenames and all metadata. Equivalent
absolute checkout roots have identical identities; changing basename invalidates
binding even if geometry bytes happen to agree. No filesystem lookup is performed.
The complete emitted set includes both copper files, both masks, top legend/paste,
profile and both drill files for supported nonempty process coverage, plus every
conditional native bottom legend/paste file. Actual writer is the inventory owner;
check nonempty output, required function presence and duplicate functions/filenames
independently before passing ExportFidelity. Missing outline/range output cannot
pass. Actual fidelity additionally requires existing export-grid coverage and no
fab-critical or LossyGeometry entry; matching digests alone never passes it.

Semantic binding digest uses domain volt.manufacturing-export-binding/1 and the
existing length-prefixed UTF-8 token convention (decimal UTF-8 byte length, colon,
bytes); fields follow the preceding identity paragraph, then metadata declaration
order, optional requested basename marker/value, inventory count and sorted records
(function, filename, digest). Integers use unsigned base-10; hashes use canonical
sha256:<64 lowercase hex>. Strings contain no hidden normalization. No JSON or
filesystem ordering enters this digest. Golden cross-compiler tests pin it.

## Review records, byte correspondence and errors

A typed assertion owns one ManufacturingCheck, one closed method, exact ordered
native scope tuples and derived counts, Pass/Fail, reviewer, observation/source and
rationale (nonempty meaningful strings), a finite criterion below, exact binding and evidence attachment key
plus SHA256. Evidence attachments are supplied in memory as key/exact bytes. The
assertion is a separate document, so its attachment digest is not self-referential.
Each assertion also requires a criterion description, exact source citation and
source attachment key. IO verifies that attachment against
`target.profile().source_content()`; the existing target already pins those captured
source bytes and exact process applicability. MaskCopperClearance, MaskWeb, MaskSliver,
LegendHeight, LegendStroke, LegendPadClearance and LegendMaskClearance additionally
require an explicit finite nonnegative minimum in millimetres. Other eligible leaves
require the qualitative obligation in the eligibility table and prohibit a numeric
minimum. Missing/empty criteria, absent required minima or inappropriate minima
reject structurally. The assertion records the human-declared complete applicable
threshold/criterion and its source; native code does not parse source documents or
certify that a declared threshold faithfully transcribes them. That transcription
and completeness are part of the documented human trust boundary, not new native
profile limits or an automatic manufacturer policy. Both the source attachment and
review attachment are required exact in-memory bytes; a single attachment can serve
both when explicitly referenced. Synthetic source bytes/criteria say synthetic.
Keys are opaque nonempty labels, never paths or read instructions. Every supplied
attachment must be referenced, every reference must have exactly one byte payload;
empty attachment bytes are missing evidence. Multiple leaves may share one attachment.
Assertions are sorted by leaf; duplicate leaf assertions (identical or conflicting)
and duplicate attachment keys reject structurally, never last-writer-wins.

Optional supplied reviewed fabrication files must equal the complete actual inventory
by function, filename and exact bytes (order independent). If omitted, the returned
actual bytes are the reviewed inventory candidate; assertions still bind its complete
digest. If supplied empty/missing/extra/foreign/changed/duplicate, no review pass is
possible. No record or attachment silently disappears. Binding mismatch, reduced or
reordered/duplicate entity scope, changed attachment bytes, missing attachment, or
ineligible method/leaf produces an explicit nonpassing receipt and leaves that row
NotEvaluated (native Failed/NativePass/N/A never overwritten). A valid Fail assertion
makes its eligible row Failed. Missing assertion remains NotEvaluated. Duplicate
assertions/keys and invalid enums/empty required strings are typed InvalidArgument;
strict evidence JSON rejects malformed/unknown/missing/duplicate keys and unsupported
schema/policy/evaluator/exporter versions with a domain-specific Malformed or
UnsupportedVersion error. No partial result is published on structural errors.
Representable stale/foreign/incomplete evidence yields typed nonpassing receipts.

The deterministic standalone evidence V1 codec owns assertion/binding metadata;
attachment and fabrication byte inputs are separate. Reader never trusts documents
as evaluated reports; every evaluation recomputes actual export/binding/scopes.
Report schema/evaluator V3 explicitly extends V2 with ReviewedPass, NativeIO and
HumanReview owners, binding and ordered receipt provenance. Standalone target V1,
policy V1, old V1/V2 report writers/meaning are not silently reinterpreted: native-only
evaluate_manufacturing_coverage retains V2 bytes; composed reports explicitly use V3.
No report decoder or archive migration is introduced.

## Closed eligibility, applicability and scopes

Only these 16 leaves accept evidence. Every eligible scope contains a board-wide
obligation plus complete native relevant inventories, so missing marks are reviewed
as an absence rather than excluded. Tuple order is category below, then native ID;
entities within tuples have explicit fixed semantic order. Counts include tuple
count and sorted unique flattened entity count; neither count is caller-selected.

| Leaf(s) | Only method | Native scope and obligation |
| --- | --- | --- |
| MaskOpenings, MaskCopperClearance, MaskWeb, MaskSliver | EmittedFabricationInspection | board; every layer; every placement/component tuple; every placement/pad tuple; every via. Audit actual mask opening/clearance/bridge/sliver output against pinned target, including absent geometry. |
| ViaCovering | EmittedFabricationInspection | same complete mask scope; explicitly verify selected Exposed treatment in actual output, including zero-via board-wide review. Unsupported covering never becomes eligible through evidence. |
| LegendHeight, LegendStroke, LegendPadClearance, LegendMaskClearance, LegendClipping | EmittedFabricationInspection | board; every layer; every board text; every placement/component; every placement/pad; every via. Review actual 5x7 cell text and 0.12mm footprint-body strokes, not SVG appearance. Each leaf independent. |
| RequiredPinOne, RequiredPolarity | EmittedFabricationInspection | complete legend scope plus every logical component and pin, including unplaced/no-mark occurrences. Board-wide audit determines required marking presence/survival from exact selected definitions/source evidence; native model does not claim a typed essential-mark inventory. Missing essential marks require Fail; no automatic N/A/cosmetic shortcut. |
| PartPackageReview, LandPatternReview, PinMapReview, AssemblyOrientation | IndependentAssemblyInspection | board; every logical component and pin; every placement/component; every placement/pad; every layer. Full selected-part closure and complete native CPL/options are bound. Native consistency is not independent correctness. |

Assembly-only rows retain native NotApplicable for Fabrication. All eligible applicable
rows stay required even for empty inventories. Native derives exact tuples from immutable
artifact state (no EntityRef traversal); a same-code extra component/text/placement/pad
changes scope and binding. Full artifact and closure identities additionally cover all
geometry and definition content, even without individual primitive IDs.

MaterialRealization, FinishRealization, AssemblyProcess, every deferred copper/drill/
mechanical/slot measurement and SelectedPartRatings gaps remain ineligible. Future
eligibility needs an explicitly versioned reviewed change, not automatic NotEvaluated
conversion. No human assertion can override a native required failure, missing required
selection/placement/terminal, unresolved authoritative continuity, foreign-net short,
unsupported process, target-limit violation or export fidelity gap/loss. If any native
required row is Failed, required physical row lacks Pass, or identity/process/profile/
actual fidelity prerequisites lack Pass, no assertion can become ReviewedPass. A required
native coverage gap elsewhere remains visible and blocks aggregate readiness, but does
not prevent documenting a separately eligible leaf. No DNP/expected diagnostic/result.ok
shortcuts. N/A is native policy only. Original source and row observations remain intact.

ReviewedPass proves eligible exact correspondence of a declared human assertion and
supplied bytes. It does not prove honesty, actual inspection, electrical function,
manufacturer acceptance or independence of the person named. Synthetic tests label
assertions synthetic and demonstrate binding/policy only. #247/#248 are later gates.

## Exact acceptance matrix

| Area | Positive / negative / compatibility cases |
| --- | --- |
| Actual output | Supported synthetic fabrication/assembly, inspect complete returned bytes/losses/completion, every mandatory/conditional function; empty/range/missing outline output and lossy oval/unsupported layer; solid-zone/plated-contact regressions. |
| File bytes | One byte changed, missing/extra/duplicate function or filename, foreign same-format export, explicit empty inventory; arbitrary order canonicalizes; basename/options change invalidates old binding; path independence. |
| Identity | Artifact/board/part/closure/profile/target/source-content/policy/evaluator/exporter/options changes, CPL correction only, immutable input bytes; V1 authoritative-continuity gaps remain nonpassing. |
| Reviews | Each eligible leaf/method exact Pass and Fail; missing review/attachment/source, one evidence/source byte changed, absent/malformed required criteria or numeric minima, empty bytes, reduced/reordered/duplicate scope, added same-code entity; unknown/ineligible leaf, wrong method, duplicate/conflicting assertions and attachments, unused attachments, malformed/unsupported documents. No broad family passes. |
| Non-waiver | Apparently valid reviews beside missing selection/placement/physical terminal, foreign-net short, target-limit failure, unsupported process/geometry/layer, fab-critical/lossy/quantization output and V1 gaps; native rows and diagnostics unchanged. |
| Closed payload | All 65 ordered states/reasons/entities/counts/observations/provenance, BOM/CPL/rating parity; explicit fabrication vs assembly N/A; remaining deferred rows block and ready remains false. |
| Lifetime/codec | Strict deterministic write/read/write, evidence order canonicalization, direct native/thin Python complete report+actual byte equality; fresh/source-free reopen after owners destroyed; checkout independence/no mutation; target V1 and native V2 report golden compatibility. |
| Delivery | Full relevant C++/Python/architecture/docs/routing/completion/target/assembly/rating regressions, installed SDK/downstream consumer and installed-wheel source-free parity; GCC/Clang/MSVC digest golden; independent final implementation review; actual comments dispositioned; non-draft PR closes only #410; all six exact-final-head CI jobs terminal SUCCESS. No merge/auto-merge. |
