#include <volt/pcb/compiled/compiled_board.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

#include <volt/circuit/updates.hpp>

namespace volt {
namespace {

[[nodiscard]] bool same_capability_range(const std::optional<BoardCapabilityRange> &lhs,
                                         const std::optional<BoardCapabilityRange> &rhs) {
    return lhs.has_value() == rhs.has_value() &&
           (!lhs.has_value() ||
            (lhs->minimum_mm == rhs->minimum_mm && lhs->maximum_mm == rhs->maximum_mm));
}

[[nodiscard]] bool same_capability_profile(const BoardCapabilityProfile &lhs,
                                           const BoardCapabilityProfile &rhs) {
    const auto same_clearances =
        std::ranges::equal(lhs.minimum_clearances(), rhs.minimum_clearances(),
                           [](const BoardClearancePair &left, const BoardClearancePair &right) {
                               return left.first == right.first && left.second == right.second &&
                                      left.clearance_mm == right.clearance_mm;
                           });
    const auto same_refinements =
        std::ranges::equal(lhs.copper_weight_refinements(), rhs.copper_weight_refinements(),
                           [](const BoardCapabilityCopperWeightRefinement &left,
                              const BoardCapabilityCopperWeightRefinement &right) {
                               return left.copper_weight_oz == right.copper_weight_oz &&
                                      left.minimum_track_width_mm == right.minimum_track_width_mm &&
                                      left.minimum_clearance_mm == right.minimum_clearance_mm;
                           });
    return lhs.name() == rhs.name() && lhs.provenance().source == rhs.provenance().source &&
           lhs.provenance().as_of == rhs.provenance().as_of &&
           lhs.minimum_track_width_mm() == rhs.minimum_track_width_mm() &&
           lhs.minimum_via_drill_mm() == rhs.minimum_via_drill_mm() &&
           lhs.minimum_via_annular_mm() == rhs.minimum_via_annular_mm() && same_clearances &&
           same_refinements &&
           lhs.supported_copper_layer_counts() == rhs.supported_copper_layer_counts() &&
           same_capability_range(lhs.board_thickness_range_mm(), rhs.board_thickness_range_mm()) &&
           lhs.available_copper_weights_oz() == rhs.available_copper_weights_oz() &&
           same_capability_range(lhs.drill_diameter_range_mm(), rhs.drill_diameter_range_mm());
}

[[nodiscard]] Board copy_board_snapshot(const Board &source, const Circuit &circuit) {
    auto result = Board{circuit, source.name()};
    result.set_design_rules(source.design_rules());
    if (source.capability_profile().has_value()) {
        result.set_capability_profile(*source.capability_profile());
    }
    for (const auto &layer : source.all<BoardLayerId>()) {
        static_cast<void>(result.add_layer(layer));
    }
    if (source.layer_stack().has_value()) {
        result.set_layer_stack(*source.layer_stack());
    }
    if (source.outline().has_value()) {
        result.set_outline(*source.outline());
    }
    for (const auto &feature : source.all<BoardFeatureId>()) {
        static_cast<void>(result.add_feature(feature));
    }
    for (const auto &placement : source.all<ComponentPlacementId>()) {
        static_cast<void>(result.place_component(placement));
    }
    for (const auto &track : source.all<BoardTrackId>()) {
        static_cast<void>(result.add_track(track));
    }
    for (const auto &via : source.all<BoardViaId>()) {
        static_cast<void>(result.add_via(via));
    }
    for (const auto &zone : source.all<BoardZoneId>()) {
        static_cast<void>(result.add_zone(zone));
    }
    for (const auto &keepout : source.all<BoardKeepoutId>()) {
        static_cast<void>(result.add_keepout(keepout));
    }
    for (const auto &room : source.all<BoardRoomId>()) {
        static_cast<void>(result.add_room(room));
    }
    for (const auto &text : source.all<BoardTextId>()) {
        static_cast<void>(result.add_text(text));
    }
    return result;
}

[[nodiscard]] Circuit materialize_logical_dependencies(Circuit circuit,
                                                       std::span<const ResolvedBoardPart> parts) {
    for (const auto &part : parts) {
        circuit.update(part.component(),
                       SelectPhysicalPart{part.physical_part(), std::optional{part.reference()}});
    }
    return circuit;
}

} // namespace

class CompiledBoard::Storage {
  public:
    Storage(Circuit logical_dependencies, const BoardResolution &resolution,
            CompiledBoardCapabilities capabilities, CompiledBoardProvenance provenance,
            std::string logical_dependency_snapshot, std::string physical_snapshot,
            std::string bytes)
        : parts_{resolution.parts().begin(), resolution.parts().end()},
          circuit_{materialize_logical_dependencies(std::move(logical_dependencies), parts_)},
          board_{copy_board_snapshot(resolution.board(), circuit_)},
          footprints_{resolution.footprints()}, capabilities_{std::move(capabilities)},
          provenance_{std::move(provenance)},
          identity_{board_.name(), provenance_.provenance_digest()},
          logical_dependency_snapshot_{std::move(logical_dependency_snapshot)},
          physical_snapshot_{std::move(physical_snapshot)}, bytes_{std::move(bytes)},
          content_digest_{sha256_content_hash(bytes_)} {}

    std::vector<ResolvedBoardPart> parts_;
    Circuit circuit_;
    Board board_;
    FootprintLibrary footprints_;
    CompiledBoardCapabilities capabilities_;
    CompiledBoardProvenance provenance_;
    CompiledBoardIdentity identity_;
    std::string logical_dependency_snapshot_;
    std::string physical_snapshot_;
    std::string bytes_;
    ContentHash content_digest_;
};

CompiledBoardCapabilities::CompiledBoardCapabilities(BoardCapabilityProfile profile,
                                                     std::vector<BoardAssetCapability> additional)
    : profile_{std::move(profile)}, additional_{std::move(additional)} {
    std::ranges::sort(additional_);
    if (std::ranges::adjacent_find(additional_) != additional_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "CompiledBoard capabilities must be unique"};
    }
    for (const auto capability : additional_) {
        if (capability != BoardAssetCapability::Models3D) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "CompiledBoard capability is unsupported by schema v1"};
        }
    }
}

bool CompiledBoardCapabilities::has(BoardAssetCapability capability) const noexcept {
    return std::ranges::binary_search(additional_, capability);
}

CompiledBoardProvenance::CompiledBoardProvenance(
    CompiledBoardSchemaVersion schema_version, CompiledBoardCompilerVersion compiler_version,
    std::string compiler_name, std::string compiler_build, ContentHash logical_dependency_digest,
    ContentHash physical_snapshot_digest, ContentHash selected_closure_digest,
    ContentHash capability_digest, ContentHash provenance_digest)
    : schema_version_{schema_version}, compiler_version_{compiler_version},
      compiler_name_{std::move(compiler_name)}, compiler_build_{std::move(compiler_build)},
      logical_dependency_digest_{std::move(logical_dependency_digest)},
      physical_snapshot_digest_{std::move(physical_snapshot_digest)},
      selected_closure_digest_{std::move(selected_closure_digest)},
      capability_digest_{std::move(capability_digest)},
      provenance_digest_{std::move(provenance_digest)} {
    if (schema_version_ != CompiledBoardSchemaVersion::V1 ||
        compiler_version_ != CompiledBoardCompilerVersion::V1) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "CompiledBoard provenance version is unsupported"};
    }
    if (compiler_name_.empty() || compiler_build_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "CompiledBoard compiler provenance must not be empty"};
    }
}

CompiledBoardIdentity::CompiledBoardIdentity(BoardName board, ContentHash provenance_digest)
    : board_{std::move(board)}, provenance_digest_{std::move(provenance_digest)} {}

bool CompiledBoardIdentity::operator==(const CompiledBoardIdentity &other) const noexcept {
    return board_.value() == other.board_.value() && provenance_digest_ == other.provenance_digest_;
}

CompiledBoardFailure::CompiledBoardFailure(ErrorCode code, std::string message,
                                           std::optional<EntityRef> entity)
    : code_{code}, message_{std::move(message)}, entity_{entity} {
    if (message_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "CompiledBoard failure message must not be empty"};
    }
}

CompiledBoard::CompiledBoard(std::unique_ptr<Storage> storage) : storage_{std::move(storage)} {}

CompiledBoard::CompiledBoard(CompiledBoard &&other) noexcept = default;

CompiledBoard &CompiledBoard::operator=(CompiledBoard &&other) noexcept = default;

CompiledBoard::~CompiledBoard() = default;

CompiledBoard CompiledBoard::materialize_verified(
    Circuit logical_dependencies, const BoardResolution &resolution,
    CompiledBoardCapabilities capabilities, CompiledBoardProvenance provenance,
    std::string logical_dependency_snapshot, std::string physical_snapshot, std::string bytes) {
    if (resolution.board_name().value() != resolution.board().name().value()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "CompiledBoard resolution Board identity is inconsistent"};
    }
    if (resolution.closure_digest() != provenance.selected_closure_digest()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "CompiledBoard resolution closure digest is inconsistent"};
    }
    if (!resolution.capabilities().profile().has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "CompiledBoard requires one concrete capability profile"};
    }
    if (!same_capability_profile(*resolution.capabilities().profile(), capabilities.profile()) ||
        !std::ranges::equal(resolution.capabilities().additional(), capabilities.additional())) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "CompiledBoard capability snapshot differs from its resolution"};
    }
    if (bytes.empty() || logical_dependency_snapshot.empty() || physical_snapshot.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "CompiledBoard canonical payloads must not be empty"};
    }
    if (sha256_content_hash(logical_dependency_snapshot) !=
            provenance.logical_dependency_digest() ||
        sha256_content_hash(physical_snapshot) != provenance.physical_snapshot_digest()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "CompiledBoard canonical payload digest is inconsistent"};
    }
    return CompiledBoard{std::make_unique<Storage>(
        std::move(logical_dependencies), resolution, std::move(capabilities), std::move(provenance),
        std::move(logical_dependency_snapshot), std::move(physical_snapshot), std::move(bytes))};
}

const CompiledBoardIdentity &CompiledBoard::identity() const noexcept {
    return storage_->identity_;
}

const BoardName &CompiledBoard::board_name() const noexcept { return storage_->board_.name(); }

const CompiledBoardProvenance &CompiledBoard::provenance() const noexcept {
    return storage_->provenance_;
}

const CompiledBoardCapabilities &CompiledBoard::capabilities() const noexcept {
    return storage_->capabilities_;
}

const Board &CompiledBoard::board() const noexcept { return storage_->board_; }

const FootprintLibrary &CompiledBoard::footprints() const noexcept { return storage_->footprints_; }

std::span<const ResolvedBoardPart> CompiledBoard::parts() const noexcept {
    return storage_->parts_;
}

std::string_view CompiledBoard::logical_dependency_snapshot() const & noexcept {
    return storage_->logical_dependency_snapshot_;
}

std::string_view CompiledBoard::physical_snapshot() const & noexcept {
    return storage_->physical_snapshot_;
}

std::string_view CompiledBoard::bytes() const & noexcept { return storage_->bytes_; }

const ContentHash &CompiledBoard::content_digest() const & noexcept {
    return storage_->content_digest_;
}

CompiledBoardCompileResult::CompiledBoardCompileResult(std::optional<CompiledBoard> artifact,
                                                       std::optional<CompiledBoardFailure> failure,
                                                       DiagnosticReport diagnostics)
    : artifact_{std::move(artifact)}, failure_{std::move(failure)},
      diagnostics_{std::move(diagnostics)} {
    if (artifact_.has_value() == failure_.has_value()) {
        throw KernelLogicError{
            ErrorCode::InvalidState,
            "CompiledBoard result must contain exactly one artifact or structural failure"};
    }
}

CompiledBoardCompileResult CompiledBoardCompileResult::success(CompiledBoard artifact,
                                                               DiagnosticReport diagnostics) {
    return CompiledBoardCompileResult{std::optional<CompiledBoard>{std::move(artifact)},
                                      std::nullopt, std::move(diagnostics)};
}

CompiledBoardCompileResult CompiledBoardCompileResult::failure(CompiledBoardFailure failure,
                                                               DiagnosticReport diagnostics) {
    return CompiledBoardCompileResult{std::nullopt,
                                      std::optional<CompiledBoardFailure>{std::move(failure)},
                                      std::move(diagnostics)};
}

const CompiledBoard *CompiledBoardCompileResult::artifact() const & noexcept {
    return artifact_.has_value() ? &*artifact_ : nullptr;
}

CompiledBoard CompiledBoardCompileResult::take_artifact() && {
    if (!artifact_.has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Failed CompiledBoard result has no artifact"};
    }
    return std::move(*artifact_);
}

} // namespace volt
