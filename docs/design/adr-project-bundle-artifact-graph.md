# ADR: ProjectBundle v2 Artifact Graph and Dependency Lock

Status: accepted

Issue: [#293](https://github.com/RyanWillie/Volt/issues/293)

## Decision

`ProjectBundle` v2 is Volt's immutable, native, typed artifact graph for one completed
project build. Python continues to execute authoring source and orchestrate `Project`.
C++ owns the v2 manifest contract, semantic artifact construction, owner-local codecs,
validation, and verified reopening.

The manifest family remains `volt.project_result` and advances from `schema_version: 1`
to `schema_version: 2`. A v2 bundle is not a directory of paths with optional metadata:
every declared payload has a typed role and kind, path-independent identity, exact schema
and producer versions, a content digest, and complete dependency edges.

Opening a bundle is read-only and failure-atomic. `--bundle` consumers operate only on
the bundle and must never import or execute project Python, library authoring source, CAD
generators, or another source entrypoint.

## Ownership Boundary

`ProjectBundle` owns packaging, integrity metadata, and the lifetime of reopened objects.
It does not become an EDA owner.

- `Circuit` remains the sole owner of logical connectivity and selected-part intent.
- `Schematic` remains the owner of presentation over one logical design.
- Each named authoring `Board` remains one complete alternative physical implementation.
- `CompiledBoard` is the immutable native handoff for exactly one named `Board` and its
  compile inputs. Its internal schema and minimum logical snapshot belong to the focused
  projection/compile contract; this ADR only freezes its bundle role and edges.
- Component, part, symbol, and footprint payloads retain their owner-local native codecs.
- `BoardScene` is a derived view of one `CompiledBoard`, not another Board, netlist, part
  model, or editable scene database.
- `PartLibraryBundle` is the immutable whole published catalogue. A `ProjectBundle` does
  not embed or rename that owner. It vendors only the exact reachable closure required by
  logical external-component references and selected parts.

Artifact IDs and dependency edges are reporting and loading handles. They must not become
normal traversal or mutation handles for kernel entities.

## Typed Manifest Contract

The accepted native vocabulary is equivalent to the following shape. Exact container
types may follow local C++ conventions, but the fields and closed vocabularies are part of
schema v2:

```cpp
enum class ArtifactRole {
    Model,
    View,
    Render,
    Report,
    Adapter,
    Asset,
    Delivery,
};

enum class ArtifactKind {
    LogicalModel,
    SchematicModel,
    BoardModel,
    CompiledBoard,
    ComponentDefinition,
    PartDefinition,
    SymbolDefinition,
    FootprintDefinition,
    BoardScene,
    GlbAsset,
    Diagnostics,
    ProjectTests,
    SchematicSvg,
    BoardSvg,
    BoardLayerImage,
    KicadPcb,
    Bom,
    Cpl,
    FabricationPackage,
    StepAsset,
    WholeBoardGlb,
};

struct ArtifactSchema {
    FormatName format;
    SchemaVersion version;
};

struct ProducerInfo {
    ProducerName name;
    ProducerVersion version;
    ContentHash build;
};

struct LogicalArtifactIdentity {
    DesignKey design;
};

struct SchematicArtifactIdentity {
    DesignKey design;
    SchematicKey schematic;
};

struct BoardArtifactIdentity {
    DesignKey design;
    BoardName board;
};

struct BoardSceneArtifactIdentity {
    CompiledBoardIdentity compiled_board;
};

enum class ProjectSingletonKey {
    Primary,
};

struct ProjectSingletonIdentity {
    ProjectSingletonKey key;
};

struct ExportArtifactIdentity {
    ContentHash request_digest;
    ExportOutputKey output_key;
};

using ArtifactOwnerIdentity = std::variant<
    LogicalArtifactIdentity,
    SchematicArtifactIdentity,
    BoardArtifactIdentity,
    CompiledBoardIdentity,
    BoardSceneArtifactIdentity,
    ProjectSingletonIdentity,
    LibraryComponentRef,
    LibraryPartRef,
    LibraryAttachmentRef,
    LibraryAssetRef,
    ExportArtifactIdentity>;

struct ArtifactId {
    ArtifactKind kind;
    ArtifactOwnerIdentity owner;
};

struct ArtifactRef {
    ArtifactId artifact;
    ContentHash content_digest;
};

using ArtifactDependency = ArtifactRef;

enum class AuthoringInputKind {
    ProjectSource,
    DeclaredInput,
};

struct AuthoringInputRecord {
    AuthoringInputKind kind;
    LogicalInputName name;
    ContentHash content_digest;
};

struct AuthoringInputs {
    LogicalInputName entrypoint;
    std::vector<AuthoringInputRecord> records;
    ContentHash digest;
};

struct ArtifactDescriptor {
    ArtifactId id;
    ArtifactRole role;
    ArtifactKind kind;
    ArtifactSchema schema;
    MediaType media_type;
    RelativeBundlePath path;
    ContentHash content_digest;
    std::vector<ArtifactDependency> depends_on;
    ProducerInfo producer;
};

struct ProjectIdentity {
    ProjectName name;
    std::optional<ProjectVersion> version;
    std::optional<ProjectDescription> description;
};

enum class ProjectStatus {
    Clean,
    ExpectedDiagnostics,
    Failed,
};

struct ProjectRunSummary {
    bool ok;
    ProjectStatus status;
    ProfileName profile;
    std::vector<ProjectStageKey> stages;
};

struct ProjectBundleManifestV2 {
    ProjectIdentity project;              // name, version, description
    ProjectRunSummary run;                // ok, status, profile, executed stage keys
    AuthoringInputs authoring_inputs;
    BuildId build_id;
    ContentHash bundle_digest;
    DependencyLock dependency_lock;
    ExportSelection export_selection;
    std::vector<ArtifactDescriptor> artifacts;
};

struct BuildArtifactIdentityV2 {
    ArtifactId id;
    ArtifactRole role;
    ArtifactKind kind;
    ArtifactSchema schema;
    MediaType media_type;
    ContentHash content_digest;
    std::vector<ArtifactDependency> depends_on;
    ProducerName producer_name;
    ProducerVersion producer_version;
};

struct BuildIdentityV2 {
    FormatName format;
    SchemaVersion schema_version;
    ProjectIdentity project;
    ContentHash authoring_inputs_digest;
    DependencyLock dependency_lock;
    std::vector<BuildArtifactIdentityV2> artifacts;
};
```

The serialized root field order is `format`, `schema_version`, then the fields shown above.
The first two carry the fixed literals `format: "volt.project_result"` and
`schema_version: 2`. Every field is required; duplicate, missing, or unknown root fields
reject. `ProjectIdentity` is exactly `name`, nullable `version`, and nullable `description`
in that field order. `ProjectRunSummary` is exactly `ok`, the closed status
`clean | expected-diagnostics | failed`, profile name, and the unique executed stage keys
in the shown field order and fixed Project execution order.
It does not become EDA state. Its `ok` and status must equal the required diagnostics and
project-tests reports; disagreement rejects.

Every object and tagged variant in a v2 manifest is closed, not only the root. Missing,
duplicate, or unknown fields at any depth reject; readers must not preserve or ignore
extension members. A variant requires exactly its discriminant and that alternative's exact
payload.

`AuthoringInputs.records` is the explicit sorted set of input-kind, project-relative logical
name, and content-digest provenance records supplied by Project orchestration. It contains
the configured entrypoint plus source or non-library inputs explicitly registered by the
Project source-provenance contract. `entrypoint` must name exactly one `ProjectSource`
record. Logical input names are slash-separated,
non-empty, relative, and forbid empty, `.` and `..` segments, backslashes, drive/UNC syntax,
control bytes, and NULs; they are identifiers and are never opened during bundle inspection.
Duplicate kind/name pairs reject.

`AuthoringInputs.digest` is the SHA-256 of the canonical `entrypoint` and complete records
array with the digest field omitted. Library inputs are excluded because the dependency lock
covers them. Source records, but not source bytes, are bundled; inspection recomputes this
digest and never resolves a logical name or executes source. This is not an import tracer or
an arbitrary `ProjectResource` serializer; registration mechanics remain Project-owned and
outside this ADR. A v2 writer must fail only when its configured entrypoint or an explicitly
registered record cannot be deterministically named and hashed.

`ArtifactRole` states lifecycle; `ArtifactKind` states the typed payload. The mapping is
fixed:

| Role | v2 kinds |
| --- | --- |
| `model` | `LogicalModel`, `SchematicModel`, `BoardModel`, `CompiledBoard`, `ComponentDefinition`, `PartDefinition`, `SymbolDefinition`, `FootprintDefinition` |
| `view` | `BoardScene` |
| `render` | `SchematicSvg`, `BoardSvg`, `BoardLayerImage`, `WholeBoardGlb` |
| `report` | `Diagnostics`, `ProjectTests`, `Bom`, `Cpl` |
| `adapter` | `KicadPcb` |
| `asset` | `GlbAsset`, `StepAsset` |
| `delivery` | `FabricationPackage` |

Adding a role or kind is a ProjectBundle schema change. Media type and artifact schema
remain explicit because one kind may gain a deliberately versioned codec without changing
its lifecycle role. `ArtifactKind` selects its owner codec and allowed media family;
`ArtifactSchema.format` and `media_type` must match that kind. Unknown or mismatched pairs
reject. This ADR does not rename owner formats or pre-choose their next codec versions.

### Stable identity

`ArtifactId` is a typed graph identity: `ArtifactKind` plus the complete stable identity
of the owning model, asset, report, or export. `ArtifactRef` is the pair
`(ArtifactId, content_digest)` and identifies exact serialized bytes. Neither is a kernel
entity ID.

Kind selects exactly one owner alternative: logical, Schematic, Board, compiled Board, and
scene kinds use their correspondingly named identity; component, part, and
symbol/footprint kinds use `LibraryComponentRef`, `LibraryPartRef`, and
`LibraryAttachmentRef`; `GlbAsset` uses `LibraryAssetRef`; diagnostics/tests use
`ProjectSingletonIdentity{Primary}`; and every opt-in kind uses `ExportArtifactIdentity`.
Any other kind/owner pairing rejects.

- Logical models use their stable design key.
- Schematics use `(design key, schematic key)`.
- Authoring Boards use their `(DesignKey, BoardName)` pair.
- `CompiledBoard` uses its owner-defined identity: the persisted Design-scoped source
  `BoardName` and provenance digest. That provenance is computed from pre-bundle compile
  inputs and must not include a ProjectBundle build/bundle digest, artifact ID, descriptor,
  or serialized-byte digest.
- `BoardScene` uses the exact `CompiledBoard` identity it views; kind distinguishes it from
  that compiled artifact.
- Project diagnostics and project tests use reserved project singleton keys.
- Part definitions use their complete `LibraryPartRef`, ordered as library namespace, human
  version, part key, library-bundle digest, and part semantic digest. Component definitions
  use the equivalent `LibraryComponentRef` order with component key and component semantic
  digest.
- Symbol and footprint definitions use their owner-persisted `LibraryAttachmentRef`, ordered
  as library namespace, human version, attachment kind, stable key, library-bundle digest,
  and owner semantic or content digest. Thus every required definition kind has a complete
  origin-bearing ID.
- Default-graph GLB assets use `LibraryAssetRef`, ordered as library namespace, human version,
  media kind, library-bundle digest, and canonical content digest. The descriptor digest must
  equal that content digest; byte-identical assets from different library origins remain
  distinct provenance records.
- Generated export IDs use the canonical request digest plus the exporter's stable typed
  output key; a singleton output uses its reserved primary key. A Board layer-image target
  also carries its `BoardLayerKey`; a `StepAsset` uses its origin-bearing `LibraryAssetRef`
  as the output key. Paths and ordinals are never export identity.

Names used as owner keys are the names validated and persisted by the owning model codec.
Conditional v1 output names, paths, manifest order, and build-local kernel entity IDs are
not stable artifact identity. A semantic model change follows that owner's identity rules:
for example, a changed compiled provenance or component/part semantic digest creates a new
`ArtifactId`, while a lossless re-encoding may change only the descriptor digest. Renaming
or selecting a different semantic owner also creates a different ID.

Within one graph, IDs are unique and dependency targets use exact `ArtifactRef` values.
`ArtifactDescriptor.id.kind` must equal its descriptor kind. Duplicate IDs, duplicate
dependencies, conflicting reuse of an owner identity, or an ID/kind mismatch rejects.

### Schema, producer, build, and bundle digests

Artifact schema version controls decoding. Producer version is provenance and does not
authorize a loader to guess an unsupported schema.

Every digest uses Volt's canonical `ContentHash` spelling:
`sha256:` followed by 64 lowercase hexadecimal digits.

- `ArtifactDescriptor.content_digest` hashes the exact bytes at its path. Every declared
  artifact, including an opt-in export, has one.
- Each dependency edge repeats the target artifact ID and expected content digest. An ID
  match with a different digest is stale or mixed input and rejects.
- `BuildId` is a typed `ContentHash`: the SHA-256 digest of the canonical
  `BuildIdentityV2` object in the exact shown field order. Its format/schema literals match
  the v2 manifest, `authoring_inputs_digest` is `AuthoringInputs.digest`, and `artifacts` is
  the sorted `BuildArtifactIdentityV2` projection of required-default descriptors only. The
  projection intentionally excludes paths, export requests/artifacts, `build_id`,
  `bundle_digest`, and every descriptor's `producer.build` field to avoid self-reference.
- Required default payload bytes and descriptors are finalized before `BuildId` and must not
  embed the ProjectBundle build or bundle digest; selected exports may refer to the already
  computed build.
- Every project-produced artifact, including generated exports, has `producer.build` equal
  to the manifest `BuildId` content hash. A vendored library artifact, including a copied
  `StepAsset`, has `producer.build` equal to the exact `library_bundle_digest` in its
  origin-bearing ID and locked library record; there is no other producer-build escape.
- `bundle_digest` hashes the canonical v2 manifest semantics with only the
  `bundle_digest` field omitted. It therefore covers paths, export selection, producer
  records, the lock, the graph, and, transitively, every artifact content digest.

Hash preimages use one schema-defined compact UTF-8 JSON encoding. Objects use the field
order declared by their schema; no whitespace, BOM, or trailing newline is present. Strings
emit unescaped UTF-8, escape quote and reverse-solidus with one reverse-solidus, and encode
U+0000 through U+001F as lowercase `\u00xx`; booleans use `true`/`false`, and non-negative
integers use shortest decimal spelling. JSON `null` is permitted only for absent optional
project version or description; manifest hash types otherwise admit neither null nor floating
point.
Typed collections sort by the unsigned UTF-8 bytes of their complete stable keys, then
digest: artifacts by `ArtifactId`, dependencies by target ID/digest, libraries by
namespace/version/bundle digest, selections by full `LibraryPartRef`, source inputs by
kind/name/digest, and export requests by their canonical request digest. Duplicate JSON keys
reject.
Closed manifest enums and variant discriminants serialize as the lowercase snake-case
spelling of the names shown in this ADR; `ProjectStatus` alone uses the exact three spellings
listed above. Numeric enum encodings are forbidden. A tagged variant serializes as a closed
object with fields `type` then `value`, with a lowercase snake-case alternative name. `ArtifactId`
serializes as fields `kind` then `owner`, where `owner` is the kind-selected tagged identity
whose fields occur in the stable-identity order defined above. Strong string keys serialize
as their validated UTF-8 value; tuples serialize as closed objects, not ambiguous arrays.
The on-disk manifest may be pretty-printed, but hashes always use this canonical semantic
encoding. Digest equality is integrity and identity, not a signature or claim of source
authenticity.

## Dependency Lock

The v2 dependency lock is an exact offline record of consumed part-library origins and
selected parts, not a second closure graph, version request, resolver cache, registry
protocol, or generic package manager. Its native shape is equivalent to:

```cpp
struct LockedLibrary {
    LibraryNamespace library;
    LibraryVersion version;
    ContentHash library_bundle_digest;
};

struct LockedPartSelection {
    LibraryPartRef selected_part;
    ArtifactRef vendored_part;
};

struct DependencyLock {
    std::vector<LockedLibrary> libraries;
    std::vector<LockedPartSelection> selected_parts;
};
```

The lock has these semantics:

- Human-readable library versions identify releases; content digests are authoritative.
- Every `LibraryPartRef` persisted by a logical model has exactly one matching locked
  selection with the same library namespace/version, library digest, part key, and part
  digest. Its `vendored_part` must be the exact `PartDefinition` `ArtifactRef` with that
  identity, and every locked selection is referenced by at least one logical model.
- Absence is not a lock entry: an instance with no `LibraryPartRef` contributes no
  selected-part closure root, and reopening never synthesizes one. DNP and unplaced
  instances remain in each `CompiledBoard` logical snapshot, and their status alone is not
  a bundle structural failure; DNP does not require a selection. Any persisted non-empty
  reference, including one on a DNP or unplaced instance, remains exact selected-part truth
  and must satisfy the resolution and lock rule above. An ordinary model may retain an
  unresolved reference, but it cannot enter a completed self-contained v2 bundle.
- The library table admits every immutable `PartLibraryBundle` origin named by a reachable
  vendored artifact or by a `LibraryAssetRef` persisted in a reachable definition. A part
  may depend on a component contract or asset from another admitted library; cross-library
  edges are valid. The library rows are in bijection with that complete origin set: an
  unadmitted origin or a row unused by either a reachable artifact or persisted asset ref
  rejects.
- Artifact dependency edges are the sole closure topology. Closure roots are the union of
  external component definitions referenced directly by logical models and locked selected
  part artifacts. Their exact transitive graph contains referenced component contracts,
  default or explicit-override symbols, footprints, and only the GLB assets actually consumed
  by at least one `CompiledBoard` under its declared `models3d` capability. Shared members
  with the same origin-bearing ID are stored once. Missing or unrelated vendored closure
  members reject.
- STEP bytes are not part of the default closure. They appear only when selected as an
  export. The owning part payload must persist their complete `LibraryAssetRef`, including
  locked origin and content digest. That persisted ref admits the origin even when no STEP
  export is selected, so selecting an export does not rewrite the dependency lock.
- Locked libraries and selected parts are sorted and unique. Conflicting namespace/version
  origins, library digests, or part bindings reject.
- Opening uses only vendored bytes. It performs no version resolution, network lookup,
  library-source import, generator execution, or fallback to a newer installed library.
- Changing a selected part, library, component contract, required asset, or dependency
  digest creates a new `BuildId` and bundle digest. It never updates historical artifacts.

## Required Default Graph

`ExportSelection{}` is the default. A v2 bundle contains authoritative native models plus
the required inspection views and reports needed for offline inspection and Vault:

- at least one logical model, and every logical model produced by the project build;
- every Schematic produced by the build;
- every named authoring Board produced by the build;
- exactly one `CompiledBoard` for every named authoring Board;
- exactly one compact `BoardScene` for every `CompiledBoard`;
- exactly one project diagnostics report and one project-tests report;
- the exact reachable component/part/symbol/footprint closure rooted at logical
  external-component references and selected parts; and
- the exact union of GLB assets consumed by the `CompiledBoard` values under their declared
  `models3d` capabilities.

A project may produce no Schematic or no Board. In that case the corresponding set is
empty; the loader must not invent one. A project result with a Board that did not produce
its one complete compiled result and scene is not writable as v2. Historical revisions live
in separate immutable ProjectBundles, not beside the current result in one build. Each
scene's GLB references must be a subset of its own `CompiledBoard`'s consumed GLB closure;
without `models3d`, that set and the scene's GLB references are empty. Multiple Boards
remain complete alternatives, not assembly partitions, harness members, or implicit BOM
splits.

The required graph relationships are:

| Artifact | Required direct dependencies |
| --- | --- |
| component definition | its default symbol definition |
| selected part definition | its exact component definition, footprint, explicit symbol override if present, and the GLB attachments from that part consumed by any `CompiledBoard` in this graph |
| logical model | every external component definition and selected part definition referenced by its instances |
| Schematic | its one logical model |
| authoring Board | its one logical model |
| `CompiledBoard` | the exact logical model, authoring Board, and only the vendored definition/asset `ArtifactRef`s in its owner-declared consumed selected part/asset closure |
| `BoardScene` | its one `CompiledBoard` and every referenced part GLB asset |
| diagnostics/tests | exactly the artifacts evaluated by the report |
| selected export | exactly the authoritative models, compiled boards, or assets consumed by its exporter |

The graph is acyclic. A required default artifact must not depend on a selected export.
Each dependency list is the exact set of direct inputs: missing and extraneous edges reject.
The `CompiledBoard` owner contract validates its pre-bundle provenance; ProjectBundle only
requires its owner-declared input refs to equal the descriptor edges and its identity to equal
the decoded `CompiledBoardIdentity`.

### `BoardScene` and GLB

`BoardScene` has role `view`. It carries compact render/selection data and stable references
back to identities in one exact `CompiledBoard` revision; it may carry display transforms
and material information required by Vault. It references zero or more separately stored GLB
assets by `ArtifactRef`, never by an unchecked raw path, and every such asset must belong to
that `CompiledBoard`'s consumed `models3d` closure.

It must not copy or redefine Circuit connectivity, Board entities, selected-part truth,
footprint definitions, or compilation rules. It cannot be used as an authoritative input
to reconstruct or mutate a Board or `CompiledBoard`. Whole-board GLB is a separate opt-in
render and is not the scene or a replacement for referenced part GLBs.

## Opt-In Export Selection

Export selection is a closed typed build input:

```cpp
enum class ExportKind {
    SchematicSvg,
    BoardSvg,
    BoardLayerImage,
    KicadPcb,
    Bom,
    Cpl,
    Fabrication,
    Step,
    WholeBoardGlb,
};

struct ModelExportTarget {
    ArtifactRef model;
};

struct BoardLayerExportTarget {
    ArtifactRef compiled_board;
    BoardLayerKey layer;
};

struct LibraryAssetExportTarget {
    ArtifactRef selected_part;
    LibraryAssetRef asset;
};

using ExportTarget = std::variant<
    ModelExportTarget,
    BoardLayerExportTarget,
    LibraryAssetExportTarget>;

struct ExportRequestSchema {
    FormatName format;
    SchemaVersion version;
};

using ExportParameters = std::variant<
    NoExportParameters,
    SchematicSvgParameters,
    BoardSvgParameters,
    BoardLayerImageParameters,
    KicadPcbParameters,
    BomParameters,
    CplParameters,
    FabricationParameters,
    StepParameters,
    WholeBoardGlbParameters>;

struct ExportRequest {
    ExportKind kind;
    ExportTarget target;
    ExportRequestSchema request_schema;
    ExportParameters parameters;
};

using ExportSelection = std::vector<ExportRequest>;
```

The allowed pairings are fixed. Schematic SVG targets an exact Schematic descriptor; BOM
targets an exact logical-model descriptor; Board SVG, KiCad, CPL, fabrication, and
whole-board GLB target an exact `CompiledBoard` descriptor. A Board layer image targets an
exact compiled descriptor plus one of its canonical `BoardLayerKey` values. STEP targets an
exact selected-part descriptor plus an origin-complete STEP `LibraryAssetRef` persisted by
that part. Bare design, Schematic, Board, or asset keys are not export targets.

The default selection is empty. Requests target stable typed identities, not paths, globs,
stage ordinals, conditional output names, or bare owner keys. Duplicate or kind-incompatible
requests reject.

`ExportParameters` is a closed native variant selected by kind, never an opaque byte or
string property bag. The owning exporter contract defines each alternative's fields and
stable output-key vocabulary before that kind is admitted to a writer; a no-options request
uses the explicit empty alternative only when that owner contract permits it. The canonical
request digest covers kind, complete target `ArtifactRef`s, request schema, and every typed
parameter. Any byte-affecting producer input must therefore be represented in the request or
in an exact artifact dependency.

SVG, Board layer images, KiCad, BOM/CPL copies, fabrication output, STEP, and whole-board
GLB appear only through this mechanism. Each request emits one or more normal graph members
with role, schema, producer, digest, complete dependency edges, and stable owner-defined
output keys. Singleton and fan-out cardinality belong to the focused exporter contract;
paths and emission order never substitute for output identity. Every export descriptor maps
to exactly one selected request, and no undeclared output is permitted. A `StepAsset` depends
directly on its selected-part target, and its descriptor digest must equal the STEP attachment
digest in the target `LibraryAssetRef`. Changing export selection creates a new bundle digest
and never mutates an earlier bundle.

## Safe Paths

`RelativeBundlePath` is a manifest path, not an arbitrary filesystem path.

- It is non-empty, root-relative, slash-separated ASCII.
- Each segment matches `[A-Za-z0-9_][A-Za-z0-9._-]*` and is neither `.` nor `..`.
- A segment ending in `.` rejects. Its case-folded basename before the first `.` must not
  be `CON`, `PRN`, `AUX`, `NUL`, `COM1` through `COM9`, or `LPT1` through `LPT9`.
- Absolute paths, empty segments, backslashes, drive/UNC syntax, colons, control bytes,
  and NULs reject.
- Descriptor paths are unique both byte-for-byte and under ASCII case folding, so the
  bundle has one meaning on case-sensitive and case-insensitive filesystems.
- No two descriptor paths may resolve to the same host file identity. Hard-link, Unicode,
  or other host alias collisions reject rather than merge two records.
- `manifest.volt.json` is reserved under ASCII case folding and cannot also be an artifact
  path.
- Every path component and final target must exist beneath the opened bundle root without
  symbolic-link, junction, or other reparse-point traversal; the final target must be a
  regular file.
- The loader must perform containment-safe opens and must not trust a lexical precheck
  followed by an unconstrained second path lookup. It opens each artifact once and captures
  owned immutable bytes, or an operating-system snapshot primitive with equivalent
  immutability. Hashing, decoding, and later asset serving all use that same snapshot; a bare
  retained file handle is insufficient.
- A v2 root contains only `manifest.volt.json`, declared regular artifact files, and their
  parent directories. Any undeclared file, symbolic link, junction, or reparse point rejects.

Any unsafe, aliased, escaping, missing, or non-regular path is a structural load failure.

## Public C++ Open and Lifetime

The public native owner is one move-only `ProjectBundle`. It keeps decoded logical owners
at stable addresses before constructing Schematics and Boards that borrow them. Its public
shape is equivalent to:

```cpp
enum class BundleIntegrityStatus {
    LegacyUnverified,
    VerifiedV2,
};

using ProjectBundleContentsView = std::variant<
    LegacyProjectBundleV1View,
    ProjectBundleGraphV2View>;

class ProjectBundle final {
  public:
    static ProjectBundle open(const std::filesystem::path &root);

    [[nodiscard]] BundleSchemaVersion schema_version() const noexcept;
    [[nodiscard]] BundleIntegrityStatus integrity_status() const noexcept;
    [[nodiscard]] ProjectBundleContentsView view() const;
    [[nodiscard]] ProjectBundleGraphV2View require_v2() const;
};
```

The container is not copyable; moving it must preserve the address and lifetime of loaded
storage. Version views and their inspection handles are read-only leases over that storage.
They may not expose raw copyable authoring projections or allow a `Schematic`/`Board` copy to
escape with a dangling Circuit borrow. A handle either remains tied to the container or
retains the immutable storage lease itself. Circuits and their borrowing Schematics or Boards
cannot be detached in a way that breaks those relationships. This does not weaken the
owner-defined standalone semantics of immutable `CompiledBoard`: a bundle view may return an
independently owned compiled value when its public type supports that operation. Scenes,
vendored definitions, and opaque bytes follow their owner codec's value-or-lease semantics.
Exact private storage and ABI shape remain implementation details.

`view()` makes version discrimination explicit. `require_v2()` returns the v2 view or throws
the typed unavailable-in-v1 error; it never returns null.

`ProjectBundle::open` returns a complete owner or throws one typed structural load error.
It never returns an incomplete owner, callbacks into partially loaded state, or a collection
of successfully decoded fragments.

## Validation and Open Order

Opening follows this order for v2:

1. Open the fixed root manifest without symlink traversal, retain one immutable byte
   snapshot, and validate format and exact schema version before constructing model state.
2. Validate manifest structure, enum values, producer/schema fields, canonical IDs and
   ordering, `ArtifactId`/descriptor kind equality, safe unique paths, required artifact
   cardinality, authoring-input digest, export request kind/target/schema/parameter
   compatibility, and the dependency lock.
3. Validate that all dependency targets exist, edge digests match their descriptors, the
   exact direct-edge sets form an acyclic graph, every project artifact uses the project
   `BuildId` content hash, and each library artifact uses its locked library-bundle digest.
4. Open every declared artifact once through the confined root, capture an owned immutable
   byte snapshot or equivalent OS snapshot, and verify its exact digest from those bytes. A
   declared optional export is optional only by omission; if declared, it must validate.
5. Decode the same immutable snapshots in dependency order: vendored
   component/part/projection closure, logical models, Schematics and Boards, compiled boards,
   scenes, diagnostics/tests, then opaque exports. No artifact path is looked up again.
6. Run owner-local structural validation and cross-artifact checks: selected refs match the
   lock; component/part semantic digests and content-addressed asset IDs match their payloads;
   each decoded `CompiledBoardIdentity` and owner-declared direct-input refs equal its
   descriptor identity and edges after owner-codec provenance validation; and every scene's
   internal compiled revision and GLB refs equal its pinned edges. A `StepAsset` digest must
   equal the selected part's pinned attachment digest. The run `ok` and status must equal
   the decoded diagnostics/tests reports. Each export descriptor must map to exactly one
   selected request; generated output IDs carry its request digest and a valid owner-defined
   output key. A `StepAsset` output key must equal its request's `LibraryAssetRef`.
7. Compute and compare `BuildId` and `bundle_digest`, then publish the immutable owner.

Path escape, unsupported schema, stale or missing digest, mixed-build dependency,
missing required artifact, invalid lock closure, decode failure, or cross-owner mismatch
rejects the whole open. None is downgraded to a diagnostic, and no partial state is exposed.

Disposable views that are not declared are simply absent. A stale declared view cannot
silently affect authoritative model loading; strict `ProjectBundle::open` rejects the
bundle rather than exposing a partial authoritative subset.

## v1/v2 Compatibility Windows

ProjectBundle manifest version is separate from logical, Schematic, PCB, part, and
component-contract schema versions.

### Write window

- Until the v2 writer is implemented and made public, the existing Python
  `ProjectResult.write()` remains the only writer and emits v1.
- At the v2 writer cutover, all new public bundle writes emit v2. No public `write_v1`
  option or second semantic writer path is added.
- A v2 writer writes a new empty destination and never rewrites an existing bundle.
  A changed recorded source input, dependency lock, default artifact, or export selection
  produces a new build and/or bundle digest at a new destination.
- V2 writing requires a structurally complete required graph, though diagnostics may contain
  design errors and project tests may fail. An incomplete manually constructed result emits
  no v2 bundle.

### Read window

- The first public native bundle opener supports both v1 and v2 and never executes source
  for either version.
- V2 receives the verified typed graph described by this ADR and reports
  `BundleIntegrityStatus::VerifiedV2`.
- V1 receives a distinct `LegacyProjectBundleV1View` and reports
  `BundleIntegrityStatus::LegacyUnverified`. It exposes every actual v1 manifest artifact
  record and decodes every declared supported logical, Schematic, and Board document. Each
  declared record is opened containment-safely under the v1-compatible path grammar and
  captured as immutable bytes; any unsafe path or supported-model decode failure rejects the
  whole open.
- The v1-compatible grammar accepts legacy UTF-8 slash-separated relative names but rejects
  empty, `.` or `..` segments, absolute paths, backslashes, drive/UNC syntax, control bytes,
  NULs, and symlink/reparse traversal. It does not retroactively impose v2's ASCII,
  case-folding, trailing-dot, reserved-name, or closed-world rules.
- Byte-duplicate v1 manifest paths reject. Distinct legacy spellings that resolve to the
  same host file identity also reject; the adapter never aliases or merges records.
- The v1 adapter verifies every `sha256` field that is present using v1's bare-lowercase-hex
  spelling. Partial digest coverage never upgrades its overall integrity status. Undeclared
  v1 files are outside its identity and are neither exposed nor executed.
- V1 does not synthesize roles, stable artifact IDs, dependency edges, full digest coverage,
  a dependency lock, selected closure, `CompiledBoard`, or `BoardScene`. Its PCB viewer
  cache is not a scene. An operation requiring missing v2 meaning returns a typed
  unavailable-in-v1 error; it must not infer, rebuild, import Python, or consult libraries.
- Readers never upgrade or mutate either version in place. A future explicit converter
  must write a new v2 bundle and must fail unless every required v2 meaning is supplied.
- V1 read support remains until a separate accepted ADR inventories all callers and
  checked-in fixtures, supplies a migration path, and authorizes removal. There is no
  calendar- or guess-based removal window.

The current v1 writer may replace its own output directory and supplies a bare hexadecimal
digest only for part-model assets. This ADR does not retroactively claim that v1 is immutable
or fully verified; historical v1 artifacts are treated as read-only once opened.

## Historical Immutability

All v2 artifacts are immutable historical build outputs. A change to a recorded source or
declared input changes `AuthoringInputs.digest`; a dependency change changes the lock; and a
selected part, compiled input, scene, or export-selection change creates new content.
Those changes produce new build and/or bundle digests. They never refresh a file, edge, lock
entry, compiled board, or scene inside an earlier bundle. Later library releases cannot
change an old bundle when it is reopened offline.

Loaders are read-only. Artifact IDs identify semantic slots within one graph; content
digests identify the exact historical bytes. Neither grants an in-place update operation.

## Verification Contract

Implementation is admitted only with tests that prove:

- two clean identical builds produce identical manifests, IDs, graph ordering, locks,
  `BuildId`, bundle digest, and payload bytes;
- changing each recorded source/dependency/default payload changes the expected
  input/build/bundle digest without mutating the old output;
- output path or sibling cardinality changes do not silently redefine `ArtifactId`;
- compiled provenance and component/part semantic identities remain distinct from serialized
  byte digests and validate against their exact graph edges;
- an absent exact-part selection contributes no lock row or closure root and remains absent
  after reopen; DNP or unplaced status alone remains non-structural, while a non-empty
  unresolved selection prevents v2 writing;
- a `CompiledBoard` without `models3d` contributes no GLB assets or scene GLB references,
  while one with `models3d` contributes exactly its consumed GLB closure and each scene
  references only its own compiled subset;
- each required kind has the fixed role and cardinality, and exact direct-edge sets reject
  both missing and extraneous dependencies;
- absolute, parent, drive/UNC, backslash, trailing-dot, device-name, case-alias,
  symlink/reparse, manifest-alias, and undeclared paths reject;
- missing bytes, byte tampering, stale edge digests, mixed project builds, missing closure,
  missing compiled results or scenes for named Boards, mismatched content-addressed asset IDs,
  bad compiled provenance, and bad GLB references reject before exposure;
- v2 opens offline and preserves owner identity and Circuit/Schematic/Board lifetimes;
- empty export selection omits every opt-in kind and each typed request adds only its
  declared export closure;
- a source sentinel proves all v1/v2 `--bundle` inspection paths do not import or execute
  project or library source; and
- v1 fixtures remain explicitly legacy and never acquire inferred v2 meaning.

## Consequences

- The v2 default is larger than a models-only bundle because it is complete for native
  offline inspection and retains the Vault closure for every named Board's compiled result,
  but it excludes reproducible delivery copies by default.
- Every producer must declare precise codecs, inputs, and digests; stale caches cannot be
  tolerated as successful partial loads.
- Public C++ owns reopened lifetimes while Python remains the authoring and orchestration
  surface.
- Project bundles reuse canonical content-addressed library payload identities without
  duplicating domain ownership.
- Bundle integrity is deterministic and testable, but signing, trust distribution, and
  registry policy remain separate concerns.

## Non-Goals

- Implementing v1/v2 readers, writers, conversion, Python bindings, or CLI UX.
- Implementing `CompiledBoard`, `BoardScene`, Vault, GLB generation, or asset codecs.
- Migrating SVG, layer image, KiCad, BOM/CPL, fabrication, STEP, or whole-board GLB
  exporters.
- Performing downstream P6, Q1-Q4, BoardScene/Vault, or delivery/export cutover work.
- Designing registries, package resolution, signing, upload, caching, retention, or
  distribution workflows.
- Changing owner model schemas, Circuit/Schematic/Board responsibilities, Board cardinality,
  part semantics, manufacturing authority, or project stage orchestration.

## Revisit Trigger

Revisit this ADR only when a concrete artifact cannot be represented by the closed role/kind
and dependency contract, or when measured compatibility evidence justifies removing v1
reading. A new export format alone may add a deliberately versioned kind through a successor
bundle schema; it does not justify an untyped kind, path, or dependency escape hatch.
