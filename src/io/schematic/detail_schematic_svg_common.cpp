#include <volt/io/schematic/detail/schematic_svg_common.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string svg_escape(std::string_view value) {
    auto result = std::string{};
    for (const auto character : value) {
        switch (character) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

void write_svg_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"SVG numeric values must be finite"};
    }
    if (std::abs(value) < 1e-12) {
        value = 0.0;
    }

    auto formatted = std::ostringstream{};
    formatted << std::setprecision(15) << value;
    out << formatted.str();
}

[[nodiscard]] std::string svg_sheet_token(SheetId id) {
    return "sheet-" + std::to_string(id.index());
}

[[nodiscard]] std::string svg_symbol_instance_id(SymbolInstanceId id) {
    return encode_local_id(id);
}

[[nodiscard]] std::string power_port_class(PowerPortKind kind) {
    switch (kind) {
    case PowerPortKind::Power:
        return "power";
    case PowerPortKind::Ground:
        return "ground";
    }
    throw std::logic_error{"Unhandled power port kind"};
}

[[nodiscard]] std::string sheet_port_class(SheetPortKind kind) {
    switch (kind) {
    case SheetPortKind::Input:
        return "input";
    case SheetPortKind::Output:
        return "output";
    case SheetPortKind::Bidirectional:
        return "bidirectional";
    case SheetPortKind::OffPage:
        return "off-page";
    }
    throw std::logic_error{"Unhandled sheet port kind"};
}

[[nodiscard]] double orientation_degrees(SchematicOrientation orientation) {
    switch (orientation) {
    case SchematicOrientation::Right:
        return 0.0;
    case SchematicOrientation::Down:
        return 90.0;
    case SchematicOrientation::Left:
        return 180.0;
    case SchematicOrientation::Up:
        return 270.0;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] double power_port_glyph_degrees(PowerPortKind kind,
                                              SchematicOrientation orientation) {
    const auto degrees = orientation_degrees(orientation);
    switch (kind) {
    case PowerPortKind::Power:
        return std::fmod(degrees + 90.0, 360.0);
    case PowerPortKind::Ground:
        return std::fmod(degrees + 270.0, 360.0);
    }
    throw std::logic_error{"Unhandled power port kind"};
}

void write_upright_text_transform_degrees(std::ostream &out, double parent_degrees, Point anchor) {
    const auto degrees = -parent_degrees;
    if (std::abs(degrees) < 1e-12) {
        return;
    }

    out << " transform=\"rotate(";
    write_svg_number(out, degrees);
    out << ' ';
    write_svg_number(out, anchor.x());
    out << ' ';
    write_svg_number(out, anchor.y());
    out << ")\"";
}

void write_upright_text_transform(std::ostream &out, SchematicOrientation parent_orientation,
                                  Point anchor) {
    write_upright_text_transform_degrees(out, orientation_degrees(parent_orientation), anchor);
}

void write_css_stroke_width(std::ostream &out, double width) {
    out << "stroke-width:";
    write_svg_number(out, width);
}

void write_css_font(std::ostream &out, double size) {
    out << "font:";
    write_svg_number(out, size);
    out << "px sans-serif";
}

[[nodiscard]] std::string_view svg_text_anchor(TextHorizontalAlignment alignment) {
    switch (alignment) {
    case TextHorizontalAlignment::Start:
        return "start";
    case TextHorizontalAlignment::Middle:
        return "middle";
    case TextHorizontalAlignment::End:
        return "end";
    }
    throw std::logic_error{"Unhandled text horizontal alignment"};
}

[[nodiscard]] std::string_view svg_dominant_baseline(TextVerticalAlignment alignment) {
    switch (alignment) {
    case TextVerticalAlignment::Top:
        return "text-before-edge";
    case TextVerticalAlignment::Middle:
        return "middle";
    case TextVerticalAlignment::Bottom:
        return "text-after-edge";
    case TextVerticalAlignment::Baseline:
        return "alphabetic";
    }
    throw std::logic_error{"Unhandled text vertical alignment"};
}

void write_text_presentation_attributes(std::ostream &out, SchematicTextStyle style) {
    out << " text-anchor=\"" << svg_text_anchor(style.horizontal_alignment())
        << "\" dominant-baseline=\"" << svg_dominant_baseline(style.vertical_alignment()) << '"';
    if (style.font_size().has_value()) {
        out << " style=\"font-size:";
        write_svg_number(out, style.font_size().value());
        out << "px\"";
    }
}

[[nodiscard]] std::string abbreviate_middle_to_fit(std::string_view text, double available_width,
                                                   double font_size) {
    const auto character_width = font_size * title_block_text_width_factor;
    if (available_width <= 0.0 || character_width <= 0.0) {
        return {};
    }

    const auto character_limit =
        static_cast<std::size_t>(std::floor(available_width / character_width));
    if (text.size() <= character_limit) {
        return std::string{text};
    }
    if (character_limit <= 3U) {
        return std::string{text.substr(0U, character_limit)};
    }

    const auto retained = character_limit - 3U;
    const auto prefix = (retained + 1U) / 2U;
    const auto suffix = retained - prefix;
    auto result = std::string{text.substr(0U, prefix)};
    result += "...";
    if (suffix != 0U) {
        result += text.substr(text.size() - suffix);
    }
    return result;
}

[[nodiscard]] SvgTitleBlockTextFit fit_title_block_text(std::string_view text,
                                                        double available_width) {
    const auto font_size = schematic_svg_visual_scale.title_block_font_size;
    const auto rendered_width = title_block_rendered_text_width(text, font_size);
    if (rendered_width <= available_width + 1e-12) {
        return SvgTitleBlockTextFit{std::string{text}, false, false, 0.0};
    }

    const auto scale = available_width <= 0.0 ? 0.0 : available_width / rendered_width;
    if (scale >= title_block_min_compression_scale) {
        return SvgTitleBlockTextFit{std::string{text}, true, true, available_width};
    }

    return SvgTitleBlockTextFit{abbreviate_middle_to_fit(text, available_width, font_size), true,
                                false, 0.0};
}

[[nodiscard]] SvgRect drawing_area(const SheetMetadata &metadata) {
    const auto margins = metadata.frame().margins();
    const auto width = std::max(0.0, metadata.size().width() - margins.left() - margins.right());
    const auto height = std::max(0.0, metadata.size().height() - margins.top() - margins.bottom());
    return SvgRect{margins.left(), margins.top(), width, height};
}

[[nodiscard]] SvgRect title_block_rect(const SheetMetadata &metadata) {
    const auto area = drawing_area(metadata);
    const auto rows = 1U + metadata.title_block().size();
    const auto height = title_block_row_height * static_cast<double>(rows);
    const auto width = std::min(title_block_width, area.width);
    return SvgRect{area.x + std::max(0.0, area.width - width),
                   area.y + std::max(0.0, area.height - height), width, height};
}

[[nodiscard]] std::string zone_row_label(std::size_t row) {
    auto value = row + 1U;
    auto label = std::string{};
    while (value != 0U) {
        --value;
        label.insert(label.begin(), static_cast<char>('A' + (value % 26U)));
        value /= 26U;
    }
    return label;
}

void write_xy_attributes(std::ostream &out, Point point, std::string_view x_name,
                         std::string_view y_name) {
    out << ' ' << x_name << "=\"";
    write_svg_number(out, point.x());
    out << "\" " << y_name << "=\"";
    write_svg_number(out, point.y());
    out << '"';
}

[[nodiscard]] Point point_on_arc(const SymbolArc &arc, double degrees) {
    const auto radians = degrees * svg_pi / 180.0;
    return Point{arc.center().x() + (arc.radius() * std::cos(radians)),
                 arc.center().y() + (arc.radius() * std::sin(radians))};
}

} // namespace volt::io::detail
