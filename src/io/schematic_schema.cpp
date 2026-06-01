#include <volt/io/schematic_schema.hpp>

namespace volt::io {

[[nodiscard]] std::string_view schematic_orientation_name(SchematicOrientation orientation) {
    switch (orientation) {
    case SchematicOrientation::Right:
        return "Right";
    case SchematicOrientation::Down:
        return "Down";
    case SchematicOrientation::Left:
        return "Left";
    case SchematicOrientation::Up:
        return "Up";
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}
[[nodiscard]] std::optional<SchematicOrientation>
schematic_orientation_from_name(std::string_view value) noexcept {
    if (value == "Right") {
        return SchematicOrientation::Right;
    }
    if (value == "Down") {
        return SchematicOrientation::Down;
    }
    if (value == "Left") {
        return SchematicOrientation::Left;
    }
    if (value == "Up") {
        return SchematicOrientation::Up;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view symbol_line_role_name(SymbolLineRole role) {
    switch (role) {
    case SymbolLineRole::Normal:
        return "Normal";
    case SymbolLineRole::TerminalLeadStart:
        return "TerminalLeadStart";
    case SymbolLineRole::TerminalLeadEnd:
        return "TerminalLeadEnd";
    }
    throw std::logic_error{"Unhandled symbol line role"};
}
[[nodiscard]] std::optional<SymbolLineRole>
symbol_line_role_from_name(std::string_view value) noexcept {
    if (value.empty() || value == "Normal") {
        return SymbolLineRole::Normal;
    }
    if (value == "TerminalLeadStart") {
        return SymbolLineRole::TerminalLeadStart;
    }
    if (value == "TerminalLeadEnd") {
        return SymbolLineRole::TerminalLeadEnd;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view text_horizontal_alignment_name(TextHorizontalAlignment alignment) {
    switch (alignment) {
    case TextHorizontalAlignment::Start:
        return "Start";
    case TextHorizontalAlignment::Middle:
        return "Middle";
    case TextHorizontalAlignment::End:
        return "End";
    }
    throw std::logic_error{"Unhandled text horizontal alignment"};
}
[[nodiscard]] std::optional<TextHorizontalAlignment>
text_horizontal_alignment_from_name(std::string_view value) noexcept {
    if (value == "Start") {
        return TextHorizontalAlignment::Start;
    }
    if (value == "Middle") {
        return TextHorizontalAlignment::Middle;
    }
    if (value == "End") {
        return TextHorizontalAlignment::End;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view text_vertical_alignment_name(TextVerticalAlignment alignment) {
    switch (alignment) {
    case TextVerticalAlignment::Top:
        return "Top";
    case TextVerticalAlignment::Middle:
        return "Middle";
    case TextVerticalAlignment::Bottom:
        return "Bottom";
    case TextVerticalAlignment::Baseline:
        return "Baseline";
    }
    throw std::logic_error{"Unhandled text vertical alignment"};
}
[[nodiscard]] std::optional<TextVerticalAlignment>
text_vertical_alignment_from_name(std::string_view value) noexcept {
    if (value == "Top") {
        return TextVerticalAlignment::Top;
    }
    if (value == "Middle") {
        return TextVerticalAlignment::Middle;
    }
    if (value == "Bottom") {
        return TextVerticalAlignment::Bottom;
    }
    if (value == "Baseline") {
        return TextVerticalAlignment::Baseline;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view sheet_orientation_name(SheetOrientation orientation) {
    switch (orientation) {
    case SheetOrientation::Portrait:
        return "Portrait";
    case SheetOrientation::Landscape:
        return "Landscape";
    }
    throw std::logic_error{"Unhandled sheet orientation"};
}
[[nodiscard]] std::optional<SheetOrientation>
sheet_orientation_from_name(std::string_view value) noexcept {
    if (value == "Portrait") {
        return SheetOrientation::Portrait;
    }
    if (value == "Landscape") {
        return SheetOrientation::Landscape;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view route_intent_name(RouteIntent intent) {
    switch (intent) {
    case RouteIntent::Direct:
        return "Direct";
    case RouteIntent::Orthogonal:
        return "Orthogonal";
    }
    throw std::logic_error{"Unhandled route intent"};
}
[[nodiscard]] std::optional<RouteIntent> route_intent_from_name(std::string_view value) noexcept {
    if (value == "Direct") {
        return RouteIntent::Direct;
    }
    if (value == "Orthogonal") {
        return RouteIntent::Orthogonal;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view power_port_kind_name(PowerPortKind kind) {
    switch (kind) {
    case PowerPortKind::Power:
        return "Power";
    case PowerPortKind::Ground:
        return "Ground";
    }
    throw std::logic_error{"Unhandled power port kind"};
}
[[nodiscard]] std::optional<PowerPortKind>
power_port_kind_from_name(std::string_view value) noexcept {
    if (value == "Power") {
        return PowerPortKind::Power;
    }
    if (value == "Ground") {
        return PowerPortKind::Ground;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view sheet_port_kind_name(SheetPortKind kind) {
    switch (kind) {
    case SheetPortKind::Input:
        return "Input";
    case SheetPortKind::Output:
        return "Output";
    case SheetPortKind::Bidirectional:
        return "Bidirectional";
    case SheetPortKind::OffPage:
        return "OffPage";
    }
    throw std::logic_error{"Unhandled sheet port kind"};
}
[[nodiscard]] std::optional<SheetPortKind>
sheet_port_kind_from_name(std::string_view value) noexcept {
    if (value == "Input") {
        return SheetPortKind::Input;
    }
    if (value == "Output") {
        return SheetPortKind::Output;
    }
    if (value == "Bidirectional") {
        return SheetPortKind::Bidirectional;
    }
    if (value == "OffPage") {
        return SheetPortKind::OffPage;
    }
    return std::nullopt;
}

} // namespace volt::io
