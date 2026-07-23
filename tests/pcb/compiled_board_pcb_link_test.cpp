#include <string>
#include <utility>

#include <volt/pcb/compiled/compiled_board.hpp>

namespace {

[[nodiscard]] volt::BoardCapabilityProfile profile() {
    return volt::BoardCapabilityProfile{
        "PCB link contract",
        volt::BoardCapabilityProvenance{"Native fixture", "2026-07-23"},
        0.1,
        0.2,
        0.4,
        {}};
}

[[nodiscard]] int inspect_artifact(const volt::CompiledBoard &artifact) {
    return artifact.identity().board() == artifact.board_name() &&
                   artifact.provenance().provenance_digest() ==
                       artifact.identity().provenance_digest() &&
                   artifact.capabilities().profile().name() == profile().name() &&
                   artifact.board().name() == artifact.board_name() &&
                   artifact.footprints().definitions().size() <= artifact.parts().size() &&
                   !artifact.logical_dependency_snapshot().empty() &&
                   !artifact.physical_snapshot().empty() && !artifact.bytes().empty() &&
                   artifact.content_digest() == volt::sha256_content_hash(artifact.bytes())
               ? 0
               : 1;
}

[[nodiscard]] volt::CompiledBoard move_artifact(volt::CompiledBoard artifact) { return artifact; }

void assign_artifact(volt::CompiledBoard &target, volt::CompiledBoard source) {
    target = std::move(source);
}

[[nodiscard]] volt::CompiledBoardCompileResult successful_result(volt::CompiledBoard artifact) {
    return volt::CompiledBoardCompileResult::success(std::move(artifact), volt::DiagnosticReport{});
}

[[nodiscard]] volt::CompiledBoard take_result(volt::CompiledBoardCompileResult result) {
    return std::move(result).take_artifact();
}

} // namespace

int main() {
    static_cast<void>(&inspect_artifact);
    static_cast<void>(&move_artifact);
    static_cast<void>(&assign_artifact);
    static_cast<void>(&successful_result);
    static_cast<void>(&take_result);

    const auto logical = volt::sha256_content_hash("logical");
    const auto physical = volt::sha256_content_hash("physical");
    const auto closure = volt::sha256_content_hash("closure");
    const auto capability = volt::sha256_content_hash("capability");
    const auto provenance_digest = volt::sha256_content_hash("provenance");
    const auto capabilities = volt::CompiledBoardCapabilities{profile()};
    const auto provenance = volt::CompiledBoardProvenance{
        volt::CompiledBoardSchemaVersion::V1,
        volt::CompiledBoardCompilerVersion::V1,
        "link.compiler",
        "link.build",
        logical,
        physical,
        closure,
        capability,
        provenance_digest,
    };
    const auto identity =
        volt::CompiledBoardIdentity{volt::BoardName{"PCB link contract"}, provenance_digest};
    if (capabilities.profile().name() != profile().name() || !capabilities.additional().empty() ||
        capabilities.has(volt::BoardAssetCapability::Models3D) ||
        provenance.schema_version() != volt::CompiledBoardSchemaVersion::V1 ||
        provenance.compiler_version() != volt::CompiledBoardCompilerVersion::V1 ||
        provenance.compiler_name() != "link.compiler" ||
        provenance.compiler_build() != "link.build" ||
        provenance.logical_dependency_digest() != logical ||
        provenance.physical_snapshot_digest() != physical ||
        provenance.selected_closure_digest() != closure ||
        provenance.capability_digest() != capability ||
        provenance.provenance_digest() != provenance_digest ||
        identity.board().value() != "PCB link contract" ||
        identity.provenance_digest() != provenance_digest ||
        !(identity ==
          volt::CompiledBoardIdentity{volt::BoardName{"PCB link contract"}, provenance_digest})) {
        return 1;
    }

    const auto failure = volt::CompiledBoardCompileResult::failure(
        volt::CompiledBoardFailure{volt::ErrorCode::InvalidArgument, "rejected"},
        volt::DiagnosticReport{});
    return !failure.has_artifact() && failure.artifact() == nullptr &&
                   failure.failure().has_value() && failure.failure()->message() == "rejected" &&
                   failure.failure()->code() == volt::ErrorCode::InvalidArgument &&
                   !failure.failure()->entity().has_value() && failure.diagnostics().empty()
               ? 0
               : 1;
}
