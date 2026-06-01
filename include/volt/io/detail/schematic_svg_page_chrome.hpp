#pragma once

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

#include <volt/io/detail/schematic_svg_common.hpp>

namespace volt::io::detail {

[[nodiscard]] bool region_uses_dashed_frame(const SheetRegion &region);

void write_region_title_clip_defs_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet);

void write_sheet_defs_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet);

void write_sheet_grid_svg(std::ostream &out, SheetId sheet_id, const SheetMetadata &metadata);

void write_coordinate_zones_svg(std::ostream &out, const SheetMetadata &metadata);

void write_title_block_text_svg(std::ostream &out, std::string_view css_class, double x, double y,
                                std::string_view text, double available_width);

void write_title_block_row_svg(std::ostream &out, const SvgRect &rect, std::size_t row,
                               std::string_view key, std::string_view value);

void write_title_block_svg(std::ostream &out, SheetId sheet_id, const SheetMetadata &metadata);

void write_regions_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet);

void write_sheet_page_chrome_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet);

} // namespace volt::io::detail
