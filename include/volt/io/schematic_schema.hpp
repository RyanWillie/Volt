#pragma once

#include <optional>
#include <stdexcept>
#include <string_view>

#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

/** Return the canonical v1 schematic projection format name. */
[[nodiscard]] inline constexpr std::string_view schematic_format_name() noexcept {
    return "volt.schematic";
}

/** Return the canonical schematic projection format version written by this library. */
[[nodiscard]] inline constexpr int schematic_format_version() noexcept { return 1; }

[[nodiscard]] std::string_view schematic_orientation_name(SchematicOrientation orientation);

[[nodiscard]] std::optional<SchematicOrientation>
schematic_orientation_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view symbol_line_role_name(SymbolLineRole role);

[[nodiscard]] std::optional<SymbolLineRole>
symbol_line_role_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view text_horizontal_alignment_name(TextHorizontalAlignment alignment);

[[nodiscard]] std::optional<TextHorizontalAlignment>
text_horizontal_alignment_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view text_vertical_alignment_name(TextVerticalAlignment alignment);

[[nodiscard]] std::optional<TextVerticalAlignment>
text_vertical_alignment_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view sheet_orientation_name(SheetOrientation orientation);

[[nodiscard]] std::optional<SheetOrientation>
sheet_orientation_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view route_intent_name(RouteIntent intent);

[[nodiscard]] std::optional<RouteIntent> route_intent_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view power_port_kind_name(PowerPortKind kind);

[[nodiscard]] std::optional<PowerPortKind>
power_port_kind_from_name(std::string_view value) noexcept;

[[nodiscard]] std::string_view sheet_port_kind_name(SheetPortKind kind);

[[nodiscard]] std::optional<SheetPortKind>
sheet_port_kind_from_name(std::string_view value) noexcept;

} // namespace volt::io
