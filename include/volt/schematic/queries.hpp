#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include <volt/schematic/schematic.hpp>

/** Read-only derived queries over kernel-owned schematic presentation data. */
namespace volt::queries {

/** Return a symbol definition by exact name, if present. */
[[nodiscard]] inline std::optional<SymbolDefId>
symbol_definition_by_name(const Schematic &schematic, std::string_view name) {
    for (std::size_t index = 0; index < schematic.all<SymbolDefId>().size(); ++index) {
        const auto id = SymbolDefId{index};
        if (schematic.get(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

/** Return a sheet by exact name, if present. */
[[nodiscard]] inline std::optional<SheetId> sheet_by_name(const Schematic &schematic,
                                                          std::string_view name) {
    for (std::size_t index = 0; index < schematic.all<SheetId>().size(); ++index) {
        const auto id = SheetId{index};
        if (schematic.get(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

/** Return a sheet-local authored region by exact name, if present. */
[[nodiscard]] inline std::optional<SheetRegionId>
sheet_region_by_name(const Schematic &schematic, SheetId sheet, std::string_view name) {
    const auto &regions = schematic.get(sheet).regions();
    for (std::size_t index = 0; index < schematic.all<SheetRegionId>().size(); ++index) {
        const auto id = SheetRegionId{index};
        const auto &candidate = schematic.get(id);
        for (const auto &region : regions) {
            if (&candidate == &region && candidate.name() == name) {
                return id;
            }
        }
    }
    return std::nullopt;
}

} // namespace volt::queries
