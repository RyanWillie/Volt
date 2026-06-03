#include <volt/schematic/readability_bounds_validation.hpp>

namespace volt::detail {

void validate_readability_bounds(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report) {
    const auto area = drawing_area_bounds(sheet.metadata());
    const auto title = title_block_bounds(sheet.metadata());
    for (const auto &object : readability_objects_for_sheet(schematic, sheet)) {
        if (!contains_bounds(area, object.bounds)) {
            add_readability_diagnostic(
                report, Severity::Error, "SCHEMATIC_OBJECT_OUTSIDE_USABLE_AREA",
                "Schematic object is outside the usable drawing area or reserved border strips",
                sheet_id, object.entity, object.context);
        }
        if (intersects_bounds(title, object.bounds)) {
            add_readability_diagnostic(report, Severity::Warning,
                                       "SCHEMATIC_OBJECT_OVERLAPS_TITLE_BLOCK",
                                       "Schematic object overlaps the reserved title block",
                                       sheet_id, object.entity, object.context);
        }
        if (object.authored_region.has_value()) {
            const auto &region = sheet.region(object.authored_region.value());
            if (!contains_bounds(region_bounds(region), object.bounds)) {
                add_readability_diagnostic(report, Severity::Error,
                                           "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION",
                                           "Schematic object authored through region '" +
                                               region.name() + "' extends outside that region",
                                           sheet_id, object.entity, object.context);
            }
        }
    }
}

void add_region_content_object_refs(std::vector<EntityRef> &refs,
                                    const std::vector<const ReadabilityObject *> &objects,
                                    SchematicBounds comparison_bounds) {
    auto added = false;
    for (const auto *object : objects) {
        if (!overlaps_bounds_area(object->bounds, comparison_bounds)) {
            continue;
        }
        refs.push_back(object->entity);
        refs.insert(refs.end(), object->context.begin(), object->context.end());
        added = true;
    }
    if (!added && !objects.empty()) {
        refs.push_back(objects.front()->entity);
        refs.insert(refs.end(), objects.front()->context.begin(), objects.front()->context.end());
    }
}

void validate_authored_region_content_overlaps(const Schematic &schematic, SheetId sheet_id,
                                               const Sheet &sheet, DiagnosticReport &report) {
    const auto objects = readability_objects_for_sheet(schematic, sheet);
    auto bounds_by_region = std::vector<std::optional<SchematicBounds>>(sheet.regions().size());
    auto objects_by_region =
        std::vector<std::vector<const ReadabilityObject *>>(sheet.regions().size());

    for (const auto &object : objects) {
        if (!object.authored_region.has_value()) {
            continue;
        }
        const auto region_index = object.authored_region.value();
        objects_by_region[region_index].push_back(&object);
        if (bounds_by_region[region_index].has_value()) {
            include_bounds(bounds_by_region[region_index].value(), object.bounds);
        } else {
            bounds_by_region[region_index] = object.bounds;
        }
    }

    for (std::size_t first = 0; first < bounds_by_region.size(); ++first) {
        if (!bounds_by_region[first].has_value()) {
            continue;
        }
        for (std::size_t second = first + 1U; second < bounds_by_region.size(); ++second) {
            if (!bounds_by_region[second].has_value() ||
                !overlaps_bounds_area(bounds_by_region[first].value(),
                                      bounds_by_region[second].value())) {
                continue;
            }

            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
            add_region_content_object_refs(refs, objects_by_region[first],
                                           bounds_by_region[second].value());
            add_region_content_object_refs(refs, objects_by_region[second],
                                           bounds_by_region[first].value());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP"},
                "Authored schematic regions '" + sheet.region(first).name() + "' and '" +
                    sheet.region(second).name() +
                    "' have overlapping occupied content bounds; move one region or tighten "
                    "the placement",
                std::move(refs),
            });
        }
    }
}

void add_title_block_overflow_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                         std::string_view column, std::string_view row_label) {
    report.add(Diagnostic{
        Severity::Warning,
        DiagnosticCode{"SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW"},
        "Schematic title-block " + std::string{column} + " text for '" + std::string{row_label} +
            "' exceeds the rendered column width",
        std::vector{EntityRef::sheet(sheet_id)},
    });
}

void validate_title_block_text_overflow(SheetId sheet_id, const Sheet &sheet,
                                        DiagnosticReport &report) {
    const auto &metadata = sheet.metadata();
    const auto title_bounds = title_block_bounds(metadata);
    const auto label_available_width =
        std::max(0.0, std::min(title_block_label_width, bounds_width(title_bounds)) -
                          title_block_label_x - 1.0);
    const auto value_available_width =
        std::max(0.0, bounds_width(title_bounds) - title_block_value_x - title_block_right_padding);

    const auto check_cell = [&](std::string_view column, std::string_view row_label,
                                std::string_view text, double available_width) {
        if (title_block_rendered_text_width(text, title_block_rendered_font_size) <=
            available_width + schematic_geometry_tolerance) {
            return;
        }
        add_title_block_overflow_diagnostic(report, sheet_id, column, row_label);
    };

    check_cell("label", "Title", "Title", label_available_width);
    check_cell("value", "Title", metadata.title(), value_available_width);
    for (const auto &field : metadata.title_block()) {
        check_cell("label", field.key(), field.key(), label_available_width);
        check_cell("value", field.key(), field.value(), value_available_width);
    }
}

} // namespace volt::detail
