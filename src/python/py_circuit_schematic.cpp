#include "py_circuit.hpp"

#include "py_circuit_schematic_helpers.hpp"

#include <sstream>

namespace volt::python {

std::size_t PyCircuit::schematic_sheet(const std::string &name, const py::dict &metadata) {
    auto &projection = schematic_projection();
    if (const auto existing = projection.sheet_by_name(name); existing.has_value()) {
        if (py::len(metadata) != 0) {
            const auto requested = sheet_metadata_from_dict(metadata, name);
            if (!(projection.sheet(existing.value()).metadata() == requested)) {
                throw std::invalid_argument{
                    "Schematic sheet already exists with different metadata"};
            }
        }
        return existing.value().index();
    }
    return projection.add_sheet(volt::Sheet{name, sheet_metadata_from_dict(metadata, name)})
        .index();
}

std::size_t PyCircuit::schematic_region(std::size_t sheet, const py::dict &region_data) {
    auto &projection = schematic_projection();
    const auto sheet_handle = sheet_id(sheet);
    const auto requested = sheet_region_from_dict(region_data);
    if (const auto existing = projection.sheet_region_by_name(sheet_handle, requested.name());
        existing.has_value()) {
        if (!(projection.sheet_region(sheet_handle, existing.value()) == requested)) {
            throw std::invalid_argument{"Schematic region already exists with different metadata"};
        }
        return existing.value();
    }
    return projection.add_sheet_region(sheet_handle, std::move(requested));
}

std::size_t PyCircuit::register_schematic_symbol(const py::dict &symbol_data) {
    auto symbol = symbol_definition_from_dict(symbol_data);
    auto &projection = schematic_projection();
    if (const auto existing = projection.symbol_definition_by_name(symbol.name());
        existing.has_value()) {
        if (projection.symbol_definition(existing.value()) != symbol) {
            throw std::invalid_argument{
                "Schematic symbol name already exists with a different definition"};
        }
        return existing.value().index();
    }
    return projection.add_symbol_definition(std::move(symbol)).index();
}

std::size_t PyCircuit::place_schematic_symbol(std::size_t sheet, std::size_t component,
                                              const std::string &symbol, double x, double y,
                                              const std::string &orientation,
                                              std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    const auto sheet_handle = sheet_id(sheet);
    static_cast<void>(projection.sheet(sheet_handle));

    const auto component_handle = component_id(component);
    static_cast<void>(circuit_.get(component_handle));

    const auto symbol_definition = ensure_schematic_symbol(symbol);
    return projection
        .place_symbol(sheet_handle,
                      volt::SymbolInstance{symbol_definition, component_handle, volt::Point{x, y},
                                           schematic_orientation_from_string(orientation),
                                           authored_region})
        .index();
}

std::string PyCircuit::schematic_symbol_orientation(std::size_t instance) {
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    return schematic_orientation_name(symbol_instance.orientation());
}

std::pair<double, double> PyCircuit::schematic_symbol_pin_anchor(std::size_t instance,
                                                                 const std::string &number) {
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    const auto &symbol = projection.symbol_definition(symbol_instance.symbol_definition());

    for (const auto &pin : symbol.pins()) {
        if (pin.number() == number) {
            const auto anchor = volt::transform_schematic_point(
                pin.anchor(), symbol_instance.position(), symbol_instance.orientation());
            return {anchor.x(), anchor.y()};
        }
    }

    throw std::out_of_range{"Schematic symbol has no pin with that number"};
}

py::list PyCircuit::schematic_symbol_pin_refs(std::size_t instance) {
    auto result = py::list{};
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    const auto &symbol = projection.symbol_definition(symbol_instance.symbol_definition());

    for (const auto &pin : symbol.pins()) {
        const auto anchor = volt::transform_schematic_point(
            pin.anchor(), symbol_instance.position(), symbol_instance.orientation());
        auto item = py::dict{};
        item["name"] = pin.name();
        item["number"] = pin.number();
        item["anchor"] = std::pair<double, double>{anchor.x(), anchor.y()};
        item["orientation"] = schematic_orientation_name(
            rotated_schematic_orientation(pin.orientation(), symbol_instance.orientation()));
        result.append(std::move(item));
    }
    return result;
}

std::size_t PyCircuit::add_schematic_wire(std::size_t sheet, std::size_t net,
                                          const std::vector<std::pair<double, double>> &points,
                                          const std::string &route_intent,
                                          std::optional<std::size_t> authored_region) {
    auto wire_points = std::vector<volt::Point>{};
    wire_points.reserve(points.size());
    for (const auto &[x, y] : points) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");
        wire_points.emplace_back(x, y);
    }

    auto &projection = schematic_projection();
    return projection
        .add_wire_run(sheet_id(sheet),
                      volt::WireRun{net_id(net), std::move(wire_points),
                                    route_intent_from_string(route_intent), authored_region})
        .index();
}

py::tuple PyCircuit::add_schematic_wire_for_endpoints(
    std::size_t sheet, std::optional<std::size_t> net,
    const std::vector<std::pair<double, double>> &points, const py::list &endpoints,
    const std::string &route_intent, std::optional<std::size_t> authored_region) {
    auto wire_points = std::vector<volt::Point>{};
    wire_points.reserve(points.size());
    for (const auto &[x, y] : points) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");
        wire_points.emplace_back(x, y);
    }

    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_wire_run_for_endpoints(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            std::move(wire_points), schematic_endpoints_from_list(endpoints),
            route_intent_from_string(route_intent), authored_region);
        return schematic_entity_result(id.index(), projection.wire_run(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_net_label(std::size_t sheet, std::size_t net, double x,
                                               double y, const std::string &orientation,
                                               std::optional<std::size_t> authored_region,
                                               std::optional<std::string> label,
                                               const std::string &horizontal_alignment,
                                               const std::string &vertical_alignment,
                                               std::optional<double> font_size) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_net_label(sheet_id(sheet),
                       volt::NetLabel{net_id(net), volt::Point{x, y},
                                      schematic_orientation_from_string(orientation),
                                      authored_region, std::move(label),
                                      text_style_from_strings(horizontal_alignment,
                                                              vertical_alignment, font_size)})
        .index();
}

py::tuple PyCircuit::add_schematic_net_label_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const py::tuple &endpoint,
    const std::string &orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label, const std::string &horizontal_alignment,
    const std::string &vertical_alignment, std::optional<double> font_size) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_net_label_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), schematic_orientation_from_string(orientation),
            authored_region, std::move(label),
            text_style_from_strings(horizontal_alignment, vertical_alignment, font_size));
        return schematic_entity_result(id.index(), projection.net_label(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_junction(std::size_t sheet, std::size_t net, double x,
                                              double y,
                                              std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_junction(sheet_id(sheet),
                      volt::Junction{net_id(net), volt::Point{x, y}, authored_region})
        .index();
}

py::tuple
PyCircuit::add_schematic_junction_for_endpoint(std::size_t sheet, std::optional<std::size_t> net,
                                               const py::tuple &endpoint,
                                               std::optional<std::size_t> authored_region) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_junction_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), authored_region);
        return schematic_entity_result(id.index(), projection.junction(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_terminal_marker(std::size_t sheet, std::size_t net,
                                                     const std::string &kind, double x, double y,
                                                     const std::string &orientation,
                                                     std::optional<std::size_t> authored_region,
                                                     std::optional<std::string> label) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_terminal_marker(sheet_id(sheet),
                             volt::PowerPort{net_id(net), power_port_kind_from_string(kind),
                                             volt::Point{x, y},
                                             schematic_orientation_from_string(orientation),
                                             authored_region, std::move(label)})
        .index();
}

py::tuple PyCircuit::add_schematic_terminal_marker_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const std::string &kind,
    const py::tuple &endpoint, const std::string &orientation,
    std::optional<std::size_t> authored_region, std::optional<std::string> label) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_terminal_marker_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), power_port_kind_from_string(kind),
            schematic_orientation_from_string(orientation), authored_region, std::move(label));
        return schematic_entity_result(id.index(), projection.power_port(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_no_connect_marker(std::size_t sheet, std::size_t pin, double x,
                                                       double y, const std::string &orientation,
                                                       const std::string &reason,
                                                       std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_no_connect_marker(sheet_id(sheet),
                               volt::NoConnectMarker{pin_id(pin), volt::Point{x, y},
                                                     schematic_orientation_from_string(orientation),
                                                     reason, authored_region})
        .index();
}

std::size_t PyCircuit::add_schematic_sheet_port(std::size_t sheet, std::size_t net,
                                                const std::string &name, const std::string &kind,
                                                double x, double y, const std::string &orientation,
                                                std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_sheet_port(
            sheet_id(sheet),
            volt::SheetPort{net_id(net), name, sheet_port_kind_from_string(kind), volt::Point{x, y},
                            schematic_orientation_from_string(orientation), authored_region})
        .index();
}

py::tuple PyCircuit::add_schematic_sheet_port_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const std::string &name,
    const std::string &kind, const py::tuple &endpoint, const std::string &orientation,
    std::optional<std::size_t> authored_region) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_sheet_port_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), name, sheet_port_kind_from_string(kind),
            schematic_orientation_from_string(orientation), authored_region);
        return schematic_entity_result(id.index(), projection.sheet_port(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_symbol_field(
    std::size_t sheet, std::size_t instance, const std::string &name, const std::string &value,
    double x, double y, const std::string &orientation, std::optional<std::size_t> authored_region,
    const std::string &horizontal_alignment, const std::string &vertical_alignment,
    std::optional<double> font_size) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_symbol_field(
            sheet_id(sheet),
            volt::SymbolField{
                volt::SymbolInstanceId{instance}, name, value, volt::Point{x, y},
                schematic_orientation_from_string(orientation), authored_region,
                text_style_from_strings(horizontal_alignment, vertical_alignment, font_size)})
        .index();
}

std::string PyCircuit::schematic_to_json() {
    volt::layout_schematic_text(schematic_projection());
    auto out = std::ostringstream{};
    volt::io::write_schematic(out, schematic_document_);
    return out.str();
}

std::string PyCircuit::schematic_to_svg() {
    volt::layout_schematic_text(schematic_projection());
    auto out = std::ostringstream{};
    volt::io::write_schematic_svg(out, schematic_projection());
    return out.str();
}

std::string PyCircuit::schematic_to_body_svg(std::size_t sheet, double margin) {
    volt::layout_schematic_text(schematic_projection());
    auto options = volt::io::SchematicSvgBodyOptions{};
    options.margin = margin;
    auto out = std::ostringstream{};
    volt::io::write_schematic_body_svg(out, schematic_projection(), sheet_id(sheet), options);
    return out.str();
}

py::list PyCircuit::schematic_svg_pages() {
    volt::layout_schematic_text(schematic_projection());
    auto result = py::list{};
    for (const auto &page : volt::io::write_schematic_svg_pages(schematic_projection())) {
        auto item = py::dict{};
        item["sheet"] = page.sheet.index();
        item["name"] = page.name;
        item["svg"] = page.svg;
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::load_schematic_json(const std::string &text) {
    schematic_document_.replace_schematic(volt::io::read_schematic_text(text, circuit_));
}

std::vector<std::string> PyCircuit::schematic_sheet_names() const {
    const auto &projection = schematic_document_.schematic();
    auto names = std::vector<std::string>{};
    names.reserve(projection.sheet_count());
    for (std::size_t index = 0; index < projection.sheet_count(); ++index) {
        names.push_back(projection.sheet(volt::SheetId{index}).name());
    }
    return names;
}

volt::Schematic &PyCircuit::schematic_projection() { return schematic_document_.schematic(); }

volt::SymbolDefId PyCircuit::ensure_schematic_symbol(const std::string &name) {
    auto &projection = schematic_projection();
    if (const auto existing = projection.symbol_definition_by_name(name); existing.has_value()) {
        return existing.value();
    }

    auto symbol = built_in_symbol(name);
    if (!symbol.has_value()) {
        throw std::invalid_argument{"Unknown schematic symbol"};
    }
    return projection.add_symbol_definition(std::move(symbol.value()));
}

} // namespace volt::python
