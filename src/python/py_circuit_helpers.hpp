#pragma once

#include "py_circuit.hpp"

#include "binding_pcb_conversions.hpp"

#include <array>

#include <volt/circuit/bom/bom.hpp>
#include <volt/pcb/routing/board_router.hpp>

namespace volt::python {

namespace {

constexpr auto default_authoring_via_drill_mm = 0.30;
constexpr auto default_authoring_via_annular_mm = 0.70;

[[nodiscard]] inline std::optional<std::size_t> optional_index_from_py(py::handle value,
                                                                       const char *message) {
    if (value.is_none()) {
        return std::nullopt;
    }
    try {
        return py::cast<std::size_t>(value);
    } catch (const py::cast_error &) {
        throw py::type_error{message};
    }
}

[[nodiscard]] inline std::vector<std::string> pad_labels_from_value(py::handle value) {
    if (py::isinstance<py::str>(value)) {
        return std::vector{py::cast<std::string>(value)};
    }

    if (py::isinstance<py::sequence>(value)) {
        const auto sequence = py::reinterpret_borrow<py::sequence>(value);
        if (py::len(sequence) == 0U) {
            throw std::invalid_argument{"Pin-pad mapping pad lists must not be empty"};
        }

        auto labels = std::vector<std::string>{};
        labels.reserve(static_cast<std::size_t>(py::len(sequence)));
        for (const auto item : sequence) {
            labels.push_back(py::cast<std::string>(item));
        }
        return labels;
    }

    throw py::type_error{"Pin-pad mapping values must be pad labels or sequences of pad labels"};
}

[[nodiscard]] inline std::vector<std::string> strings_from_iterable(py::handle value,
                                                                    const char *message) {
    if (!py::isinstance<py::iterable>(value)) {
        throw py::type_error{message};
    }
    auto result = std::vector<std::string>{};
    for (const auto item : py::reinterpret_borrow<py::iterable>(value)) {
        result.push_back(py::cast<std::string>(item));
    }
    return result;
}

[[nodiscard]] inline volt::BomSourcingSnapshot sourcing_snapshot_from_dict(const py::dict &dict) {
    auto snapshot = volt::BomSourcingSnapshot{};
    for (const auto item : dict) {
        const auto mpn = py::cast<std::string>(item.first);
        if (!py::isinstance<py::dict>(item.second)) {
            throw py::type_error{"BOM sourcing snapshot values must be dicts"};
        }
        snapshot.set_mpn_properties(
            mpn, properties_from_dict(py::reinterpret_borrow<py::dict>(item.second)));
    }
    return snapshot;
}

[[nodiscard]] inline volt::NetClassLayerScope
parse_net_class_layer_scope(const std::string &scope) {
    if (scope == "any_copper") {
        return volt::NetClassLayerScope::AnyCopper;
    }
    if (scope == "outer_only") {
        return volt::NetClassLayerScope::OuterOnly;
    }
    if (scope == "inner_only") {
        return volt::NetClassLayerScope::InnerOnly;
    }
    if (scope == "top_only") {
        return volt::NetClassLayerScope::TopOnly;
    }
    if (scope == "bottom_only") {
        return volt::NetClassLayerScope::BottomOnly;
    }
    throw py::value_error{"Unknown net-class layer scope: " + scope};
}

[[nodiscard]] inline volt::SchematicEndpoint
schematic_endpoint_from_tuple(const py::tuple &endpoint) {
    if (py::len(endpoint) != 4U) {
        throw py::value_error{"Schematic endpoint payloads must contain x, y, pin, and port net"};
    }

    const auto x = py::cast<double>(endpoint[0]);
    const auto y = py::cast<double>(endpoint[1]);
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    const auto pin = optional_index_from_py(endpoint[2], "Schematic endpoint pins must be indexes");
    const auto port_net =
        optional_index_from_py(endpoint[3], "Schematic endpoint port nets must be indexes");
    if (pin.has_value() && port_net.has_value()) {
        throw py::value_error{"Schematic endpoints cannot reference both a pin and a port net"};
    }

    const auto point = volt::Point{x, y};
    if (pin.has_value()) {
        return volt::SchematicEndpoint{point, pin_id(pin.value())};
    }
    if (port_net.has_value()) {
        return volt::SchematicEndpoint::port(point, net_id(port_net.value()));
    }
    return volt::SchematicEndpoint{point};
}

[[nodiscard]] inline std::vector<volt::SchematicEndpoint>
schematic_endpoints_from_list(const py::list &endpoints) {
    auto result = std::vector<volt::SchematicEndpoint>{};
    result.reserve(static_cast<std::size_t>(py::len(endpoints)));
    for (const auto item : endpoints) {
        result.push_back(schematic_endpoint_from_tuple(py::cast<py::tuple>(item)));
    }
    return result;
}

[[nodiscard]] inline volt::BoardRouteEndpoint
board_route_endpoint_from_tuple(const py::tuple &endpoint) {
    if (py::len(endpoint) != 4U) {
        throw py::value_error{
            "Board route endpoint payloads must contain x, y, placement, and pad"};
    }

    const auto x = py::cast<double>(endpoint[0]);
    const auto y = py::cast<double>(endpoint[1]);
    require_finite(x, "Board route endpoint coordinates must be finite");
    require_finite(y, "Board route endpoint coordinates must be finite");

    const auto placement =
        optional_index_from_py(endpoint[2], "Board route endpoint placements must be indexes");
    const auto pad =
        optional_index_from_py(endpoint[3], "Board route endpoint pads must be indexes");
    if (placement.has_value() != pad.has_value()) {
        throw py::value_error{"Board route pad endpoints require placement and pad IDs"};
    }

    const auto position = volt::BoardPoint{x, y};
    if (!placement.has_value()) {
        return volt::BoardRouteEndpoint::board_point(position);
    }
    return volt::BoardRouteEndpoint::footprint_pad(
        position, volt::ComponentPlacementId{placement.value()}, volt::FootprintPadId{pad.value()});
}

[[nodiscard]] inline std::vector<volt::BoardRouteEndpoint>
board_route_endpoints_from_list(const py::list &endpoints) {
    auto result = std::vector<volt::BoardRouteEndpoint>{};
    result.reserve(static_cast<std::size_t>(py::len(endpoints)));
    for (const auto item : endpoints) {
        result.push_back(board_route_endpoint_from_tuple(py::cast<py::tuple>(item)));
    }
    return result;
}

[[nodiscard]] inline std::optional<volt::PartModel3D> part_model_3d_from_object(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto data = py::cast<py::dict>(value);
    auto translation = std::array<double, 3>{};
    const auto translation_payload = py::cast<py::sequence>(data["translation_mm"]);
    if (py::len(translation_payload) != 3U) {
        throw py::value_error{"Selected-part 3D model translation must contain three numbers"};
    }
    for (auto index = std::size_t{0}; index < 3; ++index) {
        translation[index] = py::cast<double>(translation_payload[index]);
        require_finite(translation[index], "Selected-part 3D model translation must be finite");
    }
    const auto rotation = py::cast<double>(data["rotation_deg"]);
    require_finite(rotation, "Selected-part 3D model rotation must be finite");
    return volt::PartModel3D{py::cast<std::string>(data["format"]),
                             py::cast<std::string>(data["file_name"]), translation, rotation};
}

[[nodiscard]] inline py::object
part_model_3d_to_object(const std::optional<volt::PartModel3D> &model_3d) {
    if (!model_3d.has_value()) {
        return py::none{};
    }
    auto payload = py::dict{};
    payload["format"] = model_3d->format();
    payload["file_name"] = model_3d->file_name();
    payload["translation_mm"] =
        py::make_tuple(model_3d->translation_mm()[0], model_3d->translation_mm()[1],
                       model_3d->translation_mm()[2]);
    payload["rotation_deg"] = model_3d->rotation_deg();
    return payload;
}

[[nodiscard]] inline std::string
board_spatial_blocker_kind_name(volt::BoardSpatialBlockerKind kind) {
    switch (kind) {
    case volt::BoardSpatialBlockerKind::CopperClearance:
        return "copper_clearance";
    case volt::BoardSpatialBlockerKind::BoardOutline:
        return "board_outline";
    case volt::BoardSpatialBlockerKind::Keepout:
        return "keepout";
    }
    throw std::logic_error{"Unhandled board spatial blocker kind"};
}

[[nodiscard]] inline py::object optional_size_to_object(const std::optional<std::size_t> &value) {
    if (!value.has_value()) {
        return py::none{};
    }
    return py::cast(value.value());
}

template <typename Id>
[[nodiscard]] inline py::object optional_id_to_object(const std::optional<Id> &value) {
    if (!value.has_value()) {
        return py::none{};
    }
    return py::cast(value->index());
}

[[nodiscard]] inline py::dict
board_spatial_blocker_to_dict(const volt::BoardSpatialBlocker &blocker) {
    auto result = py::dict{};
    result["kind"] = board_spatial_blocker_kind_name(blocker.kind);
    result["shape_index"] = optional_size_to_object(blocker.shape_index);
    result["keepout"] = optional_id_to_object(blocker.keepout);
    result["layer"] = optional_id_to_object(blocker.layer);
    result["required_clearance_mm"] = blocker.required_clearance_mm;
    result["actual_clearance_mm"] = blocker.actual_clearance_mm;
    result["room"] = optional_id_to_object(blocker.room);
    return result;
}

[[nodiscard]] inline std::string
board_escape_failure_reason_name(volt::BoardEscapeFailureReason reason) {
    switch (reason) {
    case volt::BoardEscapeFailureReason::None:
        return "none";
    case volt::BoardEscapeFailureReason::PadUnconnected:
        return "pad_unconnected";
    case volt::BoardEscapeFailureReason::NoCopperLayer:
        return "no_copper_layer";
    case volt::BoardEscapeFailureReason::DisallowedLayer:
        return "disallowed_layer";
    case volt::BoardEscapeFailureReason::NoLegalCandidate:
        return "no_legal_candidate";
    }
    throw std::logic_error{"Unhandled board escape failure reason"};
}

[[nodiscard]] inline std::string board_side_name(volt::BoardSide side) {
    switch (side) {
    case volt::BoardSide::Top:
        return "top";
    case volt::BoardSide::Bottom:
        return "bottom";
    }
    throw std::logic_error{"Unhandled board side"};
}

[[nodiscard]] inline std::string board_layer_side_name(volt::BoardLayerSide side) {
    switch (side) {
    case volt::BoardLayerSide::Top:
        return "top";
    case volt::BoardLayerSide::Bottom:
        return "bottom";
    case volt::BoardLayerSide::Inner:
        return "inner";
    case volt::BoardLayerSide::Both:
        return "both";
    case volt::BoardLayerSide::None:
        return "none";
    }
    throw std::logic_error{"Unhandled board layer side"};
}

[[nodiscard]] inline double layer_z_mm(const volt::Board &board, const volt::LayerStack &stack,
                                       std::size_t stack_index) {
    const auto layer_id = stack.layers()[stack_index];
    const auto &layer = board.layer(layer_id);
    const auto half_thickness = stack.board_thickness_mm() / 2.0;
    switch (layer.side()) {
    case volt::BoardLayerSide::Top:
        return half_thickness;
    case volt::BoardLayerSide::Bottom:
        return -half_thickness;
    case volt::BoardLayerSide::Inner:
    case volt::BoardLayerSide::Both:
    case volt::BoardLayerSide::None:
        break;
    }
    if (stack.layers().size() == 1U) {
        return 0.0;
    }
    return half_thickness - ((stack.board_thickness_mm() * static_cast<double>(stack_index)) /
                             static_cast<double>(stack.layers().size() - 1U));
}

[[nodiscard]] inline py::tuple schematic_entity_result(std::size_t index, volt::NetId net) {
    return py::make_tuple(index, net.index());
}

[[nodiscard]] inline bool dict_contains(const py::dict &dict, const char *key) {
    return dict.contains(py::str{key});
}

[[nodiscard]] inline std::optional<double> optional_double_field(const py::dict &dict,
                                                                 const char *key) {
    if (!dict_contains(dict, key) || dict[py::str{key}].is_none()) {
        return std::nullopt;
    }
    return py::cast<double>(dict[py::str{key}]);
}

[[nodiscard]] inline std::optional<std::string> optional_string_field(const py::dict &dict,
                                                                      const char *key) {
    if (!dict_contains(dict, key) || dict[py::str{key}].is_none()) {
        return std::nullopt;
    }
    return py::cast<std::string>(dict[py::str{key}]);
}

[[nodiscard]] inline volt::NetClassTraceEnvironment
parse_trace_environment(const std::string &value) {
    if (value == "external" || value == "External") {
        return volt::NetClassTraceEnvironment::External;
    }
    if (value == "internal" || value == "Internal") {
        return volt::NetClassTraceEnvironment::Internal;
    }
    throw std::invalid_argument{"Unknown net-class trace environment"};
}

[[nodiscard]] inline volt::NetClassDielectricSpacingRule
parse_dielectric_spacing_rule(const std::string &value) {
    if (value == "stripline_1h" || value == "stripline-1h" || value == "Stripline1H") {
        return volt::NetClassDielectricSpacingRule::Stripline1H;
    }
    if (value == "microstrip_2h" || value == "microstrip-2h" || value == "Microstrip2H") {
        return volt::NetClassDielectricSpacingRule::Microstrip2H;
    }
    throw std::invalid_argument{"Unknown net-class dielectric spacing rule"};
}

[[nodiscard]] inline py::dict derivation_input_to_dict(const volt::NetClassDerivationInput &input) {
    auto result = py::dict{};
    result["name"] = input.name;
    if (input.text_value.empty()) {
        result["value"] = input.value;
    } else {
        result["value"] = input.text_value;
    }
    result["unit"] = input.unit;
    return result;
}

[[nodiscard]] inline py::dict derived_rule_to_dict(const volt::DerivedNetClassRuleValue &value) {
    auto result = py::dict{};
    result["value_mm"] = value.value_mm;
    auto calculator = py::dict{};
    calculator["id"] = value.derivation.calculator_id;
    calculator["name"] = value.derivation.calculator_name;
    calculator["standard"] = value.derivation.standard;
    calculator["reference"] = value.derivation.reference;
    result["calculator"] = std::move(calculator);
    auto inputs = py::list{};
    for (const auto &input : value.derivation.inputs) {
        inputs.append(derivation_input_to_dict(input));
    }
    result["inputs"] = std::move(inputs);
    return result;
}

[[noreturn]] inline void raise_schematic_authoring_error(const std::invalid_argument &error) {
    throw py::value_error{error.what()};
}

} // namespace

} // namespace volt::python
