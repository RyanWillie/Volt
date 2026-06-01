#include <volt/io/detail/schematic_svg_page_chrome.hpp>

namespace volt::io::detail {

[[nodiscard]] bool region_uses_dashed_frame(const SheetRegion &region) {
    for (const auto &field : region.style()) {
        if (field.key() == "border" && field.value() == "dashed") {
            return true;
        }
    }
    return false;
}
void write_region_title_clip_defs_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto sheet_token = svg_sheet_token(sheet_id);
    for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
        const auto bounds = sheet.region(index).bounds();
        out << "      <clipPath id=\"region-title-clip-" << sheet_token << '-' << index << "\">\n";
        out << "        <rect x=\"";
        write_svg_number(out, bounds.x());
        out << "\" y=\"";
        write_svg_number(out, bounds.y());
        out << "\" width=\"";
        write_svg_number(out, bounds.width());
        out << "\" height=\"10\"/>\n";
        out << "      </clipPath>\n";
    }
}
void write_sheet_defs_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto &metadata = sheet.metadata();
    const auto area = drawing_area(metadata);
    const auto sheet_token = svg_sheet_token(sheet_id);

    out << "    <defs>\n";
    const auto title_block = title_block_rect(metadata);
    out << "      <clipPath id=\"title-block-clip-" << sheet_token << "\">\n";
    out << "        <rect x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, title_block.width);
    out << "\" height=\"";
    write_svg_number(out, title_block.height);
    out << "\"/>\n";
    out << "      </clipPath>\n";

    write_region_title_clip_defs_svg(out, sheet_id, sheet);

    if (metadata.grid().has_value() && metadata.grid()->visible()) {
        out << "      <pattern id=\"grid-" << sheet_token << "\" x=\"";
        write_svg_number(out, area.x);
        out << "\" y=\"";
        write_svg_number(out, area.y);
        out << "\" width=\"";
        write_svg_number(out, metadata.grid()->spacing());
        out << "\" height=\"";
        write_svg_number(out, metadata.grid()->spacing());
        out << "\" patternUnits=\"userSpaceOnUse\">\n";
        out << "        <circle class=\"sheet-grid-dot\" cx=\"0\" cy=\"0\" r=\"0.25\"/>\n";
        out << "      </pattern>\n";
    }
    out << "    </defs>\n";
}
void write_sheet_grid_svg(std::ostream &out, SheetId sheet_id, const SheetMetadata &metadata) {
    if (!metadata.grid().has_value() || !metadata.grid()->visible()) {
        return;
    }

    const auto area = drawing_area(metadata);
    out << "    <rect class=\"sheet-grid\" x=\"";
    write_svg_number(out, area.x);
    out << "\" y=\"";
    write_svg_number(out, area.y);
    out << "\" width=\"";
    write_svg_number(out, area.width);
    out << "\" height=\"";
    write_svg_number(out, area.height);
    out << "\" fill=\"url(#grid-" << svg_sheet_token(sheet_id) << ")\"/>\n";
}
void write_coordinate_zones_svg(std::ostream &out, const SheetMetadata &metadata) {
    if (!metadata.coordinate_zones().has_value() || !metadata.coordinate_zones()->visible()) {
        return;
    }

    const auto area = drawing_area(metadata);
    if (area.width <= 0.0 || area.height <= 0.0) {
        return;
    }

    const auto &zones = metadata.coordinate_zones().value();
    const auto column_width = area.width / static_cast<double>(zones.columns());
    const auto row_height = area.height / static_cast<double>(zones.rows());

    out << "    <g class=\"coordinate-zones\">\n";
    for (std::size_t column = 0; column < zones.columns(); ++column) {
        const auto x = area.x + (column_width * (static_cast<double>(column) + 0.5));
        out << "      <text class=\"coordinate-zone-label column\" x=\"";
        write_svg_number(out, x);
        out << "\" y=\"";
        write_svg_number(out, metadata.frame().margins().top() / 2.0);
        out << "\">" << column + 1U << "</text>\n";
        out << "      <text class=\"coordinate-zone-label column\" x=\"";
        write_svg_number(out, x);
        out << "\" y=\"";
        write_svg_number(out,
                         metadata.size().height() - (metadata.frame().margins().bottom() / 2.0));
        out << "\">" << column + 1U << "</text>\n";
    }
    for (std::size_t row = 0; row < zones.rows(); ++row) {
        const auto y = area.y + (row_height * (static_cast<double>(row) + 0.5));
        const auto label = zone_row_label(row);
        out << "      <text class=\"coordinate-zone-label row\" x=\"";
        write_svg_number(out, metadata.frame().margins().left() / 2.0);
        out << "\" y=\"";
        write_svg_number(out, y);
        out << "\">" << label << "</text>\n";
        out << "      <text class=\"coordinate-zone-label row\" x=\"";
        write_svg_number(out, metadata.size().width() - (metadata.frame().margins().right() / 2.0));
        out << "\" y=\"";
        write_svg_number(out, y);
        out << "\">" << label << "</text>\n";
    }
    out << "    </g>\n";
}
void write_title_block_text_svg(std::ostream &out, std::string_view css_class, double x, double y,
                                std::string_view text, double available_width) {
    const auto fit = fit_title_block_text(text, available_width);

    out << "      <text class=\"" << css_class << "\" x=\"";
    write_svg_number(out, x);
    out << "\" y=\"";
    write_svg_number(out, y);
    out << "\"";
    if (fit.fitted) {
        out << " data-full-text=\"" << svg_escape(text) << "\"";
    }
    if (fit.compressed) {
        out << " textLength=\"";
        write_svg_number(out, fit.text_length);
        out << "\" lengthAdjust=\"spacingAndGlyphs\"";
    }
    out << ">" << svg_escape(fit.text) << "</text>\n";
}
void write_title_block_row_svg(std::ostream &out, const SvgRect &rect, std::size_t row,
                               std::string_view key, std::string_view value) {
    const auto text_y = (static_cast<double>(row) * title_block_row_height) + 4.2;
    const auto label_available_width =
        std::max(0.0, std::min(title_block_label_width, rect.width) - title_block_label_x - 1.0);
    const auto value_available_width =
        std::max(0.0, rect.width - title_block_value_x - title_block_right_padding);
    write_title_block_text_svg(out, "title-block-label", title_block_label_x, text_y, key,
                               label_available_width);
    auto value_class = std::string{"title-block-value"};
    if (row == 0U) {
        value_class += " sheet-title";
    }
    write_title_block_text_svg(out, value_class, title_block_value_x, text_y, value,
                               value_available_width);
}
void write_title_block_svg(std::ostream &out, SheetId sheet_id, const SheetMetadata &metadata) {
    const auto rect = title_block_rect(metadata);
    out << "    <g class=\"title-block\" transform=\"translate(";
    write_svg_number(out, rect.x);
    out << ' ';
    write_svg_number(out, rect.y);
    out << ")\" clip-path=\"url(#title-block-clip-" << svg_sheet_token(sheet_id) << ")\">\n";
    out << "      <rect class=\"title-block-outline\" x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, rect.width);
    out << "\" height=\"";
    write_svg_number(out, rect.height);
    out << "\"/>\n";
    out << "      <line class=\"title-block-rule\" x1=\"";
    write_svg_number(out, title_block_label_width);
    out << "\" y1=\"0\" x2=\"";
    write_svg_number(out, title_block_label_width);
    out << "\" y2=\"";
    write_svg_number(out, rect.height);
    out << "\"/>\n";
    for (std::size_t row = 1; row < 1U + metadata.title_block().size(); ++row) {
        const auto y = title_block_row_height * static_cast<double>(row);
        out << "      <line class=\"title-block-rule\" x1=\"0\" y1=\"";
        write_svg_number(out, y);
        out << "\" x2=\"";
        write_svg_number(out, rect.width);
        out << "\" y2=\"";
        write_svg_number(out, y);
        out << "\"/>\n";
    }

    write_title_block_row_svg(out, rect, 0U, "Title", metadata.title());
    for (std::size_t index = 0; index < metadata.title_block().size(); ++index) {
        const auto &field = metadata.title_block()[index];
        write_title_block_row_svg(out, rect, index + 1U, field.key(), field.value());
    }
    out << "    </g>\n";
}
void write_regions_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto sheet_token = svg_sheet_token(sheet_id);
    for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
        const auto &region = sheet.region(index);
        const auto bounds = region.bounds();
        out << "    <g class=\"sheet-region\" data-region=\"" << svg_escape(region.name())
            << "\">\n";
        out << "      <rect class=\"sheet-region-frame";
        if (region_uses_dashed_frame(region)) {
            out << " dashed";
        }
        out << "\" data-region=\"" << svg_escape(region.name()) << "\" x=\"";
        write_svg_number(out, bounds.x());
        out << "\" y=\"";
        write_svg_number(out, bounds.y());
        out << "\" width=\"";
        write_svg_number(out, bounds.width());
        out << "\" height=\"";
        write_svg_number(out, bounds.height());
        out << "\"/>\n";
        out << "      <text class=\"sheet-region-title\" x=\"";
        write_svg_number(out, bounds.x() + 3.0);
        out << "\" y=\"";
        write_svg_number(out, bounds.y() + 6.0);
        out << "\" clip-path=\"url(#region-title-clip-" << sheet_token << '-' << index << ")\">"
            << svg_escape(region.title()) << "</text>\n";
        out << "    </g>\n";
    }
}
void write_sheet_page_chrome_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto &metadata = sheet.metadata();
    const auto area = drawing_area(metadata);

    out << "    <rect class=\"sheet\" x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, metadata.size().width());
    out << "\" height=\"";
    write_svg_number(out, metadata.size().height());
    out << "\"/>\n";
    write_sheet_grid_svg(out, sheet_id, metadata);
    if (metadata.frame().visible()) {
        out << "    <rect class=\"sheet-border\" x=\"0\" y=\"0\" width=\"";
        write_svg_number(out, metadata.size().width());
        out << "\" height=\"";
        write_svg_number(out, metadata.size().height());
        out << "\"/>\n";
        out << "    <rect class=\"drawing-frame\" x=\"";
        write_svg_number(out, area.x);
        out << "\" y=\"";
        write_svg_number(out, area.y);
        out << "\" width=\"";
        write_svg_number(out, area.width);
        out << "\" height=\"";
        write_svg_number(out, area.height);
        out << "\"/>\n";
    }
    write_coordinate_zones_svg(out, metadata);
    write_title_block_svg(out, sheet_id, metadata);
}

} // namespace volt::io::detail
