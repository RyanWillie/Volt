#pragma once

#include "binding_component_conversions.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <utility>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/parts/part_definition.hpp>

namespace volt::python {

namespace {

[[nodiscard]] inline py::dict required_dict_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<py::dict>(dict[name]);
}

[[nodiscard]] inline py::list required_list_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<py::list>(dict[name]);
}

[[nodiscard]] inline std::string required_string_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<std::string>(dict[name]);
}

[[nodiscard]] inline std::string optional_part_string_field(const py::dict &dict, const char *name,
                                                            std::string default_value) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return default_value;
    }
    return py::cast<std::string>(dict[name]);
}

[[nodiscard]] inline std::vector<std::string> string_vector_from_list(const py::list &values) {
    auto result = std::vector<std::string>{};
    result.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto item : values) {
        result.push_back(py::cast<std::string>(item));
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::PinDefinition> part_pins_from_list(const py::list &pins) {
    auto result = std::vector<volt::PinDefinition>{};
    result.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        const auto spec = pin_spec_from_dict(py::cast<py::dict>(item));
        auto attributes = volt::ElectricalAttributeMap{};
        if (spec.voltage_range.has_value()) {
            attributes.set(
                volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_range"},
                                              volt::ElectricalAttributeOwner::PinSpec,
                                              volt::ElectricalAttributeKind::Constraint,
                                              volt::UnitDimension::Voltage},
                volt::ElectricalAttributeValue{spec.voltage_range.value()});
        }
        result.emplace_back(spec.name, spec.number, spec.requirement, spec.terminal_kind,
                            spec.direction, spec.signal_domain, spec.drive_kind, spec.polarity,
                            std::move(attributes));
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::PinSpec>
component_pin_specs_from_list(const py::list &pins) {
    auto result = std::vector<volt::PinSpec>{};
    result.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        const auto spec = pin_spec_from_dict(py::cast<py::dict>(item));
        auto attributes = std::vector<volt::ElectricalAttributeAssignment>{};
        if (spec.voltage_range.has_value()) {
            attributes.push_back(volt::ElectricalAttributeAssignment{
                volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_range"},
                                              volt::ElectricalAttributeOwner::PinSpec,
                                              volt::ElectricalAttributeKind::Constraint,
                                              volt::UnitDimension::Voltage},
                volt::ElectricalAttributeValue{*spec.voltage_range}});
        }
        result.push_back(volt::PinSpec{spec.name, spec.number, spec.requirement, spec.terminal_kind,
                                       spec.direction, spec.signal_domain, spec.drive_kind,
                                       spec.polarity, std::move(attributes)});
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::PartSchematicAssetReference>
part_assets_from_list(const py::list &symbols,
                      const std::vector<volt::PinDefinition> &component_pins) {
    auto result = std::vector<volt::PartSchematicAssetReference>{};
    result.reserve(static_cast<std::size_t>(py::len(symbols)));
    for (const auto item : symbols) {
        const auto symbol = py::cast<py::dict>(item);
        auto seen = std::vector<bool>(component_pins.size(), false);
        const auto pin_list = required_list_field(symbol, "pins");
        for (const auto pin_item : pin_list) {
            const auto pin = py::cast<py::dict>(pin_item);
            const auto name = required_string_field(pin, "name");
            const auto number = required_string_field(pin, "number");
            const auto match =
                std::ranges::find(component_pins, number, &volt::PinDefinition::number);
            if (match == component_pins.end() || match->name() != name) {
                throw std::invalid_argument{
                    "Part artifact schematic symbol pin is outside component pins"};
            }
            const auto index =
                static_cast<std::size_t>(std::distance(component_pins.begin(), match));
            if (seen[index]) {
                throw std::invalid_argument{
                    "Part artifact schematic symbol contains duplicate component pin"};
            }
            seen[index] = true;
        }
        if (!std::ranges::all_of(seen, [](const auto value) { return value; })) {
            throw std::invalid_argument{
                "Part artifact schematic symbol must contain every component pin"};
        }
        result.emplace_back(required_string_field(symbol, "name"),
                            optional_part_string_field(symbol, "variant", "default"),
                            volt::ContentHash{required_string_field(symbol, "hash")});
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::SchematicSymbolReference>
component_symbol_references(const std::vector<volt::PartSchematicAssetReference> &assets) {
    auto result = std::vector<volt::SchematicSymbolReference>{};
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.emplace_back(asset.name(), asset.variant());
    }
    return result;
}

[[nodiscard]] inline volt::PartFootprintPadRole
part_footprint_pad_role_from_string(const std::string &role) {
    if (role == "mechanical") {
        return volt::PartFootprintPadRole::Mechanical;
    }
    if (role == "thermal") {
        return volt::PartFootprintPadRole::Thermal;
    }
    throw std::invalid_argument{"Part artifact footprint pad role must be mechanical or thermal"};
}

[[nodiscard]] inline std::vector<volt::PartFootprintPad>
part_footprint_pads_from_list(const py::list &pads) {
    auto result = std::vector<volt::PartFootprintPad>{};
    result.reserve(static_cast<std::size_t>(py::len(pads)));
    for (const auto item : pads) {
        const auto pad = py::cast<py::dict>(item);
        const auto label = required_string_field(pad, "label");
        const auto x = py::cast<double>(pad["x_mm"]);
        const auto y = py::cast<double>(pad["y_mm"]);
        const auto width = py::cast<double>(pad["width_mm"]);
        const auto height = py::cast<double>(pad["height_mm"]);
        if (pad.contains("role") && !pad["role"].is_none()) {
            result.emplace_back(
                label, x, y, width, height,
                part_footprint_pad_role_from_string(py::cast<std::string>(pad["role"])));
        } else {
            result.emplace_back(label, x, y, width, height);
        }
    }
    return result;
}

[[nodiscard]] inline volt::PartFootprintPoint part_footprint_point_from_dict(const py::dict &dict) {
    return volt::PartFootprintPoint{py::cast<double>(dict["x_mm"]), py::cast<double>(dict["y_mm"])};
}

[[nodiscard]] inline std::optional<volt::PartFootprintPolygon>
optional_part_footprint_polygon_from_object(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto points = py::cast<py::list>(value);
    auto vertices = std::vector<volt::PartFootprintPoint>{};
    vertices.reserve(static_cast<std::size_t>(py::len(points)));
    for (const auto item : points) {
        vertices.push_back(part_footprint_point_from_dict(py::cast<py::dict>(item)));
    }
    return volt::PartFootprintPolygon{std::move(vertices)};
}

[[nodiscard]] inline std::optional<volt::PartFootprintPolygon>
optional_part_footprint_polygon_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return std::nullopt;
    }
    return optional_part_footprint_polygon_from_object(dict[name]);
}

[[nodiscard]] inline volt::PartFootprintMarkingKind
part_footprint_marking_kind_from_string(const std::string &kind) {
    if (kind == "silkscreen") {
        return volt::PartFootprintMarkingKind::Silkscreen;
    }
    if (kind == "polarity") {
        return volt::PartFootprintMarkingKind::Polarity;
    }
    if (kind == "pin_1") {
        return volt::PartFootprintMarkingKind::PinOne;
    }
    throw std::invalid_argument{
        "Part artifact footprint marking kind must be silkscreen, polarity, or pin_1"};
}

[[nodiscard]] inline volt::PartFootprintMarking
part_footprint_marking_from_dict(const py::dict &dict) {
    return volt::PartFootprintMarking{
        part_footprint_marking_kind_from_string(required_string_field(dict, "kind")),
        optional_part_footprint_polygon_from_object(dict["polygon"]).value()};
}

[[nodiscard]] inline std::vector<volt::PartFootprintMarking>
part_footprint_markings_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return {};
    }
    const auto values = py::cast<py::list>(dict[name]);
    auto markings = std::vector<volt::PartFootprintMarking>{};
    markings.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto item : values) {
        markings.push_back(part_footprint_marking_from_dict(py::cast<py::dict>(item)));
    }
    return markings;
}

[[nodiscard]] inline std::vector<volt::PackageTerminalPadMapping>
part_terminal_pad_mappings_from_list(const py::list &mappings) {
    auto pads_by_terminal = std::map<std::string, std::vector<volt::FootprintPadKey>>{};
    for (const auto item : mappings) {
        const auto mapping = py::cast<py::dict>(item);
        pads_by_terminal[required_string_field(mapping, "pin_number")].emplace_back(
            required_string_field(mapping, "pad"));
    }
    auto result = std::vector<volt::PackageTerminalPadMapping>{};
    result.reserve(pads_by_terminal.size());
    for (auto &[terminal, pads] : pads_by_terminal) {
        result.emplace_back(volt::PackageTerminalKey{terminal}, std::move(pads));
    }
    return result;
}

[[nodiscard]] inline std::optional<volt::PartModel3DReference>
part_model_3d_reference_from_object(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(value);
    const auto translation = py::cast<std::array<double, 3>>(dict["translation_mm"]);
    for (const auto coordinate : translation) {
        require_finite(coordinate, "Part artifact 3D model translation must be finite");
    }
    const auto rotation = py::cast<double>(dict["rotation_deg"]);
    require_finite(rotation, "Part artifact 3D model rotation must be finite");
    return volt::PartModel3DReference{
        required_string_field(dict, "format"), required_string_field(dict, "file_name"),
        volt::ContentHash{required_string_field(dict, "hash")}, translation, rotation};
}

[[nodiscard]] inline std::optional<volt::PartModel3DReference>
part_model_3d_reference_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return std::nullopt;
    }
    return part_model_3d_reference_from_object(dict[name]);
}

[[nodiscard]] inline volt::OrderablePart orderable_part_from_dict(const py::dict &dict) {
    return volt::OrderablePart{
        volt::ManufacturerPart{required_string_field(dict, "manufacturer"),
                               required_string_field(dict, "mpn")},
        volt::PackageRef{required_string_field(dict, "package")},
        volt::HashedFootprintReference{
            volt::FootprintRef{required_string_field(dict, "footprint_library"),
                               required_string_field(dict, "footprint_name")},
            volt::ContentHash{required_string_field(dict, "footprint_hash")}},
        part_footprint_pads_from_list(required_list_field(dict, "footprint_pads")),
        part_terminal_pad_mappings_from_list(required_list_field(dict, "pin_pad_mappings")),
        string_vector_from_list(required_list_field(dict, "approved_alternate_mpns")),
        part_model_3d_reference_from_dict(dict, "model_3d"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_courtyard"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_body"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_fabrication_outline"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_assembly_outline"),
        part_footprint_markings_from_dict(dict, "footprint_markings")};
}

[[nodiscard]] inline volt::ElectricalSubjectKind
electrical_subject_kind_from_string(const std::string &value) {
    if (value == "framed_pin") {
        return volt::ElectricalSubjectKind::FramedPin;
    }
    if (value == "directed_relation") {
        return volt::ElectricalSubjectKind::DirectedRelation;
    }
    if (value == "supply_domain") {
        return volt::ElectricalSubjectKind::SupplyDomain;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical subject kind is unsupported"};
}

[[nodiscard]] inline std::string
electrical_subject_kind_to_string(volt::ElectricalSubjectKind value) {
    switch (value) {
    case volt::ElectricalSubjectKind::FramedPin:
        return "framed_pin";
    case volt::ElectricalSubjectKind::DirectedRelation:
        return "directed_relation";
    case volt::ElectricalSubjectKind::SupplyDomain:
        return "supply_domain";
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical subject kind is unsupported"};
}

[[nodiscard]] inline volt::ElectricalObservable
electrical_observable_from_string(const std::string &value) {
    if (value == "voltage") {
        return volt::ElectricalObservable::Voltage;
    }
    if (value == "current") {
        return volt::ElectricalObservable::Current;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical observable is unsupported"};
}

[[nodiscard]] inline std::string electrical_observable_to_string(volt::ElectricalObservable value) {
    switch (value) {
    case volt::ElectricalObservable::Voltage:
        return "voltage";
    case volt::ElectricalObservable::Current:
        return "current";
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical observable is unsupported"};
}

[[nodiscard]] inline volt::ElectricalMeaning
electrical_meaning_from_string(const std::string &value) {
    if (value == "characteristic") {
        return volt::ElectricalMeaning::Characteristic;
    }
    if (value == "accepted_range") {
        return volt::ElectricalMeaning::AcceptedRange;
    }
    if (value == "provided_range") {
        return volt::ElectricalMeaning::ProvidedRange;
    }
    if (value == "absolute_limit") {
        return volt::ElectricalMeaning::AbsoluteLimit;
    }
    if (value == "requirement") {
        return volt::ElectricalMeaning::Requirement;
    }
    if (value == "capability") {
        return volt::ElectricalMeaning::Capability;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical meaning is unsupported"};
}

[[nodiscard]] inline std::string electrical_meaning_to_string(volt::ElectricalMeaning value) {
    switch (value) {
    case volt::ElectricalMeaning::Characteristic:
        return "characteristic";
    case volt::ElectricalMeaning::AcceptedRange:
        return "accepted_range";
    case volt::ElectricalMeaning::ProvidedRange:
        return "provided_range";
    case volt::ElectricalMeaning::AbsoluteLimit:
        return "absolute_limit";
    case volt::ElectricalMeaning::Requirement:
        return "requirement";
    case volt::ElectricalMeaning::Capability:
        return "capability";
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical meaning is unsupported"};
}

[[nodiscard]] inline volt::FeatureRoleCardinality
feature_role_cardinality_from_string(const std::string &value) {
    if (value == "exactly_one") {
        return volt::FeatureRoleCardinality::ExactlyOne;
    }
    if (value == "one_or_more") {
        return volt::FeatureRoleCardinality::OneOrMore;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Feature-role cardinality is unsupported"};
}

[[nodiscard]] inline std::string
feature_role_cardinality_to_string(volt::FeatureRoleCardinality value) {
    switch (value) {
    case volt::FeatureRoleCardinality::ExactlyOne:
        return "exactly_one";
    case volt::FeatureRoleCardinality::OneOrMore:
        return "one_or_more";
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Feature-role cardinality is unsupported"};
}

[[nodiscard]] inline py::dict feature_schema_to_dict(const volt::FeatureSchema &schema) {
    auto roles = py::list{};
    for (const auto &role : schema.roles()) {
        auto item = py::dict{};
        item["key"] = role.key().value();
        item["cardinality"] = feature_role_cardinality_to_string(role.cardinality());
        roles.append(std::move(item));
    }
    auto requirements = py::list{};
    for (const auto &requirement : schema.required_records()) {
        auto item = py::dict{};
        item["observable"] = electrical_observable_to_string(requirement.observable);
        item["meaning"] = electrical_meaning_to_string(requirement.meaning);
        requirements.append(std::move(item));
    }
    auto result = py::dict{};
    result["key"] = schema.key().value();
    result["subject_kind"] = electrical_subject_kind_to_string(schema.subject_kind());
    result["roles"] = std::move(roles);
    result["required_records"] = std::move(requirements);
    return result;
}

[[nodiscard]] inline py::dict standard_feature_schema_to_dict(const std::string &name) {
    if (name == "supply_consumer") {
        return feature_schema_to_dict(volt::supply_consumer_feature_schema());
    }
    if (name == "supply_source") {
        return feature_schema_to_dict(volt::supply_source_feature_schema());
    }
    if (name == "diode_junction") {
        return feature_schema_to_dict(volt::diode_junction_feature_schema());
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Standard feature schema is unsupported"};
}

[[nodiscard]] inline volt::ComponentSubjectRef
component_subject_ref_from_dict(const py::dict &dict) {
    const auto kind = required_string_field(dict, "kind");
    const auto key = required_string_field(dict, "key");
    if (kind == "framed_pin") {
        return volt::ComponentSubjectRef::framed_pin(volt::FramedPinKey{key});
    }
    if (kind == "directed_relation") {
        return volt::ComponentSubjectRef::directed_relation(volt::RelationKey{key});
    }
    if (kind == "supply_domain") {
        return volt::ComponentSubjectRef::supply_domain(volt::SupplyDomainKey{key});
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Feature binding requires a named contract subject"};
}

[[nodiscard]] inline std::vector<volt::PinKey> pin_keys_from_list(const py::list &values) {
    auto result = std::vector<volt::PinKey>{};
    result.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto value : values) {
        result.emplace_back(py::cast<std::string>(value));
    }
    return result;
}

[[nodiscard]] inline volt::ComponentContractSpec
component_contract_spec_from_dict(const py::dict &dict) {
    auto spec = volt::ComponentContractSpec{
        .key = volt::ComponentKey{required_string_field(dict, "key")},
        .pin_keys = pin_keys_from_list(required_list_field(dict, "pin_keys")),
    };
    for (const auto item : required_list_field(dict, "framed_pins")) {
        const auto framed = py::cast<py::dict>(item);
        spec.framed_pins.emplace_back(volt::FramedPinKey{required_string_field(framed, "key")},
                                      volt::PinKey{required_string_field(framed, "pin")},
                                      volt::PinKey{required_string_field(framed, "reference")});
    }
    for (const auto item : required_list_field(dict, "relations")) {
        const auto relation = py::cast<py::dict>(item);
        spec.relations.emplace_back(volt::RelationKey{required_string_field(relation, "key")},
                                    volt::PinKey{required_string_field(relation, "from")},
                                    volt::PinKey{required_string_field(relation, "to")});
    }
    for (const auto item : required_list_field(dict, "supply_domains")) {
        const auto domain = py::cast<py::dict>(item);
        spec.supply_domains.emplace_back(
            volt::SupplyDomainKey{required_string_field(domain, "key")},
            pin_keys_from_list(required_list_field(domain, "positive_pins")),
            pin_keys_from_list(required_list_field(domain, "return_pins")));
    }
    for (const auto item : required_list_field(dict, "feature_schemas")) {
        const auto schema = py::cast<py::dict>(item);
        auto roles = std::vector<volt::FeatureRole>{};
        for (const auto role_item : required_list_field(schema, "roles")) {
            const auto role = py::cast<py::dict>(role_item);
            roles.emplace_back(
                volt::FeatureRoleKey{required_string_field(role, "key")},
                feature_role_cardinality_from_string(required_string_field(role, "cardinality")));
        }
        auto requirements = std::vector<volt::CanonicalRecordRequirement>{};
        for (const auto requirement_item : required_list_field(schema, "required_records")) {
            const auto requirement = py::cast<py::dict>(requirement_item);
            requirements.push_back(volt::CanonicalRecordRequirement{
                electrical_observable_from_string(required_string_field(requirement, "observable")),
                electrical_meaning_from_string(required_string_field(requirement, "meaning"))});
        }
        spec.feature_schemas.emplace_back(
            volt::FeatureSchemaKey{required_string_field(schema, "key")},
            electrical_subject_kind_from_string(required_string_field(schema, "subject_kind")),
            std::move(roles), std::move(requirements));
    }
    for (const auto item : required_list_field(dict, "feature_bindings")) {
        const auto binding = py::cast<py::dict>(item);
        auto roles = std::vector<volt::FeatureRoleBinding>{};
        for (const auto role_item : required_list_field(binding, "roles")) {
            const auto role = py::cast<py::dict>(role_item);
            roles.emplace_back(volt::FeatureRoleKey{required_string_field(role, "role")},
                               pin_keys_from_list(required_list_field(role, "pins")));
        }
        spec.feature_bindings.emplace_back(
            volt::FeatureKey{required_string_field(binding, "key")},
            volt::FeatureSchemaKey{required_string_field(binding, "schema")},
            component_subject_ref_from_dict(required_dict_field(binding, "subject")),
            std::move(roles));
    }
    return spec;
}

[[nodiscard]] inline std::optional<volt::ComponentContractSpec>
optional_component_contract_from_dict(const py::dict &dict) {
    if (!dict.contains("contract") || dict["contract"].is_none()) {
        return std::nullopt;
    }
    return component_contract_spec_from_dict(py::cast<py::dict>(dict["contract"]));
}

[[nodiscard]] inline volt::ComponentSpec component_spec_from_part_dict(const py::dict &dict) {
    const auto identity = required_dict_field(dict, "identity");
    return volt::ComponentSpec{
        .name = optional_part_string_field(dict, "component_name",
                                           required_string_field(identity, "name")),
        .pins = component_pin_specs_from_list(required_list_field(dict, "pins")),
        .properties = dict.contains("component_properties")
                          ? properties_from_dict(py::cast<py::dict>(dict["component_properties"]))
                          : volt::PropertyMap{},
        .source = volt::DefinitionSource{required_string_field(identity, "namespace"),
                                         required_string_field(identity, "name"),
                                         required_string_field(identity, "version")},
        .schematic_symbols = component_symbol_references(
            part_assets_from_list(required_list_field(dict, "symbols"),
                                  part_pins_from_list(required_list_field(dict, "pins")))),
        .contract = optional_component_contract_from_dict(dict),
    };
}

[[nodiscard]] inline volt::ElectricalPinIndex
electrical_pin_index(const volt::ComponentContract &contract, const std::string &key) {
    const auto match = std::ranges::find(contract.pin_keys(), volt::PinKey{key});
    if (match == contract.pin_keys().end()) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                     "Electrical subject references a foreign PinKey"};
    }
    return volt::ElectricalPinIndex{
        static_cast<std::size_t>(std::distance(contract.pin_keys().begin(), match))};
}

[[nodiscard]] inline volt::ElectricalSubject
electrical_subject_from_dict(const py::dict &dict, const volt::ComponentContract &contract) {
    const auto kind = required_string_field(dict, "kind");
    if (kind == "framed_pin") {
        const auto key = required_string_field(dict, "key");
        const auto match = std::ranges::find(contract.framed_pins(), volt::FramedPinKey{key},
                                             &volt::ContractFramedPin::key);
        if (match == contract.framed_pins().end()) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Electrical record references a foreign framed pin"};
        }
        return volt::ElectricalSubject::framed_pin(
            electrical_pin_index(contract, match->pin().value()),
            electrical_pin_index(contract, match->reference().value()));
    }
    if (kind == "directed_relation") {
        const auto key = required_string_field(dict, "key");
        const auto match = std::ranges::find(contract.relations(), volt::RelationKey{key},
                                             &volt::ContractDirectedRelation::key);
        if (match == contract.relations().end()) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Electrical record references a foreign relation"};
        }
        return volt::ElectricalSubject::directed_relation(
            electrical_pin_index(contract, match->from().value()),
            electrical_pin_index(contract, match->to().value()));
    }
    if (kind == "supply_domain") {
        const auto key = required_string_field(dict, "key");
        const auto match = std::ranges::find(contract.supply_domains(), volt::SupplyDomainKey{key},
                                             &volt::ContractSupplyDomain::key);
        if (match == contract.supply_domains().end()) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Electrical record references a foreign supply domain"};
        }
        auto positive = std::vector<volt::ElectricalPinIndex>{};
        auto returns = std::vector<volt::ElectricalPinIndex>{};
        for (const auto &pin : match->positive_pins()) {
            positive.push_back(electrical_pin_index(contract, pin.value()));
        }
        for (const auto &pin : match->return_pins()) {
            returns.push_back(electrical_pin_index(contract, pin.value()));
        }
        return volt::ElectricalSubject::supply_domain(std::move(positive), std::move(returns));
    }
    const auto positive_keys = required_list_field(dict, "positive_pins");
    const auto return_keys = required_list_field(dict, "return_pins");
    auto positive = std::vector<volt::ElectricalPinIndex>{};
    auto returns = std::vector<volt::ElectricalPinIndex>{};
    for (const auto key : positive_keys) {
        positive.push_back(electrical_pin_index(contract, py::cast<std::string>(key)));
    }
    for (const auto key : return_keys) {
        returns.push_back(electrical_pin_index(contract, py::cast<std::string>(key)));
    }
    if (kind == "directed_pins") {
        if (positive.size() != 1U || returns.size() != 1U) {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Directed-pin subject requires exactly two pins"};
        }
        return volt::ElectricalSubject::directed_relation(positive.front(), returns.front());
    }
    if (kind == "supply_pins") {
        return volt::ElectricalSubject::supply_domain(std::move(positive), std::move(returns));
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical record subject is unsupported"};
}

[[nodiscard]] inline volt::Quantity quantity(volt::ElectricalObservable observable, double value) {
    require_finite(value, "Electrical record quantities must be finite");
    return volt::Quantity{observable == volt::ElectricalObservable::Voltage
                              ? volt::UnitDimension::Voltage
                              : volt::UnitDimension::Current,
                          value};
}

[[nodiscard]] inline std::optional<double> optional_record_double(const py::dict &dict,
                                                                  const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return std::nullopt;
    }
    return py::cast<double>(dict[name]);
}

[[nodiscard]] inline volt::ElectricalRecordSelector
electrical_record_selector_from_dict(const py::dict &dict,
                                     const volt::ComponentContract &contract) {
    return volt::ElectricalRecordSelector{
        electrical_subject_from_dict(required_dict_field(dict, "subject"), contract),
        electrical_observable_from_string(required_string_field(dict, "observable")),
        electrical_meaning_from_string(required_string_field(dict, "meaning"))};
}

[[nodiscard]] inline volt::ElectricalValueExpression
electrical_value_expression_from_dict(const py::dict &dict, volt::ElectricalObservable observable,
                                      const volt::ComponentContract &contract) {
    const auto kind = required_string_field(dict, "kind");
    if (kind == "literal") {
        const auto value = optional_record_double(dict, "value");
        if (!value.has_value()) {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Literal electrical expression requires a value"};
        }
        return volt::ElectricalValueExpression::literal(quantity(observable, *value));
    }
    if (kind == "scaled_reference") {
        if (!dict.contains("selector") || dict["selector"].is_none()) {
            throw volt::KernelArgumentError{
                volt::ErrorCode::InvalidArgument,
                "Scaled electrical expression requires a record selector"};
        }
        const auto scale = optional_record_double(dict, "scale");
        if (!scale.has_value()) {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Scaled electrical expression requires a scale"};
        }
        return volt::ElectricalValueExpression::scaled_reference(
            electrical_record_selector_from_dict(py::cast<py::dict>(dict["selector"]), contract),
            *scale);
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical expression kind is unsupported"};
}

[[nodiscard]] inline std::optional<volt::ElectricalValueExpression>
optional_electrical_value_expression(py::handle value, volt::ElectricalObservable observable,
                                     const volt::ComponentContract &contract) {
    if (value.is_none()) {
        return std::nullopt;
    }
    return electrical_value_expression_from_dict(py::cast<py::dict>(value), observable, contract);
}

[[nodiscard]] inline volt::ElectricalCondition
electrical_condition_from_dict(const py::dict &dict, const volt::ComponentContract &contract) {
    const auto observable =
        electrical_observable_from_string(required_string_field(dict, "observable"));
    const auto subject =
        electrical_subject_from_dict(required_dict_field(dict, "subject"), contract);
    const auto predicate = required_string_field(dict, "predicate");
    if (predicate == "equal") {
        if (!dict.contains("minimum") || dict["minimum"].is_none()) {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Electrical equality requires a value"};
        }
        return volt::ElectricalCondition::equal(
            subject, observable,
            electrical_value_expression_from_dict(py::cast<py::dict>(dict["minimum"]), observable,
                                                  contract));
    }
    if (predicate == "range") {
        return volt::ElectricalCondition::range(
            subject, observable,
            optional_electrical_value_expression(dict["minimum"], observable, contract),
            optional_electrical_value_expression(dict["maximum"], observable, contract));
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical condition predicate is unsupported"};
}

[[nodiscard]] inline std::vector<volt::ElectricalCondition>
electrical_conditions_from_dict(const py::dict &dict, const volt::ComponentContract &contract) {
    auto result = std::vector<volt::ElectricalCondition>{};
    if (!dict.contains("conditions") || dict["conditions"].is_none()) {
        return result;
    }
    const auto values = py::cast<py::list>(dict["conditions"]);
    result.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto item : values) {
        result.push_back(electrical_condition_from_dict(py::cast<py::dict>(item), contract));
    }
    return result;
}

[[nodiscard]] inline volt::ElectricalValue
electrical_value_from_dict(const py::dict &dict, volt::ElectricalObservable observable) {
    const auto kind = required_string_field(dict, "value_kind");
    if (kind == "unknown") {
        return volt::ElectricalValue{volt::UnknownElectricalValue{}};
    }
    if (kind == "quantity") {
        return volt::ElectricalValue{quantity(observable, py::cast<double>(dict["value"]))};
    }
    if (kind == "continuous_current") {
        return volt::ElectricalValue{
            volt::ContinuousCurrent{quantity(observable, py::cast<double>(dict["value"]))}};
    }
    if (kind == "toleranced") {
        const auto nominal = optional_record_double(dict, "value");
        const auto minus = optional_record_double(dict, "tolerance_minus");
        const auto plus = optional_record_double(dict, "tolerance_plus");
        if (!nominal.has_value() || !minus.has_value() || !plus.has_value()) {
            throw volt::KernelArgumentError{
                volt::ErrorCode::InvalidArgument,
                "Toleranced electrical value requires nominal minus and plus values"};
        }
        const auto mode = required_string_field(dict, "tolerance_mode");
        auto tolerance = volt::Tolerance::percent(*minus, *plus);
        if (mode == "absolute") {
            tolerance = volt::Tolerance::absolute(quantity(observable, *minus),
                                                  quantity(observable, *plus));
        } else if (mode != "percent") {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Electrical tolerance mode is unsupported"};
        }
        return volt::ElectricalValue{
            volt::TolerancedQuantity{quantity(observable, *nominal), tolerance}};
    }
    const auto minimum = optional_record_double(dict, "minimum");
    const auto maximum = optional_record_double(dict, "maximum");
    if (kind == "range") {
        if (minimum.has_value() && maximum.has_value()) {
            return volt::ElectricalValue{volt::QuantityRange::bounded(
                quantity(observable, *minimum), quantity(observable, *maximum))};
        }
        if (minimum.has_value()) {
            return volt::ElectricalValue{
                volt::QuantityRange::minimum(quantity(observable, *minimum))};
        }
        if (maximum.has_value()) {
            return volt::ElectricalValue{
                volt::QuantityRange::maximum(quantity(observable, *maximum))};
        }
        throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                        "Electrical range requires at least one bound"};
    }
    if (kind == "envelope") {
        const auto typical = optional_record_double(dict, "typical");
        if (!minimum.has_value() || !typical.has_value() || !maximum.has_value()) {
            throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                            "Electrical envelope requires min typical and max"};
        }
        return volt::ElectricalValue{volt::CharacteristicEnvelope{quantity(observable, *minimum),
                                                                  quantity(observable, *typical),
                                                                  quantity(observable, *maximum)}};
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Electrical value kind is unsupported"};
}

[[nodiscard]] inline volt::ElectricalRecordSet
electrical_records_from_dict(const py::dict &dict, const volt::ComponentDefinition &component) {
    auto records = std::vector<volt::ElectricalRecordSpec>{};
    if (dict.contains("electrical_records") && !dict["electrical_records"].is_none()) {
        for (const auto item : py::cast<py::list>(dict["electrical_records"])) {
            const auto record = py::cast<py::dict>(item);
            const auto observable =
                electrical_observable_from_string(required_string_field(record, "observable"));
            auto evidence = std::vector<volt::ContentHash>{};
            for (const auto hash : required_list_field(record, "evidence")) {
                evidence.emplace_back(py::cast<std::string>(hash));
            }
            auto spec = volt::ElectricalRecordSpec{
                electrical_subject_from_dict(required_dict_field(record, "subject"),
                                             component.contract()),
                observable,
                electrical_meaning_from_string(required_string_field(record, "meaning")),
                electrical_value_from_dict(record, observable),
                electrical_conditions_from_dict(record, component.contract()),
                std::move(evidence)};
            records.push_back(std::move(spec));
        }
    }
    if (const auto voltage_rating = optional_record_double(dict, "voltage_rating");
        voltage_rating.has_value()) {
        if (component.contract().pin_keys().size() != 2U) {
            throw volt::KernelArgumentError{
                volt::ErrorCode::InvalidArgument,
                "voltage_rating shorthand requires an explicitly oriented two-pin part"};
        }
        records.push_back(volt::voltage_record(
            volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                       volt::ElectricalPinIndex{1}),
            volt::ElectricalMeaning::AbsoluteLimit,
            volt::ElectricalValue{volt::QuantityRange::maximum(
                quantity(volt::ElectricalObservable::Voltage, *voltage_rating))}));
    }
    return volt::ElectricalRecordSet{component.contract().pin_keys().size(), std::move(records)};
}

struct LoweredPartDefinition {
    volt::ComponentSpec component_spec;
    volt::ComponentDefinition component;
    volt::PartDefinition part;
};

[[nodiscard]] inline LoweredPartDefinition lower_part_definition_from_dict(const py::dict &dict) {
    auto component_spec = component_spec_from_part_dict(dict);
    auto circuit = volt::Circuit{};
    const auto component_id = circuit.define_component(component_spec);
    const auto component = circuit.get(component_id);
    const auto identity = required_dict_field(dict, "identity");
    const auto provenance = required_dict_field(dict, "provenance");
    const auto pins = part_pins_from_list(required_list_field(dict, "pins"));
    const auto assets = part_assets_from_list(required_list_field(dict, "symbols"), pins);
    auto pin_mappings = std::vector<volt::PinPackageTerminalMapping>{};
    pin_mappings.reserve(pins.size());
    for (std::size_t index = 0; index < pins.size(); ++index) {
        pin_mappings.emplace_back(component.contract().pin_keys()[index],
                                  std::vector{volt::PackageTerminalKey{pins[index].number()}});
    }
    auto part = volt::PartDefinition{
        component,
        volt::PartIdentity{required_string_field(identity, "namespace"),
                           required_string_field(identity, "name"),
                           required_string_field(identity, "version")},
        electrical_records_from_dict(dict, component),
        std::move(pin_mappings),
        {},
        volt::PartProvenance{optional_part_string_field(provenance, "datasheet", ""),
                             optional_part_string_field(provenance, "authored_by", ""),
                             optional_part_string_field(provenance, "derived_from", "")},
        assets,
        orderable_part_from_dict(required_dict_field(dict, "orderable_part"))};
    return LoweredPartDefinition{std::move(component_spec), component, std::move(part)};
}

} // namespace

} // namespace volt::python
