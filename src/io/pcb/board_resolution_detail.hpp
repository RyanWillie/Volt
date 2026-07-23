#pragma once

#include <optional>
#include <string>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/parts/part_definition.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::io::detail {

[[nodiscard]] PartAssetReference footprint_asset_reference(const PartDefinition &part);

[[nodiscard]] PartAssetReference model_asset_reference(const PartModel3DReference &model);

void add_exact_footprint(FootprintLibrary &library, const FootprintDefinition &definition);

[[nodiscard]] ResolvedBoardPart materialize_resolved_part(const Circuit &circuit,
                                                          ComponentId component,
                                                          const PartDefinition &part,
                                                          const FootprintDefinition &footprint,
                                                          std::optional<std::string> model_bytes,
                                                          const ContentHash &implemented_component);

} // namespace volt::io::detail
