#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <volt/io/project_bundle_v2_writer.hpp>

namespace volt::io {

namespace detail {
class ProjectBundleStorage;
}

/** Supported ProjectBundle manifest versions known to the public dispatch boundary. */
enum class ProjectBundleSchemaVersion : std::uint32_t {
    V1 = 1,
    V2 = 2,
};

/** Physical container carrying one ProjectBundle manifest and its declared payloads. */
enum class ProjectBundleStorageKind {
    Directory,
    ZipArchive,
};

/** Truthful integrity state published after one complete bundle open. */
enum class BundleIntegrityStatus {
    LegacyUnverified,
    VerifiedV2,
};

/** Stable structural failure family for the fail-closed ProjectBundle open boundary. */
enum class ProjectBundleOpenErrorCode {
    MissingBundle,
    UnsupportedStorage,
    UnsafePath,
    MalformedArchive,
    MalformedManifest,
    UnsupportedFormat,
    UnsupportedSchema,
    MissingEntry,
    InputTooLarge,
    DuplicateIdentity,
    DigestMismatch,
    OwnershipViolation,
    ModelDecodeFailure,
};

/** One typed structural ProjectBundle rejection with no partially published owner. */
class ProjectBundleOpenError final : public std::runtime_error {
  public:
    /** Record the stable structural failure family and human-readable evidence. */
    ProjectBundleOpenError(ProjectBundleOpenErrorCode code, std::string message);

    /** Return the machine-readable structural failure family. */
    [[nodiscard]] ProjectBundleOpenErrorCode code() const noexcept { return code_; }

  private:
    ProjectBundleOpenErrorCode code_;
};

/** V2-only meaning that a truthful legacy v1 bundle cannot provide. */
enum class ProjectBundleV2Meaning {
    ArtifactGraph,
    ArtifactRoles,
    ArtifactKinds,
    DependencyEdges,
    DependencyLock,
    BuildId,
    AuthoringInputsDigest,
    BundleDigest,
    CompleteArtifactDigests,
    CompiledBoards,
    BoardScenes,
    SelectedClosure,
    CapabilityRecords,
    VerifiedProvenance,
};

/** Availability result for one explicitly requested ProjectBundle meaning. */
enum class ProjectBundleMeaningAvailability {
    Available,
    UnavailableInV1,
};

/** Typed rejection for an operation whose required meaning is absent from schema v1. */
class ProjectBundleUnavailableError final : public std::logic_error {
  public:
    /** Record the unavailable meaning and the schema that truthfully lacks it. */
    ProjectBundleUnavailableError(ProjectBundleSchemaVersion schema,
                                  ProjectBundleV2Meaning meaning);

    /** Return the opened bundle schema. */
    [[nodiscard]] ProjectBundleSchemaVersion schema_version() const noexcept { return schema_; }

    /** Return the exact unavailable meaning requested by the caller. */
    [[nodiscard]] ProjectBundleV2Meaning meaning() const noexcept { return meaning_; }

  private:
    ProjectBundleSchemaVersion schema_;
    ProjectBundleV2Meaning meaning_;
};

/** Immutable lease over one exact artifact record and its captured v1 bytes. */
class LegacyProjectBundleV1ArtifactView final {
  public:
    /// @cond
    LegacyProjectBundleV1ArtifactView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                                      std::size_t index);
    /// @endcond

    /** Return the legacy kind exactly recorded by the v1 manifest. */
    [[nodiscard]] std::string kind() const;

    /** Return the display name exactly recorded by the v1 manifest. */
    [[nodiscard]] std::string name() const;

    /** Return the accepted legacy-safe relative path. */
    [[nodiscard]] std::string path() const;

    /** Return the media type exactly recorded by the v1 manifest. */
    [[nodiscard]] std::string media_type() const;

    /** Return one recorded legacy group field without interpreting it as v2 identity. */
    [[nodiscard]] std::optional<std::string> group_value(const std::string &key) const;

    /** Return the bare lowercase digest recorded by v1, when one was present and verified. */
    [[nodiscard]] std::optional<std::string> recorded_sha256() const;

    /** Return a JSON copy of the complete actual v1 manifest record. */
    [[nodiscard]] std::string manifest_record_json() const;

    /** Return a safe owning copy of the exact captured artifact bytes. */
    [[nodiscard]] std::string bytes() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Read-only identity and byte lease for one decoded v1 logical Circuit document. */
class LegacyProjectBundleV1LogicalDocumentView final {
  public:
    /// @cond
    LegacyProjectBundleV1LogicalDocumentView(
        std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index);
    /// @endcond

    /** Return the exact v1 design group that owns this decoded Circuit. */
    [[nodiscard]] std::string design() const;

    /** Return the manifest artifact carrying the decoded Circuit document. */
    [[nodiscard]] LegacyProjectBundleV1ArtifactView artifact() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Read-only identity and byte lease for one decoded v1 Schematic document. */
class LegacyProjectBundleV1SchematicView final {
  public:
    /// @cond
    LegacyProjectBundleV1SchematicView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                                       std::size_t index);
    /// @endcond

    /** Return the exact v1 logical design group this Schematic projects. */
    [[nodiscard]] std::string design() const;

    /** Return the exact v1 Schematic group identity. */
    [[nodiscard]] std::string schematic() const;

    /** Return the manifest artifact carrying the decoded Schematic document. */
    [[nodiscard]] LegacyProjectBundleV1ArtifactView artifact() const;

    /** Return the retained logical owner lease used by the native Schematic codec. */
    [[nodiscard]] LegacyProjectBundleV1LogicalDocumentView circuit() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Read-only identity and byte lease for one decoded v1 Board document. */
class LegacyProjectBundleV1BoardView final {
  public:
    /// @cond
    LegacyProjectBundleV1BoardView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                                   std::size_t index);
    /// @endcond

    /** Return the exact v1 logical design group this Board implements. */
    [[nodiscard]] std::string design() const;

    /** Return the exact persisted BoardName verified by the native Board codec. */
    [[nodiscard]] std::string board() const;

    /** Return the manifest artifact carrying the decoded Board document. */
    [[nodiscard]] LegacyProjectBundleV1ArtifactView artifact() const;

    /** Return the retained logical owner lease used by the native Board codec. */
    [[nodiscard]] LegacyProjectBundleV1LogicalDocumentView circuit() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Explicit truthful read-only view of one completely reopened legacy v1 bundle. */
class LegacyProjectBundleV1View final {
  public:
    /// @cond
    explicit LegacyProjectBundleV1View(std::shared_ptr<const detail::ProjectBundleStorage> storage);

    /// @endcond

    /** Return the immutable legacy trust state; digest coverage never upgrades it. */
    [[nodiscard]] BundleIntegrityStatus integrity_status() const noexcept {
        return BundleIntegrityStatus::LegacyUnverified;
    }

    /** Return the exact project name recorded by the v1 manifest. */
    [[nodiscard]] std::string project_name() const;

    /** Return the optional project version recorded by the v1 manifest. */
    [[nodiscard]] std::optional<std::string> project_version() const;

    /** Return the optional project description recorded by the v1 manifest. */
    [[nodiscard]] std::optional<std::string> project_description() const;

    /** Return a safe owning copy of the exact captured manifest bytes. */
    [[nodiscard]] std::string manifest_bytes() const;

    /** Enumerate every actual v1 artifact record in deterministic path order. */
    [[nodiscard]] std::vector<LegacyProjectBundleV1ArtifactView> artifacts() const;

    /** Enumerate all decoded logical Circuit documents in deterministic identity order. */
    [[nodiscard]] std::vector<LegacyProjectBundleV1LogicalDocumentView> circuits() const;

    /** Enumerate all decoded Schematic documents in deterministic identity order. */
    [[nodiscard]] std::vector<LegacyProjectBundleV1SchematicView> schematics() const;

    /** Enumerate all decoded Board documents in deterministic identity order. */
    [[nodiscard]] std::vector<LegacyProjectBundleV1BoardView> boards() const;

    /** Report that a v2-only meaning is truthfully absent from this legacy view. */
    [[nodiscard]] ProjectBundleMeaningAvailability
    availability(ProjectBundleV2Meaning meaning) const noexcept;

    /** Throw the typed unavailable-in-v1 result for an operation requiring v2 meaning. */
    [[noreturn]] void require(ProjectBundleV2Meaning meaning) const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

/** Immutable lease over one fully verified v2 artifact and its captured payload bytes. */
class ProjectBundleV2ArtifactView final {
  public:
    /// @cond
    ProjectBundleV2ArtifactView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                                std::size_t index);
    /// @endcond

    /** Return the complete typed descriptor verified by the native opener. */
    [[nodiscard]] const ArtifactDescriptor &descriptor() const &;
    [[nodiscard]] const ArtifactDescriptor &descriptor() const && = delete;

    /** Return the canonical JSON spelling of the exact typed ArtifactId. */
    [[nodiscard]] std::string id_json() const;

    /** Return a JSON copy of the exact verified manifest record. */
    [[nodiscard]] std::string manifest_record_json() const;

    /** Return an owning copy of the exact immutable payload bytes. */
    [[nodiscard]] std::string bytes() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Immutable lease over one reopened authoritative logical Circuit. */
class LoadedLogicalModelView final {
  public:
    /// @cond
    LoadedLogicalModelView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                           std::size_t index);
    /// @endcond

    /** Return the exact logical design owner. */
    [[nodiscard]] const DesignKey &design() const &;
    [[nodiscard]] const DesignKey &design() const && = delete;
    /** Return the verified artifact carrying this logical model. */
    [[nodiscard]] ProjectBundleV2ArtifactView artifact() const;
    /** Return the immutable reopened Circuit. */
    [[nodiscard]] const Circuit &model() const &;
    [[nodiscard]] const Circuit &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Immutable lease over one reopened Schematic and its retained logical owner. */
class LoadedSchematicView final {
  public:
    /// @cond
    LoadedSchematicView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                        std::size_t index);
    /// @endcond

    /** Return the exact logical design owner. */
    [[nodiscard]] const DesignKey &design() const &;
    [[nodiscard]] const DesignKey &design() const && = delete;
    /** Return the project-local Schematic identity. */
    [[nodiscard]] const SchematicKey &schematic() const &;
    [[nodiscard]] const SchematicKey &schematic() const && = delete;
    /** Return the verified artifact carrying this Schematic. */
    [[nodiscard]] ProjectBundleV2ArtifactView artifact() const;
    /** Return the retained logical Circuit owner. */
    [[nodiscard]] LoadedLogicalModelView circuit() const;
    /** Return the immutable reopened Schematic. */
    [[nodiscard]] const Schematic &model() const &;
    [[nodiscard]] const Schematic &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Immutable lease over one reopened named authoring Board and its logical owner. */
class LoadedBoardView final {
  public:
    /// @cond
    LoadedBoardView(std::shared_ptr<const detail::ProjectBundleStorage> storage, std::size_t index);
    /// @endcond

    /** Return the exact logical design owner. */
    [[nodiscard]] const DesignKey &design() const &;
    [[nodiscard]] const DesignKey &design() const && = delete;
    /** Return the independent named Board identity. */
    [[nodiscard]] const BoardName &board() const &;
    [[nodiscard]] const BoardName &board() const && = delete;
    /** Return the verified artifact carrying this authoring Board. */
    [[nodiscard]] ProjectBundleV2ArtifactView artifact() const;
    /** Return the retained logical Circuit owner. */
    [[nodiscard]] LoadedLogicalModelView circuit() const;
    /** Return the immutable reopened authoring Board. */
    [[nodiscard]] const Board &model() const &;
    [[nodiscard]] const Board &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Immutable lease over one independently owned historical CompiledBoard. */
class LoadedCompiledBoardView final {
  public:
    /// @cond
    LoadedCompiledBoardView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                            std::size_t index);
    /// @endcond

    /** Return the exact immutable compilation identity. */
    [[nodiscard]] const CompiledBoardIdentity &identity() const &;
    [[nodiscard]] const CompiledBoardIdentity &identity() const && = delete;
    /** Return the verified artifact carrying this CompiledBoard. */
    [[nodiscard]] ProjectBundleV2ArtifactView artifact() const;
    /** Return the independently owned immutable historical CompiledBoard. */
    [[nodiscard]] const CompiledBoard &model() const &;
    [[nodiscard]] const CompiledBoard &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Immutable lease over one exact CompiledBoard-derived BoardScene. */
class LoadedBoardSceneView final {
  public:
    /// @cond
    LoadedBoardSceneView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
                         std::size_t index);
    /// @endcond

    /** Return the verified artifact carrying this BoardScene. */
    [[nodiscard]] ProjectBundleV2ArtifactView artifact() const;
    /** Return the exact retained CompiledBoard owner. */
    [[nodiscard]] LoadedCompiledBoardView compiled_board() const;
    /** Return the immutable reopened BoardScene. */
    [[nodiscard]] const BoardScene &model() const &;
    [[nodiscard]] const BoardScene &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Narrow immutable typed project model loaded from one completely verified v2 graph. */
class LoadedProject final {
  public:
    /// @cond
    explicit LoadedProject(std::shared_ptr<const detail::ProjectBundleStorage> storage);
    /// @endcond

    /** Enumerate logical Circuits in deterministic typed-identity order. */
    [[nodiscard]] std::vector<LoadedLogicalModelView> circuits() const;
    /** Enumerate Schematics in deterministic typed-identity order. */
    [[nodiscard]] std::vector<LoadedSchematicView> schematics() const;
    /** Enumerate independent named authoring Boards in deterministic identity order. */
    [[nodiscard]] std::vector<LoadedBoardView> boards() const;
    /** Enumerate independently owned CompiledBoards in deterministic identity order. */
    [[nodiscard]] std::vector<LoadedCompiledBoardView> compiled_boards() const;
    /** Enumerate BoardScenes in deterministic CompiledBoard-owner order. */
    [[nodiscard]] std::vector<LoadedBoardSceneView> board_scenes() const;
    /** Return the singleton verified diagnostics report. */
    [[nodiscard]] ProjectBundleV2ArtifactView diagnostics() const;
    /** Return the singleton verified project-test report. */
    [[nodiscard]] ProjectBundleV2ArtifactView tests() const;
    /** Enumerate selected export outputs in canonical request identity order. */
    [[nodiscard]] std::vector<ProjectBundleV2ArtifactView> selected_exports() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

/** Fully verified immutable typed v2 graph over ProjectBundle-owned storage. */
class ProjectBundleGraphV2View final {
  public:
    /// @cond
    explicit ProjectBundleGraphV2View(std::shared_ptr<const detail::ProjectBundleStorage> storage);

    /// @endcond

    /** Return the verified v2 trust state. */
    [[nodiscard]] BundleIntegrityStatus integrity_status() const noexcept {
        return BundleIntegrityStatus::VerifiedV2;
    }

    /** Return the verified human project name. */
    [[nodiscard]] std::string project_name() const;
    /** Return the optional verified human project version. */
    [[nodiscard]] std::optional<std::string> project_version() const;
    /** Return the optional verified human project description. */
    [[nodiscard]] std::optional<std::string> project_description() const;
    /** Return an owning copy of the exact canonical manifest bytes. */
    [[nodiscard]] std::string manifest_bytes() const;
    /** Return the recomputed required-default build identity. */
    [[nodiscard]] const BuildId &build_id() const &;
    [[nodiscard]] const BuildId &build_id() const && = delete;
    /** Return the recomputed complete bundle digest. */
    [[nodiscard]] const ContentHash &bundle_digest() const &;
    [[nodiscard]] const ContentHash &bundle_digest() const && = delete;
    /** Return the recomputed authoring-input evidence digest. */
    [[nodiscard]] const ContentHash &authoring_inputs_digest() const &;
    [[nodiscard]] const ContentHash &authoring_inputs_digest() const && = delete;
    /** Return the exact verified offline dependency lock. */
    [[nodiscard]] const DependencyLock &dependency_lock() const &;
    [[nodiscard]] const DependencyLock &dependency_lock() const && = delete;
    /** Return the exact verified opt-in export requests. */
    [[nodiscard]] const ExportSelection &export_selection() const &;
    [[nodiscard]] const ExportSelection &export_selection() const && = delete;
    /** Enumerate all verified artifacts in deterministic typed-identity order. */
    [[nodiscard]] std::vector<ProjectBundleV2ArtifactView> artifacts() const;
    /** Look up one exact typed artifact identity. */
    [[nodiscard]] std::optional<ProjectBundleV2ArtifactView> artifact(const ArtifactId &id) const;
    /** Return the fully decoded immutable project view. */
    [[nodiscard]] LoadedProject loaded_project() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

/** Explicit version-discriminated read-only ProjectBundle contents. */
using ProjectBundleContentsView = std::variant<LegacyProjectBundleV1View, ProjectBundleGraphV2View>;

/** Move-only owner of one fully captured, validated, and decoded ProjectBundle. */
class ProjectBundle final {
  public:
    /** Open a v1/v2 directory or ZIP archive without executing source or publishing partial state.
     */
    [[nodiscard]] static ProjectBundle open(const std::filesystem::path &path);

    /** Owning bundles cannot be copied. */
    ProjectBundle(const ProjectBundle &) = delete;

    /** Owning bundles cannot be copy-assigned. */
    ProjectBundle &operator=(const ProjectBundle &) = delete;

    /** Transfer the complete storage owner without invalidating retained document leases. */
    ProjectBundle(ProjectBundle &&other) noexcept;

    /** Replace this owner by transferring another complete storage owner. */
    ProjectBundle &operator=(ProjectBundle &&other) noexcept;

    /** Release the bundle's ownership share and its immutable decoded storage. */
    ~ProjectBundle();

    /** Return the explicitly dispatched manifest schema. */
    [[nodiscard]] ProjectBundleSchemaVersion schema_version() const noexcept;

    /** Return the physical storage kind opened by this owner. */
    [[nodiscard]] ProjectBundleStorageKind storage_kind() const noexcept;

    /** Return the truthful integrity state for the dispatched schema. */
    [[nodiscard]] BundleIntegrityStatus integrity_status() const noexcept;

    /** Return the explicit versioned contents view. */
    [[nodiscard]] ProjectBundleContentsView view() const;

    /** Return the truthful legacy view, or reject a non-v1 bundle. */
    [[nodiscard]] LegacyProjectBundleV1View legacy_v1() const;

    /** Return the verified v2 view or throw typed unavailable-in-v1. */
    [[nodiscard]] ProjectBundleGraphV2View require_v2() const;

  private:
    explicit ProjectBundle(std::shared_ptr<const detail::ProjectBundleStorage> storage);

    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

} // namespace volt::io
