#include <volt/pcb/resolution/board_resolution.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

#include <volt/core/errors.hpp>

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

void require_capability_profile(const Board &board,
                                const BoardResolutionCapabilities &capabilities) {
    if (board.capability_profile().has_value() != capabilities.profile().has_value()) {
        throw KernelLogicError{
            ErrorCode::CrossReferenceViolation,
            "Board resolution capability profile does not match the named Board"};
    }
    if (board.capability_profile().has_value() &&
        !same_capability_profile(*board.capability_profile(), *capabilities.profile())) {
        throw KernelLogicError{
            ErrorCode::CrossReferenceViolation,
            "Board resolution capability profile differs from the named Board snapshot"};
    }
}

void require_resolved_view_inputs(const Board &board, const FootprintLibrary &footprints,
                                  std::span<const ResolvedBoardPart> parts) {
    for (std::size_t index = 1; index < parts.size(); ++index) {
        if (parts[index - 1U].component().index() >= parts[index].component().index()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Resolved Board parts must use unique ascending component order"};
        }
    }

    for (const auto &part : parts) {
        const auto component = part.component();
        if (component.index() >= board.circuit().all<ComponentId>().size()) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Resolved Board part references a component outside the authoring Circuit",
                EntityRef::component(component)};
        }
        const auto &instance = board.circuit().get(component);
        if (!instance.selected_library_part_ref().has_value() ||
            part.reference() != *instance.selected_library_part_ref()) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Resolved Board part reference differs from authoring Circuit selection",
                EntityRef::component(component)};
        }
    }

    for (std::size_t index = 0; index < board.all<FootprintDefId>().size(); ++index) {
        const auto &definition = board.get(FootprintDefId{index});
        const auto *resolved = footprints.find(definition.ref());
        if (resolved != nullptr && *resolved != definition) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Board footprint definition differs from its exact selected closure"};
        }
    }
}

void require_complete_resolved_inputs(const Board &board, const FootprintLibrary &footprints,
                                      std::span<const ResolvedBoardPart> parts) {
    require_resolved_view_inputs(board, footprints, parts);

    for (std::size_t index = 0; index < board.all<FootprintDefId>().size(); ++index) {
        const auto &definition = board.get(FootprintDefId{index});
        const auto *resolved = footprints.find(definition.ref());
        if (resolved == nullptr || *resolved != definition) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Board footprint definition differs from its exact selected closure"};
        }
    }

    auto resolved_index = std::size_t{0};
    for (std::size_t index = 0; index < board.circuit().all<ComponentId>().size(); ++index) {
        const auto component = ComponentId{index};
        const auto &instance = board.circuit().get(component);
        if (!instance.selected_library_part_ref().has_value()) {
            continue;
        }
        if (resolved_index == parts.size() || parts[resolved_index].component() != component) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Resolved Board parts do not exactly cover selected library references",
                EntityRef::component(component)};
        }

        const auto &part = parts[resolved_index];
        const auto &definition = board.circuit().get(instance.definition());
        const auto *footprint = footprints.find(part.physical_part().footprint());
        if (footprint == nullptr) {
            throw KernelRangeError{
                ErrorCode::UnknownEntity,
                "Resolved Board part footprint is absent from materialized definitions",
                EntityRef::component(component)};
        }
        for (const auto &mapping : part.physical_part().pin_pad_mappings()) {
            if (std::ranges::find(definition.pins(), mapping.pin()) == definition.pins().end()) {
                throw KernelLogicError{
                    ErrorCode::CrossReferenceViolation,
                    "Resolved Board part maps a pin outside the selected component definition",
                    EntityRef::pin_def(mapping.pin())};
            }
            const auto pad = footprint->pad_id(mapping.pad());
            if (!pad.has_value() || !footprint->pad(*pad).requires_pin_mapping()) {
                throw KernelLogicError{
                    ErrorCode::CrossReferenceViolation,
                    "Resolved Board part maps a logical pin to an invalid footprint pad",
                    EntityRef::component(component)};
            }
        }
        for (const auto pin : definition.pins()) {
            const auto mapped = std::ranges::any_of(
                part.physical_part().pin_pad_mappings(),
                [pin](const PinPadMapping &mapping) { return mapping.pin() == pin; });
            if (!mapped) {
                throw KernelLogicError{
                    ErrorCode::CrossReferenceViolation,
                    "Resolved Board part does not map every component-definition pin",
                    EntityRef::pin_def(pin)};
            }
        }
        ++resolved_index;
    }

    for (const auto &definition : footprints.definitions()) {
        const auto used = std::ranges::any_of(parts, [&](const ResolvedBoardPart &part) {
            return part.physical_part().footprint() == definition.ref();
        });
        if (!used) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Resolved Board footprint definitions contain an unselected asset"};
        }
    }
}

void require_materialization_inputs(const Board &board,
                                    const BoardResolutionCapabilities &capabilities,
                                    const FootprintLibrary &footprints,
                                    const std::vector<ResolvedBoardPart> &parts) {
    require_capability_profile(board, capabilities);
    require_complete_resolved_inputs(board, footprints, parts);

    for (const auto &part : parts) {
        const auto expects_model_bytes = capabilities.has(BoardAssetCapability::Models3D) &&
                                         part.physical_part().model_3d().has_value();
        if (part.model_3d_bytes().has_value() != expects_model_bytes) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Resolved Board part 3D bytes do not match the requested capability closure",
                EntityRef::component(part.component())};
        }
    }
}

} // namespace

BoardResolutionCapabilities::BoardResolutionCapabilities(
    std::optional<BoardCapabilityProfile> profile, std::vector<BoardAssetCapability> additional)
    : profile_{std::move(profile)}, additional_{std::move(additional)} {
    std::ranges::sort(additional_);
    if (std::ranges::adjacent_find(additional_) != additional_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Board resolution capabilities must be unique"};
    }
}

bool BoardResolutionCapabilities::has(BoardAssetCapability capability) const noexcept {
    return std::ranges::binary_search(additional_, capability);
}

ResolvedBoardPart::ResolvedBoardPart(ComponentId component, LibraryPartRef reference,
                                     PhysicalPart physical_part,
                                     std::optional<std::string> model_3d_bytes)
    : component_{component}, reference_{std::move(reference)},
      physical_part_{std::move(physical_part)}, model_3d_bytes_{std::move(model_3d_bytes)} {}

ResolvedBoardView::ResolvedBoardView(const Board &board, const FootprintLibrary &footprints,
                                     std::span<const ResolvedBoardPart> parts, bool complete)
    : board_{&board}, footprints_{&footprints}, parts_{parts}, complete_{complete} {
    require_current();
}

ResolvedBoardView ResolvedBoardView::from(const BoardResolution &resolution) {
    return ResolvedBoardView{resolution.board(), resolution.footprints(), resolution.parts(), true};
}

void ResolvedBoardView::require_current() const {
    if (complete_) {
        require_complete_resolved_inputs(*board_, *footprints_, parts_);
        return;
    }
    require_resolved_view_inputs(*board_, *footprints_, parts_);
}

const Board &ResolvedBoardView::board() const {
    require_current();
    return *board_;
}

const FootprintLibrary &ResolvedBoardView::footprints() const {
    require_current();
    return *footprints_;
}

std::span<const ResolvedBoardPart> ResolvedBoardView::parts() const {
    require_current();
    return parts_;
}

const ResolvedBoardPart *ResolvedBoardView::part(ComponentId component) const {
    require_current();
    const auto match = std::ranges::find(parts_, component, &ResolvedBoardPart::component);
    return match != parts_.end() ? &*match : nullptr;
}

BoardResolution BoardResolution::materialize_verified(const Board &board,
                                                      ContentHash closure_digest,
                                                      BoardResolutionCapabilities capabilities,
                                                      FootprintLibrary footprints,
                                                      std::vector<ResolvedBoardPart> parts) {
    require_materialization_inputs(board, capabilities, footprints, parts);
    return BoardResolution{board, std::move(closure_digest), std::move(capabilities),
                           std::move(footprints), std::move(parts)};
}

BoardResolution::BoardResolution(const Board &board, ContentHash closure_digest,
                                 BoardResolutionCapabilities capabilities,
                                 FootprintLibrary footprints, std::vector<ResolvedBoardPart> parts)
    : board_{&board}, board_name_{board.name()}, closure_digest_{std::move(closure_digest)},
      capabilities_{std::move(capabilities)}, footprints_{std::move(footprints)},
      parts_{std::move(parts)} {}

const Board &BoardResolution::board() const {
    require_materialization_inputs(*board_, capabilities_, footprints_, parts_);
    return *board_;
}

const FootprintLibrary &BoardResolution::footprints() const {
    require_materialization_inputs(*board_, capabilities_, footprints_, parts_);
    return footprints_;
}

std::span<const ResolvedBoardPart> BoardResolution::parts() const {
    require_materialization_inputs(*board_, capabilities_, footprints_, parts_);
    return parts_;
}

const ResolvedBoardPart *BoardResolution::part(ComponentId component) const {
    return view().part(component);
}

ResolvedBoardView BoardResolution::view() const & { return ResolvedBoardView::from(*this); }

} // namespace volt
