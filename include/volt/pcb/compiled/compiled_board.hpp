#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volt/core/content_hash.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt {

/** Supported standalone CompiledBoard schema. */
enum class CompiledBoardSchemaVersion : std::uint32_t {
    V1 = 1,
};

/** Supported native Board compilation contract. */
enum class CompiledBoardCompilerVersion : std::uint32_t {
    V1 = 1,
};

/** Required closed capability input for one CompiledBoard revision. */
class CompiledBoardCapabilities {
  public:
    /** Build a concrete mandatory profile plus normalized optional asset capabilities. */
    CompiledBoardCapabilities(BoardCapabilityProfile profile,
                              std::vector<BoardAssetCapability> additional = {});

    /** Return the exact mandatory capability-profile snapshot. */
    [[nodiscard]] const BoardCapabilityProfile &profile() const noexcept { return profile_; }

    /** Return normalized optional asset capabilities. */
    [[nodiscard]] std::span<const BoardAssetCapability> additional() const noexcept {
        return additional_;
    }

    /** Return whether one optional asset capability is present. */
    [[nodiscard]] bool has(BoardAssetCapability capability) const noexcept;

  private:
    BoardCapabilityProfile profile_;
    std::vector<BoardAssetCapability> additional_;
};

/** Complete deterministic provenance for one CompiledBoard revision. */
class CompiledBoardProvenance {
  public:
    /** Record every canonical compile-input digest and the exact compiler contract. */
    CompiledBoardProvenance(CompiledBoardSchemaVersion schema_version,
                            CompiledBoardCompilerVersion compiler_version,
                            std::string compiler_name, std::string compiler_build,
                            ContentHash logical_dependency_digest,
                            ContentHash physical_snapshot_digest,
                            ContentHash selected_closure_digest, ContentHash capability_digest,
                            ContentHash provenance_digest);

    /** Return the CompiledBoard codec schema used by this revision. */
    [[nodiscard]] CompiledBoardSchemaVersion schema_version() const noexcept {
        return schema_version_;
    }

    /** Return the native compile contract used by this revision. */
    [[nodiscard]] CompiledBoardCompilerVersion compiler_version() const noexcept {
        return compiler_version_;
    }

    /** Return the stable compiler owner name. */
    [[nodiscard]] const std::string &compiler_name() const noexcept { return compiler_name_; }

    /** Return the compiler build provenance recorded by the artifact. */
    [[nodiscard]] const std::string &compiler_build() const noexcept { return compiler_build_; }

    /** Return the exact minimum logical dependency-snapshot digest. */
    [[nodiscard]] const ContentHash &logical_dependency_digest() const noexcept {
        return logical_dependency_digest_;
    }

    /** Return the exact canonical physical Board snapshot digest. */
    [[nodiscard]] const ContentHash &physical_snapshot_digest() const noexcept {
        return physical_snapshot_digest_;
    }

    /** Return the exact consumed selected part/asset closure digest. */
    [[nodiscard]] const ContentHash &selected_closure_digest() const noexcept {
        return selected_closure_digest_;
    }

    /** Return the exact required capability-record digest. */
    [[nodiscard]] const ContentHash &capability_digest() const noexcept {
        return capability_digest_;
    }

    /** Return the domain-separated digest over the complete compilation contract. */
    [[nodiscard]] const ContentHash &provenance_digest() const noexcept {
        return provenance_digest_;
    }

  private:
    CompiledBoardSchemaVersion schema_version_;
    CompiledBoardCompilerVersion compiler_version_;
    std::string compiler_name_;
    std::string compiler_build_;
    ContentHash logical_dependency_digest_;
    ContentHash physical_snapshot_digest_;
    ContentHash selected_closure_digest_;
    ContentHash capability_digest_;
    ContentHash provenance_digest_;
};

/** Stable identity of one historical CompiledBoard revision. */
class CompiledBoardIdentity {
  public:
    /** Pair one persisted Board name with its deterministic compilation provenance. */
    CompiledBoardIdentity(BoardName board, ContentHash provenance_digest);

    /** Return the exact Design-scoped source Board identity. */
    [[nodiscard]] const BoardName &board() const noexcept { return board_; }

    /** Return the deterministic digest of the complete compilation inputs and contract. */
    [[nodiscard]] const ContentHash &provenance_digest() const noexcept {
        return provenance_digest_;
    }

    /** Compare complete CompiledBoard identities. */
    [[nodiscard]] bool operator==(const CompiledBoardIdentity &other) const noexcept;

  private:
    BoardName board_;
    ContentHash provenance_digest_;
};

/** One typed structural compilation rejection with no partial artifact. */
class CompiledBoardFailure {
  public:
    /** Preserve the kernel failure family, message, and optional rejected entity. */
    CompiledBoardFailure(ErrorCode code, std::string message,
                         std::optional<EntityRef> entity = std::nullopt);

    /** Return the machine-readable structural failure family. */
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

    /** Return the human-readable structural failure message. */
    [[nodiscard]] const std::string &message() const noexcept { return message_; }

    /** Return the rejected entity when one was available. */
    [[nodiscard]] const std::optional<EntityRef> &entity() const noexcept { return entity_; }

  private:
    ErrorCode code_;
    std::string message_;
    std::optional<EntityRef> entity_;
};

/** Standalone immutable historical physical artifact for exactly one named Board. */
class CompiledBoard final {
  public:
    /** Owner codec defined by the Volt::IO public surface. */
    class Codec;

    /** Immutable artifacts cannot be copied. */
    CompiledBoard(const CompiledBoard &) = delete;

    /** Immutable artifacts cannot be copy-assigned. */
    CompiledBoard &operator=(const CompiledBoard &) = delete;

    /** Transfer complete owning artifact storage without invalidating its internal references. */
    CompiledBoard(CompiledBoard &&other) noexcept;

    /** Replace this artifact by transferring another complete owning storage block. */
    CompiledBoard &operator=(CompiledBoard &&other) noexcept;

    /** Destroy all artifact-owned Board, Circuit, closure, provenance, and byte storage. */
    ~CompiledBoard();

    /** Return the stable historical identity. */
    [[nodiscard]] const CompiledBoardIdentity &identity() const noexcept;

    /** Return the exact source Board name. */
    [[nodiscard]] const BoardName &board_name() const noexcept;

    /** Return complete compile-input and compiler provenance. */
    [[nodiscard]] const CompiledBoardProvenance &provenance() const noexcept;

    /** Return the explicit required capability snapshot. */
    [[nodiscard]] const CompiledBoardCapabilities &capabilities() const noexcept;

    /** Return the standalone resolved Board over artifact-owned logical storage. */
    [[nodiscard]] const Board &board() const noexcept;

    /** Return exact consumed footprint definitions owned by this revision. */
    [[nodiscard]] const FootprintLibrary &footprints() const noexcept;

    /** Return exact selected implementations in component order. */
    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const noexcept;

    /** Return the exact minimum logical dependency payload bytes. */
    [[nodiscard]] std::string_view logical_dependency_snapshot() const & noexcept;
    [[nodiscard]] std::string_view logical_dependency_snapshot() const && = delete;

    /** Return the exact canonical physical snapshot payload bytes. */
    [[nodiscard]] std::string_view physical_snapshot() const & noexcept;
    [[nodiscard]] std::string_view physical_snapshot() const && = delete;

    /** Return the exact canonical CompiledBoard artifact bytes. */
    [[nodiscard]] std::string_view bytes() const & noexcept;
    [[nodiscard]] std::string_view bytes() const && = delete;

    /** Return the digest of the exact canonical artifact bytes. */
    [[nodiscard]] const ContentHash &content_digest() const & noexcept;
    [[nodiscard]] const ContentHash &content_digest() const && = delete;

  private:
    class Storage;

    explicit CompiledBoard(std::unique_ptr<Storage> storage);

    [[nodiscard]] static CompiledBoard
    materialize_verified(Circuit logical_dependencies, const BoardResolution &resolution,
                         CompiledBoardCapabilities capabilities, CompiledBoardProvenance provenance,
                         std::string logical_dependency_snapshot, std::string physical_snapshot,
                         std::string bytes);

    std::unique_ptr<Storage> storage_;
};

/** Atomic compilation result containing either one complete artifact or one typed failure. */
class CompiledBoardCompileResult {
  public:
    /** Publish one complete artifact with deterministic design diagnostics. */
    [[nodiscard]] static CompiledBoardCompileResult success(CompiledBoard artifact,
                                                            DiagnosticReport diagnostics);

    /** Publish a structural failure and deterministic design evidence with no artifact. */
    [[nodiscard]] static CompiledBoardCompileResult failure(CompiledBoardFailure failure,
                                                            DiagnosticReport diagnostics);

    /** Return whether one complete CompiledBoard is available. */
    [[nodiscard]] bool has_artifact() const noexcept { return artifact_.has_value(); }

    /** Return the complete artifact, or null when compilation failed structurally. */
    [[nodiscard]] const CompiledBoard *artifact() const & noexcept;
    [[nodiscard]] const CompiledBoard *artifact() const && = delete;

    /** Move out the complete artifact, rejecting a failed result. */
    [[nodiscard]] CompiledBoard take_artifact() &&;

    /** Return typed design diagnostics retained on both success and failure. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

    /** Return the structural failure, or null when compilation succeeded. */
    [[nodiscard]] const std::optional<CompiledBoardFailure> &failure() const noexcept {
        return failure_;
    }

  private:
    CompiledBoardCompileResult(std::optional<CompiledBoard> artifact,
                               std::optional<CompiledBoardFailure> failure,
                               DiagnosticReport diagnostics);

    std::optional<CompiledBoard> artifact_;
    std::optional<CompiledBoardFailure> failure_;
    DiagnosticReport diagnostics_;
};

} // namespace volt
