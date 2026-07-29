#include <volt/pcb/compiled/compiled_board.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include <volt/pcb/queries/board_queries.hpp>

namespace volt {
namespace {

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

[[nodiscard]] CompiledBoardPlacement
freeze_placement(const Board &board, const FootprintLibrary &footprints,
                 std::span<const ResolvedBoardPart> parts,
                 std::span<const PadResolution> pad_resolutions,
                 ComponentPlacementId placement_id) {
    const auto &placement = board.get(placement_id);
    const auto selected_part =
        std::ranges::find(parts, placement.component(), &ResolvedBoardPart::component);
    if (selected_part == parts.end()) {
        return CompiledBoardPlacement{
            placement_id, CompiledBoardPlacementStatus::MissingPart, std::nullopt, {}};
    }

    const auto &footprint = selected_part->physical_part().footprint();
    const auto board_definition = queries::footprint_definition_id(board, footprint);
    const auto *definition = board_definition.has_value() ? &board.get(board_definition.value())
                                                          : footprints.find(footprint);
    if (definition == nullptr) {
        return CompiledBoardPlacement{
            placement_id, CompiledBoardPlacementStatus::MissingFootprint, std::nullopt, {}};
    }

    auto placement_pads = std::vector<PadResolution>{};
    std::ranges::copy_if(pad_resolutions, std::back_inserter(placement_pads),
                         [placement_id](const PadResolution &resolution) {
                             return resolution.placement() == placement_id;
                         });
    return CompiledBoardPlacement{placement_id, CompiledBoardPlacementStatus::Resolved, *definition,
                                  std::move(placement_pads)};
}

[[nodiscard]] std::vector<CompiledBoardPlacement>
freeze_placements(const Board &board, const FootprintLibrary &footprints,
                  std::span<const ResolvedBoardPart> parts,
                  std::span<const PadResolution> pad_resolutions) {
    auto result = std::vector<CompiledBoardPlacement>{};
    result.reserve(board.all<ComponentPlacementId>().size());
    for (std::size_t index = 0; index < board.all<ComponentPlacementId>().size(); ++index) {
        result.push_back(freeze_placement(board, footprints, parts, pad_resolutions,
                                          ComponentPlacementId{index}));
    }
    return result;
}

} // namespace

CompiledBoardPlacement::CompiledBoardPlacement(ComponentPlacementId placement,
                                               CompiledBoardPlacementStatus status,
                                               std::optional<FootprintDefinition> footprint,
                                               std::vector<PadResolution> pad_resolutions)
    : placement_{placement}, status_{status}, footprint_{std::move(footprint)},
      pad_resolutions_{std::move(pad_resolutions)} {
    const auto valid_status = status_ == CompiledBoardPlacementStatus::Resolved ||
                              status_ == CompiledBoardPlacementStatus::MissingPart ||
                              status_ == CompiledBoardPlacementStatus::MissingFootprint;
    const auto resolved = status_ == CompiledBoardPlacementStatus::Resolved;
    if (!valid_status || resolved != footprint_.has_value() ||
        (!resolved && !pad_resolutions_.empty())) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "CompiledBoard placement status must match its frozen footprint and pad mapping"};
    }
}

class CompiledBoard::Storage {
  public:
    Storage(Circuit logical_dependencies, const Board &board, FootprintLibrary footprints,
            std::vector<ResolvedBoardPart> parts, CompiledBoardCapabilities capabilities,
            CompiledBoardProvenance provenance, std::string logical_dependency_snapshot,
            std::string physical_snapshot, std::string bytes)
        : parts_{std::move(parts)}, circuit_{std::move(logical_dependencies)},
          board_{copy_board_snapshot(board, circuit_)}, footprints_{std::move(footprints)},
          capabilities_{std::move(capabilities)}, provenance_{std::move(provenance)},
          identity_{board_.name(), provenance_.provenance_digest()},
          logical_dependency_snapshot_{std::move(logical_dependency_snapshot)},
          physical_snapshot_{std::move(physical_snapshot)}, bytes_{std::move(bytes)},
          content_digest_{sha256_content_hash(bytes_)} {}

    void finalize(const ResolvedBoardView &resolved) {
        pad_resolutions_ = queries::resolve_pads(resolved);
        placements_ = freeze_placements(board_, footprints_, parts_, pad_resolutions_);
    }

    std::vector<ResolvedBoardPart> parts_;
    Circuit circuit_;
    Board board_;
    FootprintLibrary footprints_;
    std::vector<PadResolution> pad_resolutions_;
    std::vector<CompiledBoardPlacement> placements_;
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
    Circuit logical_dependencies, const Board &board, ContentHash selected_closure_digest,
    FootprintLibrary footprints, std::vector<ResolvedBoardPart> parts,
    CompiledBoardCapabilities capabilities, CompiledBoardProvenance provenance,
    std::string logical_dependency_snapshot, std::string physical_snapshot, std::string bytes) {
    if (selected_closure_digest != provenance.selected_closure_digest()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "CompiledBoard resolution closure digest is inconsistent"};
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
    auto result = CompiledBoard{std::make_unique<Storage>(
        std::move(logical_dependencies), board, std::move(footprints), std::move(parts),
        std::move(capabilities), std::move(provenance), std::move(logical_dependency_snapshot),
        std::move(physical_snapshot), std::move(bytes))};
    result.storage_->finalize(result.view());
    return result;
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

const ResolvedBoardPart *CompiledBoard::part(ComponentId component) const noexcept {
    return view().part(component);
}

ResolvedBoardView CompiledBoard::view() const & { return ResolvedBoardView::from(*this); }

ResolvedBoardView ResolvedBoardView::from(const CompiledBoard &compiled) {
    return ResolvedBoardView{compiled.board(), compiled.footprints(), compiled.parts(), true};
}

std::span<const PadResolution> CompiledBoard::pad_resolutions() const noexcept {
    return storage_->pad_resolutions_;
}

std::span<const CompiledBoardPlacement> CompiledBoard::placements() const noexcept {
    return storage_->placements_;
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
