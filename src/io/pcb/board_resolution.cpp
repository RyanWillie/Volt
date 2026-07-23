#include <volt/io/pcb/board_resolution.hpp>

#include "board_resolution_detail.hpp"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/parts/footprint_asset.hpp>

namespace volt::io {
BoardResolution resolve_board(const Board &board, const PartLibraryBundle &selected_closure,
                              BoardResolutionCapabilities capabilities) {
    auto footprints = FootprintLibrary{};
    auto parts = std::vector<ResolvedBoardPart>{};

    for (std::size_t index = 0; index < board.circuit().all<ComponentId>().size(); ++index) {
        const auto component_id = ComponentId{index};
        const auto &instance = board.circuit().get(component_id);
        if (instance.selected_physical_part().has_value()) {
            throw KernelLogicError{
                ErrorCode::InvalidState,
                "Board resolution does not accept legacy PhysicalPart selections",
                EntityRef::component(component_id)};
        }
        if (!instance.selected_library_part_ref().has_value()) {
            continue;
        }

        const auto &reference = *instance.selected_library_part_ref();
        const auto &part = selected_closure.resolve(reference);
        const auto &component = board.circuit().get(instance.definition());

        const auto footprint_reference = detail::footprint_asset_reference(part);
        const auto footprint_bytes = selected_closure.asset(footprint_reference);
        if (!footprint_bytes.has_value()) {
            throw KernelRangeError{ErrorCode::UnknownEntity,
                                   "Selected exact footprint asset is absent from the closure",
                                   EntityRef::component(component_id)};
        }
        if (sha256_content_hash(*footprint_bytes) != footprint_reference.digest()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Selected exact footprint asset digest does not match bytes",
                                   EntityRef::component(component_id)};
        }
        const auto footprint = read_footprint_asset(*footprint_bytes);

        auto model_bytes = std::optional<std::string>{};
        const auto &model = part.orderable_part().model_3d();
        if (capabilities.has(BoardAssetCapability::Models3D) && model.has_value()) {
            const auto asset_reference = detail::model_asset_reference(*model);
            const auto asset = selected_closure.asset(asset_reference);
            if (!asset.has_value()) {
                throw KernelRangeError{ErrorCode::UnknownEntity,
                                       "Selected exact 3D model asset is absent from the closure",
                                       EntityRef::component(component_id)};
            }
            if (sha256_content_hash(*asset) != asset_reference.digest()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Selected exact 3D model digest does not match bytes",
                                       EntityRef::component(component_id)};
            }
            model_bytes = std::string{*asset};
        }

        detail::add_exact_footprint(footprints, footprint);
        parts.push_back(detail::materialize_resolved_part(board.circuit(), component_id, part,
                                                          footprint, std::move(model_bytes),
                                                          component.content_identity()));
    }

    std::ranges::sort(parts, [](const ResolvedBoardPart &lhs, const ResolvedBoardPart &rhs) {
        return lhs.component().index() < rhs.component().index();
    });
    return BoardResolution::materialize(board, selected_closure.digest(), std::move(capabilities),
                                        std::move(footprints), std::move(parts));
}

} // namespace volt::io
