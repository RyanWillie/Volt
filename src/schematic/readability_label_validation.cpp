#include <volt/schematic/readability_label_validation.hpp>

namespace volt::detail {

[[nodiscard]] bool display_label_is_overlong_or_scoped(const std::string &label) {
    return label.size() > overlong_label_character_threshold ||
           label.find('/') != std::string::npos || label.find("::") != std::string::npos;
}

[[nodiscard]] bool ascii_upper_alpha(char character) noexcept {
    return character >= 'A' && character <= 'Z';
}

[[nodiscard]] bool ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

[[nodiscard]] bool visible_reference_label_looks_conventional(std::string_view label) noexcept {
    auto prefix_length = std::size_t{0};
    while (prefix_length < label.size() && ascii_upper_alpha(label[prefix_length])) {
        ++prefix_length;
    }
    if (prefix_length == 0U || prefix_length > 4U || prefix_length == label.size()) {
        return false;
    }
    for (std::size_t index = prefix_length; index < label.size(); ++index) {
        if (!ascii_digit(label[index])) {
            return false;
        }
    }
    return true;
}

void validate_visible_reference_labels(const Schematic &schematic, SheetId sheet_id,
                                       const Sheet &sheet, DiagnosticReport &report) {
    struct VisibleReference {
        SymbolInstanceId instance;
        ComponentId component;
        std::string_view label;
    };

    auto references = std::vector<VisibleReference>{};
    references.reserve(sheet.symbol_fields().size());
    const auto record_visible_reference = [&](SymbolInstanceId instance_id, ComponentId component,
                                              std::string_view label) {
        references.push_back(VisibleReference{instance_id, component, label});
        if (!visible_reference_label_looks_conventional(references.back().label)) {
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL"},
                "Visible schematic reference label '" + std::string{references.back().label} +
                    "' does not look like a conventional EDA reference designator",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::symbol_instance(instance_id),
                            EntityRef::component(component)},
            });
        }
    };

    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (field.name() != "reference") {
            continue;
        }
        const auto &instance = schematic.symbol_instance(field.symbol_instance());
        record_visible_reference(field.symbol_instance(), instance.component(), field.value());
    }

    auto reported = std::vector<bool>(references.size(), false);
    for (std::size_t first = 0; first < references.size(); ++first) {
        if (reported[first]) {
            continue;
        }
        auto duplicate_indices = std::vector<std::size_t>{first};
        for (std::size_t second = first + 1U; second < references.size(); ++second) {
            if (!reported[second] && references[second].label == references[first].label) {
                duplicate_indices.push_back(second);
            }
        }
        if (duplicate_indices.size() < 2U) {
            continue;
        }
        auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
        for (const auto index : duplicate_indices) {
            refs.push_back(EntityRef::symbol_instance(references[index].instance));
            refs.push_back(EntityRef::component(references[index].component));
            reported[index] = true;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_DUPLICATE_REFERENCE_LABEL"},
            "Schematic sheet has duplicate visible reference label '" +
                std::string{references[first].label} + "'",
            std::move(refs),
        });
    }
}

void validate_label_readability(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                DiagnosticReport &report) {
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().get(label.net());
        if (label.orientation() != SchematicOrientation::Right) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_TEXT_NOT_HORIZONTAL",
                "Schematic net label is rotated where horizontal text is expected", sheet_id,
                EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())});
        }
        if (display_label_is_overlong_or_scoped(label.label().value_or(net.name().value()))) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic display label is long or exposes an internal scoped name", sheet_id,
                EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())});
        }
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (field.orientation() != SchematicOrientation::Right) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_TEXT_NOT_HORIZONTAL",
                "Schematic symbol field is rotated where horizontal text is expected", sheet_id,
                EntityRef::symbol_field(field_id),
                std::vector{EntityRef::symbol_instance(field.symbol_instance())});
        }
        if (display_label_is_overlong_or_scoped(field.value())) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic symbol field is long or exposes an internal scoped name", sheet_id,
                EntityRef::symbol_field(field_id),
                std::vector{EntityRef::symbol_instance(field.symbol_instance())});
        }
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        if (display_label_is_overlong_or_scoped(port.name())) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic sheet port label is long or exposes an internal scoped name", sheet_id,
                EntityRef::sheet_port(port_id), std::vector{EntityRef::net(port.net())});
        }
    }
}

void validate_symbol_field_ownership_distance(const Schematic &schematic, SheetId sheet_id,
                                              const Sheet &sheet, DiagnosticReport &report) {
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        const auto field_bounds = text_bounds(field.position(), field.orientation(), field.value(),
                                              field.style(), symbol_field_rendered_font_size);
        const auto owner_bounds = symbol_instance_bounds(schematic, field.symbol_instance());
        if (bounds_gap(owner_bounds, field_bounds) <= symbol_field_owner_max_gap) {
            continue;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL"},
            "Schematic symbol field is far from its owning symbol",
            std::vector{EntityRef::sheet(sheet_id), EntityRef::symbol_field(field_id),
                        EntityRef::symbol_instance(field.symbol_instance())},
        });
    }
}

void validate_text_collisions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report) {
    const auto texts = readability_texts_for_sheet(schematic, sheet);

    for (std::size_t first = 0; first < texts.size(); ++first) {
        for (std::size_t second = first + 1U; second < texts.size(); ++second) {
            if (!intersects_bounds(texts[first].bounds, texts[second].bounds)) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), texts[first].entity,
                                               texts[second].entity};
            refs.insert(refs.end(), texts[first].context.begin(), texts[first].context.end());
            refs.insert(refs.end(), texts[second].context.begin(), texts[second].context.end());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_TEXT_COLLISION"},
                "Conservative schematic text bounds overlap; review label placement",
                std::move(refs),
            });
        }
    }
}

} // namespace volt::detail
