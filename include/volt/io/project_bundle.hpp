#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/io/project_bundle_writer.hpp>

namespace volt::io {

namespace detail {
class ProjectBundleStorage;
}

/** Physical container carrying one ProjectBundle manifest and its declared payloads. */
enum class ProjectBundleStorageKind {
    Directory,
    ZipArchive,
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

/** Immutable lease over one fully verified artifact and its captured payload bytes. */
class ProjectBundleArtifactView final {
  public:
    /// @cond
    ProjectBundleArtifactView(std::shared_ptr<const detail::ProjectBundleStorage> storage,
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
    [[nodiscard]] ProjectBundleArtifactView artifact() const;
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
    [[nodiscard]] ProjectBundleArtifactView artifact() const;
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
    [[nodiscard]] ProjectBundleArtifactView artifact() const;
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
    [[nodiscard]] ProjectBundleArtifactView artifact() const;
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
    [[nodiscard]] ProjectBundleArtifactView artifact() const;
    /** Return the exact retained CompiledBoard owner. */
    [[nodiscard]] LoadedCompiledBoardView compiled_board() const;
    /** Return the immutable reopened BoardScene. */
    [[nodiscard]] const BoardScene &model() const &;
    [[nodiscard]] const BoardScene &model() const && = delete;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
    std::size_t index_;
};

/** Narrow immutable typed project model loaded from one completely verified graph. */
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
    [[nodiscard]] ProjectBundleArtifactView diagnostics() const;
    /** Return the singleton verified project-test report. */
    [[nodiscard]] ProjectBundleArtifactView tests() const;
    /** Enumerate selected export outputs in canonical request identity order. */
    [[nodiscard]] std::vector<ProjectBundleArtifactView> selected_exports() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

/** Fully verified immutable typed graph over ProjectBundle-owned storage. */
class ProjectBundleGraphView final {
  public:
    /// @cond
    explicit ProjectBundleGraphView(std::shared_ptr<const detail::ProjectBundleStorage> storage);

    /// @endcond

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
    [[nodiscard]] std::vector<ProjectBundleArtifactView> artifacts() const;
    /** Look up one exact typed artifact identity. */
    [[nodiscard]] std::optional<ProjectBundleArtifactView> artifact(const ArtifactId &id) const;
    /** Return the fully decoded immutable project view. */
    [[nodiscard]] LoadedProject loaded_project() const;

  private:
    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

/** Move-only owner of one fully captured, validated, and decoded ProjectBundle. */
class ProjectBundle final {
  public:
    /** Open a current-schema directory or ZIP archive without executing source. */
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

    /** Return the verified manifest schema. */
    [[nodiscard]] ProjectBundleSchemaVersion schema_version() const noexcept;

    /** Return the physical storage kind opened by this owner. */
    [[nodiscard]] ProjectBundleStorageKind storage_kind() const noexcept;

    /** Return the verified current artifact graph. */
    [[nodiscard]] ProjectBundleGraphView graph() const;

  private:
    explicit ProjectBundle(std::shared_ptr<const detail::ProjectBundleStorage> storage);

    std::shared_ptr<const detail::ProjectBundleStorage> storage_;
};

} // namespace volt::io
