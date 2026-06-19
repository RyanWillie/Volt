#pragma once

#include "py_circuit.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace volt::python {

namespace {

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

} // namespace

} // namespace volt::python
