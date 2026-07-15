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
    for (std::size_t index = 0; index < schematic.symbol_definition_count(); ++index) {
        const auto id = SymbolDefId{index};
        if (schematic.symbol_definition(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

/** Return a sheet by exact name, if present. */
[[nodiscard]] inline std::optional<SheetId> sheet_by_name(const Schematic &schematic,
                                                          std::string_view name) {
    for (std::size_t index = 0; index < schematic.sheet_count(); ++index) {
        const auto id = SheetId{index};
        if (schematic.sheet(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

/** Return a sheet-local authored region by exact name, if present. */
[[nodiscard]] inline std::optional<std::size_t>
sheet_region_by_name(const Schematic &schematic, SheetId sheet, std::string_view name) {
    const auto &regions = schematic.sheet(sheet).regions();
    for (std::size_t index = 0; index < regions.size(); ++index) {
        if (regions[index].name() == name) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace volt::queries
