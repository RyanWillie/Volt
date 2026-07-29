#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::io {

/** Supported deterministic ProjectBundle graph schema. */
enum class ProjectBundleSchemaVersion : std::uint32_t {
    V2 = 2,
};

/** Supported native ProjectBundle producer contract. */
enum class ProjectBundleProducerVersion : std::uint32_t {
    V1 = 1,
};

/** Closed lifecycle roles admitted by ProjectBundle v2. */
enum class ArtifactRole {
    Model,
    View,
    Render,
    Report,
    Adapter,
    Asset,
    Delivery,
};

/** Closed payload kinds admitted by ProjectBundle v2. */
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

/** Stable non-empty project-local logical design key. */
class DesignKey {
  public:
    /** Construct a validated project-local design key. */
    explicit DesignKey(std::string value);

    /** Return the canonical design-key spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare canonical design keys. */
    [[nodiscard]] bool operator==(const DesignKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Stable non-empty project-local schematic key. */
class SchematicKey {
  public:
    /** Construct a validated project-local schematic key. */
    explicit SchematicKey(std::string value);

    /** Return the canonical schematic-key spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare canonical schematic keys. */
    [[nodiscard]] bool operator==(const SchematicKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Safe slash-separated logical authoring-input name. */
class LogicalInputName {
  public:
    /** Construct a validated logical authoring-input name. */
    explicit LogicalInputName(std::string value);

    /** Return the canonical slash-separated input name. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare canonical logical input names. */
    [[nodiscard]] bool operator==(const LogicalInputName &) const noexcept = default;

  private:
    std::string value_;
};

/** Authoring input provenance class. */
enum class AuthoringInputKind {
    ProjectSource,
    DeclaredInput,
};

/** Explicit authoring input whose bytes are hashed but never bundled. */
class AuthoringInput {
  public:
    /** Construct one explicit authoring input. */
    AuthoringInput(AuthoringInputKind kind, LogicalInputName name, std::string bytes);

    /** Return the input provenance class. */
    [[nodiscard]] AuthoringInputKind kind() const noexcept { return kind_; }

    /** Return the logical input name. */
    [[nodiscard]] const LogicalInputName &name() const noexcept { return name_; }

    /** Return the exact input bytes. */
    [[nodiscard]] std::string_view bytes() const noexcept { return bytes_; }

  private:
    AuthoringInputKind kind_;
    LogicalInputName name_;
    std::string bytes_;
};

/** Human project identity persisted by Project orchestration. */
struct ProjectIdentity {
    /** Stable human project name. */
    std::string name;
    /** Optional human project version. */
    std::optional<std::string> version;
    /** Optional human project description. */
    std::optional<std::string> description;
};

/** Closed completed-run status. */
enum class ProjectStatus {
    Clean,
    ExpectedDiagnostics,
    Failed,
};

/** Deterministic summary of the completed Project execution. */
struct ProjectRunSummary {
    /** Whether the completed run satisfied its requested policy. */
    bool ok;
    /** Closed completed-run status. */
    ProjectStatus status;
    /** Named Project execution profile. */
    std::string profile;
    /** Canonically ordered executed stage names. */
    std::vector<std::string> stages;
};

/** Owner identity of one logical artifact. */
struct LogicalArtifactIdentity {
    /** Owning logical design. */
    DesignKey design;
    /** Compare logical artifact owners. */
    [[nodiscard]] bool operator==(const LogicalArtifactIdentity &) const noexcept = default;
};

/** Owner identity of one schematic artifact. */
struct SchematicArtifactIdentity {
    /** Owning logical design. */
    DesignKey design;
    /** Project-local schematic key. */
    SchematicKey schematic;
    /** Compare schematic artifact owners. */
    [[nodiscard]] bool operator==(const SchematicArtifactIdentity &) const noexcept = default;
};

/** Owner identity of one authoring Board artifact. */
struct BoardArtifactIdentity {
    /** Owning logical design. */
    DesignKey design;
    /** Project-local named Board. */
    BoardName board;
    /** Compare authoring Board artifact owners. */
    [[nodiscard]] bool operator==(const BoardArtifactIdentity &) const noexcept = default;
};

/** Owner identity of one BoardScene artifact. */
struct BoardSceneArtifactIdentity {
    /** Exact compiled Board revision projected by the scene. */
    CompiledBoardIdentity compiled_board;
    /** Compare BoardScene artifact owners. */
    [[nodiscard]] bool operator==(const BoardSceneArtifactIdentity &) const noexcept = default;
};

/** Reserved project report singleton identity. */
enum class ProjectSingletonKey {
    Primary,
};

/** Owner identity of one project singleton report. */
struct ProjectSingletonIdentity {
    /** Reserved singleton slot. */
    ProjectSingletonKey key;
    /** Compare project singleton owners. */
    [[nodiscard]] bool operator==(const ProjectSingletonIdentity &) const noexcept = default;
};

/** Exact origin-bearing component-definition identity. */
struct LibraryComponentRef {
    /** Exact source library namespace. */
    std::string library_namespace;
    /** Exact source library version. */
    std::string library_version;
    /** Component key within that library. */
    ComponentKey component_key;
    /** Digest of the complete source library bundle. */
    ContentHash library_bundle_digest;
    /** Digest of the component-definition payload. */
    ContentHash component_digest;
    /** Compare exact component-definition references. */
    [[nodiscard]] bool operator==(const LibraryComponentRef &) const noexcept = default;
};

/** Closed library attachment identity kind. */
enum class LibraryAttachmentKind {
    Symbol,
    Footprint,
};

/** Exact origin-bearing symbol or footprint identity. */
struct LibraryAttachmentRef {
    /** Exact source library namespace. */
    std::string library_namespace;
    /** Exact source library version. */
    std::string library_version;
    /** Closed attachment kind. */
    LibraryAttachmentKind kind;
    /** Attachment key within that library. */
    std::string key;
    /** Digest of the complete source library bundle. */
    ContentHash library_bundle_digest;
    /** Digest of the attachment payload. */
    ContentHash content_digest;
    /** Compare exact library attachment references. */
    [[nodiscard]] bool operator==(const LibraryAttachmentRef &) const noexcept = default;
};

/** Closed library asset identity kind. */
enum class LibraryAssetKind {
    Glb,
    Step,
};

/** Exact origin-bearing GLB or STEP identity. */
struct LibraryAssetRef {
    /** Exact source library namespace. */
    std::string library_namespace;
    /** Exact source library version. */
    std::string library_version;
    /** Closed asset kind. */
    LibraryAssetKind kind;
    /** Digest of the complete source library bundle. */
    ContentHash library_bundle_digest;
    /** Digest of the asset payload. */
    ContentHash content_digest;
    /** Compare exact library asset references. */
    [[nodiscard]] bool operator==(const LibraryAssetRef &) const noexcept = default;
};

/** Canonical owner-declared Board layer name used by layer export identity. */
class BoardLayerKey {
  public:
    /** Construct a validated owner-declared Board layer key. */
    explicit BoardLayerKey(std::string value);

    /** Return the canonical Board layer spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare canonical Board layer keys. */
    [[nodiscard]] bool operator==(const BoardLayerKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Reserved singleton export output key. */
enum class PrimaryExportOutputKey {
    Primary,
};

/** Closed stable output-key vocabulary selected by an exporter contract. */
using ExportOutputKey = std::variant<PrimaryExportOutputKey, BoardLayerKey, LibraryAssetRef>;

/** Owner identity of one selected export output. */
struct ExportArtifactIdentity {
    /** Digest of the complete typed export request. */
    ContentHash request_digest;
    /** Stable output key within the export request. */
    ExportOutputKey output_key;
    /** Compare selected export artifact owners. */
    [[nodiscard]] bool operator==(const ExportArtifactIdentity &) const noexcept = default;
};

/** Closed owner identity vocabulary selected by ArtifactKind. */
using ArtifactOwnerIdentity =
    std::variant<LogicalArtifactIdentity, SchematicArtifactIdentity, BoardArtifactIdentity,
                 CompiledBoardIdentity, BoardSceneArtifactIdentity, ProjectSingletonIdentity,
                 LibraryComponentRef, LibraryPartRef, LibraryAttachmentRef, LibraryAssetRef,
                 ExportArtifactIdentity>;

/** Path-independent typed graph identity. */
class ArtifactId {
  public:
    /** Construct a validated kind-shaped artifact identity. */
    ArtifactId(ArtifactKind kind, ArtifactOwnerIdentity owner);

    /** Return the closed artifact kind. */
    [[nodiscard]] ArtifactKind kind() const noexcept { return kind_; }

    /** Return the kind-shaped owner identity. */
    [[nodiscard]] const ArtifactOwnerIdentity &owner() const noexcept { return owner_; }

    /** Compare typed artifact identities. */
    [[nodiscard]] bool operator==(const ArtifactId &) const noexcept = default;

  private:
    ArtifactKind kind_;
    ArtifactOwnerIdentity owner_;
};

/** Exact graph reference to one artifact identity and payload digest. */
class ArtifactRef {
  public:
    /** Construct one exact graph reference. */
    ArtifactRef(ArtifactId artifact, ContentHash content_digest);

    /** Return the referenced typed identity. */
    [[nodiscard]] const ArtifactId &artifact() const noexcept { return artifact_; }

    /** Return the referenced payload digest. */
    [[nodiscard]] const ContentHash &content_digest() const noexcept { return content_digest_; }

    /** Compare exact graph references. */
    [[nodiscard]] bool operator==(const ArtifactRef &) const noexcept = default;

  private:
    ArtifactId artifact_;
    ContentHash content_digest_;
};

/** Safe canonical root-relative path inside one ProjectBundle. */
class RelativeBundlePath {
  public:
    /** Construct a validated safe root-relative bundle path. */
    explicit RelativeBundlePath(std::string value);

    /** Return the canonical slash-separated bundle path. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare canonical bundle paths. */
    [[nodiscard]] bool operator==(const RelativeBundlePath &) const noexcept = default;

  private:
    std::string value_;
};

/** Immutable public descriptor of one bundled payload. */
class ArtifactDescriptor {
  public:
    /** Construct one fully validated descriptor; normally produced by ProjectBundleBuilder. */
    ArtifactDescriptor(ArtifactId id, ArtifactRole role, ArtifactKind kind,
                       std::string schema_format, std::uint32_t schema_version,
                       std::string media_type, RelativeBundlePath path, ContentHash content_digest,
                       std::vector<ArtifactRef> dependencies, std::string producer_name,
                       std::uint32_t producer_version, ContentHash producer_build);

    /** Return the path-independent artifact identity. */
    [[nodiscard]] const ArtifactId &id() const noexcept { return id_; }

    /** Return the lifecycle role. */
    [[nodiscard]] ArtifactRole role() const noexcept { return role_; }

    /** Return the closed payload kind. */
    [[nodiscard]] ArtifactKind kind() const noexcept { return kind_; }

    /** Return the payload schema-format identifier. */
    [[nodiscard]] const std::string &schema_format() const noexcept { return schema_format_; }

    /** Return the payload schema version. */
    [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }

    /** Return the payload media type. */
    [[nodiscard]] const std::string &media_type() const noexcept { return media_type_; }

    /** Return the canonical payload path. */
    [[nodiscard]] const RelativeBundlePath &path() const noexcept { return path_; }

    /** Return the exact payload digest. */
    [[nodiscard]] const ContentHash &content_digest() const noexcept { return content_digest_; }

    /** Return the exact direct dependency edges. */
    [[nodiscard]] std::span<const ArtifactRef> dependencies() const noexcept {
        return dependencies_;
    }

    /** Return the native producer name. */
    [[nodiscard]] const std::string &producer_name() const noexcept { return producer_name_; }

    /** Return the native producer contract version. */
    [[nodiscard]] std::uint32_t producer_version() const noexcept { return producer_version_; }

    /** Return the native producer build digest. */
    [[nodiscard]] const ContentHash &producer_build() const noexcept { return producer_build_; }

  private:
    ArtifactId id_;
    ArtifactRole role_;
    ArtifactKind kind_;
    std::string schema_format_;
    std::uint32_t schema_version_;
    std::string media_type_;
    RelativeBundlePath path_;
    ContentHash content_digest_;
    std::vector<ArtifactRef> dependencies_;
    std::string producer_name_;
    std::uint32_t producer_version_;
    ContentHash producer_build_;
};

/** Typed SHA-256 identity of one complete required-default build graph. */
class BuildId {
  public:
    /** Construct a build identity from its canonical graph digest. */
    explicit BuildId(ContentHash value);

    /** Return the canonical graph digest. */
    [[nodiscard]] const ContentHash &content_hash() const noexcept { return value_; }

    /** Compare build identities. */
    [[nodiscard]] bool operator==(const BuildId &) const noexcept = default;

  private:
    ContentHash value_;
};

/** One exact immutable library origin admitted by the graph closure. */
struct LockedLibrary {
    /** Exact source library namespace. */
    std::string library_namespace;
    /** Exact source library version. */
    std::string library_version;
    /** Digest of the complete vendored library bundle. */
    ContentHash library_bundle_digest;
    /** Compare exact locked libraries. */
    [[nodiscard]] bool operator==(const LockedLibrary &) const noexcept = default;
};

/** One selected part binding to its exact vendored graph artifact. */
struct LockedPartSelection {
    /** Exact selected part binding. */
    LibraryPartRef selected_part;
    /** Exact vendored part-definition artifact. */
    ArtifactRef vendored_part;
    /** Compare exact locked selected-part bindings. */
    [[nodiscard]] bool operator==(const LockedPartSelection &) const noexcept = default;
};

/** Canonically ordered exact offline dependency lock. */
class DependencyLock {
  public:
    /** Construct and canonically order an exact offline dependency lock. */
    DependencyLock(std::vector<LockedLibrary> libraries = {},
                   std::vector<LockedPartSelection> selected_parts = {});

    /** Return the canonically ordered locked libraries. */
    [[nodiscard]] std::span<const LockedLibrary> libraries() const noexcept { return libraries_; }

    /** Return the canonically ordered selected-part bindings. */
    [[nodiscard]] std::span<const LockedPartSelection> selected_parts() const noexcept {
        return selected_parts_;
    }

  private:
    std::vector<LockedLibrary> libraries_;
    std::vector<LockedPartSelection> selected_parts_;
};

/** Closed opt-in export kinds. */
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

/** Exact authoritative model target for one export. */
struct ModelExportTarget {
    /** Exact authoritative model consumed by the exporter. */
    ArtifactRef model;
};

/** Exact compiled Board and layer target. */
struct BoardLayerExportTarget {
    /** Exact compiled Board consumed by the exporter. */
    ArtifactRef compiled_board;
    /** Owner-declared Board layer to render. */
    BoardLayerKey layer;
};

/** Exact selected-part and asset target. */
struct LibraryAssetExportTarget {
    /** Exact selected part that admitted the asset. */
    ArtifactRef selected_part;
    /** Exact vendored library asset to export. */
    LibraryAssetRef asset;
};

/** Closed export target vocabulary. */
using ExportTarget =
    std::variant<ModelExportTarget, BoardLayerExportTarget, LibraryAssetExportTarget>;

struct NoExportParameters {};

struct SchematicSvgParameters {};

struct BoardSvgParameters {};

struct BoardLayerImageParameters {};

struct KicadPcbParameters {};

struct BomParameters {};

struct CplParameters {};

struct FabricationParameters {};

struct StepParameters {};

struct WholeBoardGlbParameters {};

/** Closed per-export parameter vocabulary. */
using ExportParameters =
    std::variant<NoExportParameters, SchematicSvgParameters, BoardSvgParameters,
                 BoardLayerImageParameters, KicadPcbParameters, BomParameters, CplParameters,
                 FabricationParameters, StepParameters, WholeBoardGlbParameters>;

/** Closed supported export-request schema version. */
enum class ExportRequestSchemaVersion : std::uint32_t {
    V1 = 1,
};

/** Fixed native export-request schema contract. */
class ExportRequestSchema {
  public:
    /** Construct a supported native export-request schema. */
    explicit ExportRequestSchema(
        ExportRequestSchemaVersion version = ExportRequestSchemaVersion::V1);

    /** Return the fixed export-request schema format. */
    [[nodiscard]] std::string_view format() const noexcept { return "volt.export-request"; }

    /** Return the export-request schema version. */
    [[nodiscard]] ExportRequestSchemaVersion version() const noexcept { return version_; }

    /** Compare export-request schema contracts. */
    [[nodiscard]] bool operator==(const ExportRequestSchema &) const noexcept = default;

  private:
    ExportRequestSchemaVersion version_;
};

/** One typed opt-in export request. */
struct ExportRequest {
    /** Closed requested export kind. */
    ExportKind kind;
    /** Kind-shaped exact export target. */
    ExportTarget target;
    /** Native export-request schema contract. */
    ExportRequestSchema request_schema;
    /** Kind-shaped typed export parameters. */
    ExportParameters parameters;
};

/** Ordered opt-in native export requests; empty selects no exports. */
using ExportSelection = std::vector<ExportRequest>;

/** One native-owned project report payload. */
struct ProjectReport {
    /** Exact native report payload bytes. */
    std::string bytes;
};

/** Deterministic output representation. */
enum class ProjectBundleRepresentation {
    Directory,
    Zip,
};

/**
 * Complete in-memory deterministic v2 publication.
 *
 * The value owns canonical payload and manifest bytes. Publication is atomic and refuses every
 * non-empty destination.
 */
class ProjectBundlePublication final {
  public:
    /** Opaque complete native publication storage. */
    class Storage;

    /** Adopt complete builder-produced storage. */
    explicit ProjectBundlePublication(std::unique_ptr<Storage> storage);

    /** ProjectBundle publications are immutable and non-copyable. */
    ProjectBundlePublication(const ProjectBundlePublication &) = delete;
    /** ProjectBundle publications are immutable and non-copy-assignable. */
    ProjectBundlePublication &operator=(const ProjectBundlePublication &) = delete;
    /** Move a complete unpublished publication. */
    ProjectBundlePublication(ProjectBundlePublication &&) noexcept;
    /** Move-assign a complete unpublished publication. */
    ProjectBundlePublication &operator=(ProjectBundlePublication &&) noexcept;
    /** Destroy native publication storage. */
    ~ProjectBundlePublication();

    /** Return the required-default authoritative build identity. */
    [[nodiscard]] const BuildId &build_id() const noexcept;
    /** Return the digest of the complete canonical bundle. */
    [[nodiscard]] const ContentHash &bundle_digest() const noexcept;
    /** Return the exact offline dependency lock. */
    [[nodiscard]] const DependencyLock &dependency_lock() const noexcept;
    /** Return the canonical manifest bytes. */
    [[nodiscard]] std::string_view manifest_bytes() const noexcept;
    /** Return canonical artifact descriptors. */
    [[nodiscard]] std::span<const ArtifactDescriptor> artifacts() const noexcept;
    /** Return canonical root-relative publication paths. */
    [[nodiscard]] std::vector<std::string> paths() const;
    /** Return canonical deterministic archive bytes. */
    [[nodiscard]] std::string archive_bytes() const;

    /** Atomically publish to a new empty destination. */
    void write(
        const std::filesystem::path &destination,
        ProjectBundleRepresentation representation = ProjectBundleRepresentation::Directory) const;

  private:
    std::unique_ptr<Storage> storage_;
};

/**
 * Owner-shaped native v2 graph builder.
 *
 * Callers append complete owner artifacts. The builder derives artifact identity, closure, direct
 * edges, paths, digests, dependency lock, and export outputs; callers cannot supply a manifest or
 * arbitrary graph edges.
 */
class ProjectBundleBuilder {
  public:
    /** Begin one native ProjectBundle graph from completed Project inputs and reports. */
    ProjectBundleBuilder(ProjectIdentity project, ProjectRunSummary run,
                         LogicalInputName entrypoint, std::vector<AuthoringInput> inputs,
                         ProjectReport diagnostics, ProjectReport tests);
    /** Builder state is unique and non-copyable. */
    ProjectBundleBuilder(const ProjectBundleBuilder &) = delete;
    /** Builder state is unique and non-copy-assignable. */
    ProjectBundleBuilder &operator=(const ProjectBundleBuilder &) = delete;
    /** Move complete builder state. */
    ProjectBundleBuilder(ProjectBundleBuilder &&) noexcept;
    /** Move-assign complete builder state. */
    ProjectBundleBuilder &operator=(ProjectBundleBuilder &&) noexcept;
    /** Destroy native builder state. */
    ~ProjectBundleBuilder();

    /** Add one authoritative logical model and its exact reachable library closure. */
    ProjectBundleBuilder &add_logical(DesignKey design, const Circuit &circuit,
                                      const PartLibraryBundle &selected_closure);
    /** Add one authoritative Schematic owned by an already-added logical model. */
    ProjectBundleBuilder &add_schematic(DesignKey design, SchematicKey schematic,
                                        const Schematic &model);
    /** Add one complete Board, CompiledBoard, BoardScene, and exact library closure. */
    ProjectBundleBuilder &add_board(DesignKey design, const Board &authoring_board,
                                    const CompiledBoard &compiled, const BoardScene &scene,
                                    const PartLibraryBundle &selected_closure);
    /** Replace the closed opt-in export selection. */
    ProjectBundleBuilder &select_exports(ExportSelection selection);

    /** Validate and materialize one complete immutable publication. */
    [[nodiscard]] ProjectBundlePublication build() const;

  private:
    class Storage;
    std::unique_ptr<Storage> storage_;
};

/** Stable lowercase spelling of one closed artifact role. */
[[nodiscard]] std::string_view artifact_role_name(ArtifactRole role);

/** Stable lowercase spelling of one closed artifact kind. */
[[nodiscard]] std::string_view artifact_kind_name(ArtifactKind kind);

} // namespace volt::io
