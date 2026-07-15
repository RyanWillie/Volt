#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

#include "logical_circuit_parser.hpp"
#include "logical_net_class_format.hpp"

namespace volt::io::detail {

[[nodiscard]] LogicalCircuitRestorationPlan LogicalCircuitParser::parse() {
    require(document_.is_object(), "Logical circuit document must be an object");
    require_format(document_);
    require_version(document_);

    read_pin_definitions();
    read_component_definitions();
    read_components();
    read_pins();
    read_nets();
    read_net_classes();
    read_design_intent();
    read_module_definitions();
    read_module_instances();

    return std::move(plan_);
}

void LogicalCircuitParser::require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelLogicError{ErrorCode::InvalidArgument, message};
    }
}

const nlohmann::json &LogicalCircuitParser::field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading logical circuit");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

std::string LogicalCircuitParser::string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

std::string LogicalCircuitParser::optional_string_field(const nlohmann::json &object,
                                                        const char *name,
                                                        std::string default_value) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return default_value;
    }
    require(it->is_string(), std::string{"Expected string field: "} + name);
    return it->get<std::string>();
}

void LogicalCircuitParser::require_format(const nlohmann::json &object) {
    const auto actual = string_field(object, "format");
    require(actual == logical_circuit_format_name(),
            "Unsupported logical circuit format: " + actual);
}

void LogicalCircuitParser::require_version(const nlohmann::json &object) {
    const auto &value = field(object, "version");
    require(value.is_number_integer(), "Expected integer field: version");
    const auto actual = value.get<std::int64_t>();
    require(actual == static_cast<std::int64_t>(logical_circuit_format_version()),
            "Unsupported logical circuit format version: " + std::to_string(actual));
}

const nlohmann::json &LogicalCircuitParser::array_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

const nlohmann::json *LogicalCircuitParser::optional_array_field(const nlohmann::json &object,
                                                                 const char *name) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return nullptr;
    }
    require(it->is_array(), std::string{"Expected array field: "} + name);
    return &*it;
}

[[nodiscard]] ConnectionRequirement
LogicalCircuitParser::connection_requirement(const std::string &value) {
    if (value == "Optional")
        return ConnectionRequirement::Optional;
    if (value == "Required")
        return ConnectionRequirement::Required;
    if (value == "MustNotConnect")
        return ConnectionRequirement::MustNotConnect;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ConnectionRequirement value"};
}

[[nodiscard]] ElectricalTerminalKind
LogicalCircuitParser::electrical_terminal_kind(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalTerminalKind::Unspecified;
    if (value == "Passive")
        return ElectricalTerminalKind::Passive;
    if (value == "Signal")
        return ElectricalTerminalKind::Signal;
    if (value == "Power")
        return ElectricalTerminalKind::Power;
    if (value == "Ground")
        return ElectricalTerminalKind::Ground;
    if (value == "NoConnect")
        return ElectricalTerminalKind::NoConnect;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalTerminalKind value"};
}

[[nodiscard]] ElectricalDirection
LogicalCircuitParser::electrical_direction(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalDirection::Unspecified;
    if (value == "Input")
        return ElectricalDirection::Input;
    if (value == "Output")
        return ElectricalDirection::Output;
    if (value == "Bidirectional")
        return ElectricalDirection::Bidirectional;
    if (value == "Passive")
        return ElectricalDirection::Passive;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalDirection value"};
}

[[nodiscard]] ElectricalSignalDomain
LogicalCircuitParser::electrical_signal_domain(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalSignalDomain::Unspecified;
    if (value == "Digital")
        return ElectricalSignalDomain::Digital;
    if (value == "Analog")
        return ElectricalSignalDomain::Analog;
    if (value == "Mixed")
        return ElectricalSignalDomain::Mixed;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalSignalDomain value"};
}

[[nodiscard]] ElectricalDriveKind
LogicalCircuitParser::electrical_drive_kind(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalDriveKind::Unspecified;
    if (value == "PushPull")
        return ElectricalDriveKind::PushPull;
    if (value == "OpenCollector")
        return ElectricalDriveKind::OpenCollector;
    if (value == "OpenDrain")
        return ElectricalDriveKind::OpenDrain;
    if (value == "HighImpedance")
        return ElectricalDriveKind::HighImpedance;
    if (value == "Passive")
        return ElectricalDriveKind::Passive;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalDriveKind value"};
}

[[nodiscard]] ElectricalPolarity
LogicalCircuitParser::electrical_polarity(const std::string &value) {
    if (value == "None")
        return ElectricalPolarity::None;
    if (value == "ActiveHigh")
        return ElectricalPolarity::ActiveHigh;
    if (value == "ActiveLow")
        return ElectricalPolarity::ActiveLow;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalPolarity value"};
}

[[nodiscard]] NetKind LogicalCircuitParser::net_kind(const std::string &value) {
    if (value == "Signal")
        return NetKind::Signal;
    if (value == "Power")
        return NetKind::Power;
    if (value == "Ground")
        return NetKind::Ground;
    if (value == "Clock")
        return NetKind::Clock;
    if (value == "Analog")
        return NetKind::Analog;
    if (value == "HighCurrent")
        return NetKind::HighCurrent;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid NetKind value"};
}

[[nodiscard]] PortRole LogicalCircuitParser::port_role(const std::string &value) {
    if (value == "Passive")
        return PortRole::Passive;
    if (value == "Input")
        return PortRole::Input;
    if (value == "Output")
        return PortRole::Output;
    if (value == "Bidirectional")
        return PortRole::Bidirectional;
    if (value == "PowerInput")
        return PortRole::PowerInput;
    if (value == "PowerOutput")
        return PortRole::PowerOutput;
    if (value == "Ground")
        return PortRole::Ground;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PortRole value"};
}

[[nodiscard]] UnitDimension LogicalCircuitParser::unit_dimension(const std::string &value) {
    if (value == "resistance")
        return UnitDimension::Resistance;
    if (value == "capacitance")
        return UnitDimension::Capacitance;
    if (value == "inductance")
        return UnitDimension::Inductance;
    if (value == "voltage")
        return UnitDimension::Voltage;
    if (value == "current")
        return UnitDimension::Current;
    if (value == "power")
        return UnitDimension::Power;
    if (value == "frequency")
        return UnitDimension::Frequency;
    if (value == "time")
        return UnitDimension::Time;
    if (value == "temperature")
        return UnitDimension::Temperature;
    if (value == "ratio")
        return UnitDimension::Ratio;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid unit dimension value"};
}

[[nodiscard]] ToleranceMode LogicalCircuitParser::tolerance_mode(const std::string &value) {
    if (value == "absolute")
        return ToleranceMode::Absolute;
    if (value == "percent")
        return ToleranceMode::Percent;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid tolerance mode value"};
}

[[nodiscard]] double LogicalCircuitParser::number_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    return value.get<double>();
}

[[nodiscard]] PropertyValue LogicalCircuitParser::property_value(const nlohmann::json &object) {
    require(object.is_object(), "Property value must be an object");
    const auto type = string_field(object, "type");
    const auto &value = field(object, "value");
    if (type == "string") {
        require(value.is_string(), "String property value must be a string");
        return PropertyValue{value.get<std::string>()};
    }
    if (type == "boolean") {
        require(value.is_boolean(), "Boolean property value must be a boolean");
        return PropertyValue{value.get<bool>()};
    }
    if (type == "integer") {
        require(value.is_number_integer(), "Integer property value must be an integer");
        return PropertyValue{value.get<std::int64_t>()};
    }
    if (type == "number") {
        require(value.is_number(), "Number property value must be a number");
        return PropertyValue{value.get<double>()};
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid property value type"};
}

[[nodiscard]] PropertyMap LogicalCircuitParser::properties(const nlohmann::json &object) {
    require(object.is_object(), "Properties must be an object");
    auto result = PropertyMap{};
    for (const auto &[key, value] : object.items()) {
        result.set(PropertyKey{key}, property_value(value));
    }
    return result;
}

[[nodiscard]] ElectricalAttributeValue
LogicalCircuitParser::electrical_attribute_value(const nlohmann::json &object) {
    require(object.is_object(), "Electrical attribute value must be an object");
    const auto type = string_field(object, "type");
    const auto dimension = unit_dimension(string_field(object, "dimension"));
    if (type == "quantity") {
        return ElectricalAttributeValue{Quantity{dimension, number_field(object, "value")}};
    }
    if (type == "tolerance") {
        const auto mode = tolerance_mode(string_field(object, "mode"));
        if (mode == ToleranceMode::Percent) {
            require(dimension == UnitDimension::Ratio, "Percent tolerance dimension must be ratio");
            return ElectricalAttributeValue{
                Tolerance::percent(number_field(object, "minus"), number_field(object, "plus"))};
        }

        return ElectricalAttributeValue{
            Tolerance::absolute(Quantity{dimension, number_field(object, "minus")},
                                Quantity{dimension, number_field(object, "plus")})};
    }
    if (type == "range") {
        const auto minimum = object.find("minimum");
        const auto maximum = object.find("maximum");
        require(minimum != object.end() || maximum != object.end(),
                "Quantity range must contain at least one bound");
        if (minimum != object.end()) {
            require(minimum->is_number(), "Quantity range minimum must be a number");
        }
        if (maximum != object.end()) {
            require(maximum->is_number(), "Quantity range maximum must be a number");
        }
        if (minimum != object.end() && maximum != object.end()) {
            return ElectricalAttributeValue{
                QuantityRange::bounded(Quantity{dimension, minimum->get<double>()},
                                       Quantity{dimension, maximum->get<double>()})};
        }
        if (minimum != object.end()) {
            return ElectricalAttributeValue{
                QuantityRange::minimum(Quantity{dimension, minimum->get<double>()})};
        }
        return ElectricalAttributeValue{
            QuantityRange::maximum(Quantity{dimension, maximum->get<double>()})};
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid electrical attribute value type"};
}

[[nodiscard]] ElectricalAttributeMap LogicalCircuitParser::electrical_attributes(
    const nlohmann::json &object, ElectricalAttributeOwner owner, ElectricalAttributeKind kind) {
    auto result = ElectricalAttributeMap{};
    const auto it = object.find("electrical_attributes");
    if (it == object.end()) {
        return result;
    }
    require(it->is_object(), "Electrical attributes must be an object");
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        result.set(ElectricalAttributeSpec{ElectricalAttributeName{name}, owner, kind,
                                           attribute.dimension()},
                   attribute);
    }
    return result;
}

[[nodiscard]] std::optional<DefinitionSource>
LogicalCircuitParser::definition_source(const nlohmann::json &object) {
    const auto it = object.find("source");
    if (it == object.end()) {
        return std::nullopt;
    }
    require(it->is_object(), "Definition source must be an object");
    return DefinitionSource{string_field(*it, "namespace"), string_field(*it, "name"),
                            string_field(*it, "version")};
}

[[nodiscard]] std::vector<SchematicSymbolReference>
LogicalCircuitParser::schematic_symbol_references(const nlohmann::json &object) {
    auto result = std::vector<SchematicSymbolReference>{};
    const auto symbols = optional_array_field(object, "schematic_symbols");
    if (symbols == nullptr) {
        return result;
    }

    result.reserve(symbols->size());
    for (const auto &symbol : *symbols) {
        require(symbol.is_object(), "Schematic symbol reference must be an object");
        result.emplace_back(string_field(symbol, "name"),
                            optional_string_field(symbol, "variant", "default"));
    }
    return result;
}

ElectricalSubjectKind LogicalCircuitParser::component_subject_kind(const std::string &value) {
    if (value == "framed_pin")
        return ElectricalSubjectKind::FramedPin;
    if (value == "directed_relation")
        return ElectricalSubjectKind::DirectedRelation;
    if (value == "supply_domain")
        return ElectricalSubjectKind::SupplyDomain;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid component-contract subject kind"};
}

FeatureRoleCardinality LogicalCircuitParser::feature_role_cardinality(const std::string &value) {
    if (value == "exactly_one")
        return FeatureRoleCardinality::ExactlyOne;
    if (value == "one_or_more")
        return FeatureRoleCardinality::OneOrMore;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid feature-role cardinality"};
}

ElectricalObservable LogicalCircuitParser::canonical_observable(const std::string &value) {
    if (value == "voltage")
        return ElectricalObservable::Voltage;
    if (value == "current")
        return ElectricalObservable::Current;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid canonical electrical observable"};
}

ElectricalMeaning LogicalCircuitParser::canonical_meaning(const std::string &value) {
    if (value == "characteristic")
        return ElectricalMeaning::Characteristic;
    if (value == "accepted_range")
        return ElectricalMeaning::AcceptedRange;
    if (value == "provided_range")
        return ElectricalMeaning::ProvidedRange;
    if (value == "absolute_limit")
        return ElectricalMeaning::AbsoluteLimit;
    if (value == "requirement")
        return ElectricalMeaning::Requirement;
    if (value == "capability")
        return ElectricalMeaning::Capability;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid canonical electrical meaning"};
}

std::optional<LogicalCircuitParser::ParsedComponentContract>
LogicalCircuitParser::component_contract(const nlohmann::json &object) {
    const auto contract_it = object.find("contract");
    if (contract_it == object.end()) {
        return std::nullopt;
    }
    const auto &contract = *contract_it;
    require(contract.is_object(), "Component contract must be an object");
    const auto &version = field(contract, "semantic_model_version");
    require(version.is_number_integer() && version.get<std::int64_t>() == 1,
            "Unsupported component-contract semantic model version");

    const auto pin_keys = [](const nlohmann::json &array) {
        require(array.is_array(), "Component-contract PinKeys must be an array");
        auto result = std::vector<PinKey>{};
        result.reserve(array.size());
        for (const auto &value : array) {
            require(value.is_string(), "Component-contract PinKey must be a string");
            result.emplace_back(value.get<std::string>());
        }
        return result;
    };

    auto spec = ComponentContractSpec{ComponentKey{string_field(contract, "key")},
                                      pin_keys(field(contract, "pin_keys"))};
    for (const auto &subject : array_field(contract, "framed_pins")) {
        spec.framed_pins.emplace_back(FramedPinKey{string_field(subject, "key")},
                                      PinKey{string_field(subject, "pin")},
                                      PinKey{string_field(subject, "reference")});
    }
    for (const auto &subject : array_field(contract, "relations")) {
        spec.relations.emplace_back(RelationKey{string_field(subject, "key")},
                                    PinKey{string_field(subject, "from")},
                                    PinKey{string_field(subject, "to")});
    }
    for (const auto &subject : array_field(contract, "supply_domains")) {
        spec.supply_domains.emplace_back(SupplyDomainKey{string_field(subject, "key")},
                                         pin_keys(field(subject, "positive_pins")),
                                         pin_keys(field(subject, "return_pins")));
    }
    for (const auto &schema : array_field(contract, "feature_schemas")) {
        auto roles = std::vector<FeatureRole>{};
        for (const auto &role : array_field(schema, "roles")) {
            roles.emplace_back(FeatureRoleKey{string_field(role, "key")},
                               feature_role_cardinality(string_field(role, "cardinality")));
        }
        auto requirements = std::vector<CanonicalRecordRequirement>{};
        for (const auto &requirement : array_field(schema, "required_records")) {
            requirements.push_back(CanonicalRecordRequirement{
                canonical_observable(string_field(requirement, "observable")),
                canonical_meaning(string_field(requirement, "meaning"))});
        }
        spec.feature_schemas.emplace_back(
            FeatureSchemaKey{string_field(schema, "key")},
            component_subject_kind(string_field(schema, "subject_kind")), std::move(roles),
            std::move(requirements));
    }
    for (const auto &binding : array_field(contract, "feature_bindings")) {
        const auto &subject = field(binding, "subject");
        const auto kind = component_subject_kind(string_field(subject, "kind"));
        const auto subject_key = string_field(subject, "key");
        auto subject_ref = [&]() {
            switch (kind) {
            case ElectricalSubjectKind::FramedPin:
                return ComponentSubjectRef::framed_pin(FramedPinKey{subject_key});
            case ElectricalSubjectKind::DirectedRelation:
                return ComponentSubjectRef::directed_relation(RelationKey{subject_key});
            case ElectricalSubjectKind::SupplyDomain:
                return ComponentSubjectRef::supply_domain(SupplyDomainKey{subject_key});
            }
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Unhandled component-contract subject kind"};
        }();
        auto roles = std::vector<FeatureRoleBinding>{};
        for (const auto &role : array_field(binding, "roles")) {
            roles.emplace_back(FeatureRoleKey{string_field(role, "role")},
                               pin_keys(field(role, "pins")));
        }
        spec.feature_bindings.emplace_back(FeatureKey{string_field(binding, "key")},
                                           FeatureSchemaKey{string_field(binding, "schema")},
                                           std::move(subject_ref), std::move(roles));
    }
    return ParsedComponentContract{std::move(spec),
                                   ContentHash{string_field(contract, "content_identity")}};
}

void LogicalCircuitParser::read_pin_definitions() {
    auto seen = std::set<std::string>{};
    for (const auto &pin : array_field(document_, "pin_definitions")) {
        const auto id = local_id<PinDefId>(pin, seen);
        require(pin.find("role") == pin.end(),
                "Pin definition role is not supported; use canonical electrical fields");
        const auto connection = connection_requirement(string_field(pin, "connection_requirement"));
        const auto terminal =
            electrical_terminal_kind(optional_string_field(pin, "terminal_kind", "Unspecified"));
        const auto direction =
            electrical_direction(optional_string_field(pin, "direction", "Unspecified"));
        const auto signal_domain =
            electrical_signal_domain(optional_string_field(pin, "signal_domain", "Unspecified"));
        const auto drive =
            electrical_drive_kind(optional_string_field(pin, "drive_kind", "Unspecified"));
        const auto polarity = electrical_polarity(optional_string_field(pin, "polarity", "None"));
        auto attributes = electrical_attributes(pin, ElectricalAttributeOwner::PinSpec,
                                                ElectricalAttributeKind::Constraint);
        const auto pin_definition_id = PinDefId{plan_.connectivity.pin_definitions.size()};
        plan_.connectivity.pin_definitions.push_back(RestoredPinDefinition{
            PinDefinition{string_field(pin, "name"), string_field(pin, "number"), connection,
                          terminal, direction, signal_domain, drive, polarity, attributes},
            std::move(attributes),
        });
        pin_def_ids_.emplace(id, pin_definition_id);
    }
}

void LogicalCircuitParser::read_component_definitions() {
    auto seen = std::set<std::string>{};
    pin_definition_owners_.assign(plan_.connectivity.pin_definitions.size(), std::nullopt);
    for (const auto &definition : array_field(document_, "component_definitions")) {
        const auto id = local_id<ComponentDefId>(definition, seen);
        auto pins = std::vector<PinDefId>{};
        for (const auto &pin : array_field(definition, "pins")) {
            require(pin.is_string(), "Component definition pin reference must be a string");
            const auto pin_definition = resolve(pin_def_ids_, pin.get<std::string>());
            if (std::find(pins.begin(), pins.end(), pin_definition) != pins.end()) {
                throw KernelArgumentError{
                    ErrorCode::InvalidArgument,
                    "Component definition contains a duplicate pin definition"};
            }
            if (pin_definition_owners_[pin_definition.index()].has_value()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Pin definition already belongs to a component definition",
                                       EntityRef::pin_def(pin_definition)};
            }
            pins.push_back(pin_definition);
        }
        const auto component_definition =
            ComponentDefId{plan_.connectivity.component_definitions.size()};
        auto pin_contents = std::vector<PinDefinition>{};
        pin_contents.reserve(pins.size());
        for (const auto pin : pins) {
            pin_contents.push_back(plan_.connectivity.pin_definitions.at(pin.index()).definition);
        }
        auto parsed_contract = component_contract(definition);
        const auto expected_identity = parsed_contract.has_value()
                                           ? std::optional{parsed_contract->content_identity}
                                           : std::nullopt;
        auto restored_definition = ComponentDefinition::make(
            string_field(definition, "name"), pin_contents, pins,
            properties(field(definition, "properties")), definition_source(definition),
            schematic_symbol_references(definition),
            parsed_contract.has_value()
                ? std::optional<ComponentContractSpec>{std::move(parsed_contract->spec)}
                : std::nullopt);
        if (expected_identity.has_value() &&
            restored_definition.content_identity() != *expected_identity) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Component contract content identity does not match content"};
        }
        plan_.connectivity.component_definitions.push_back(
            RestoredComponentDefinition{std::move(restored_definition)});
        for (const auto pin : pins) {
            pin_definition_owners_[pin.index()] = component_definition;
        }
        component_def_ids_.emplace(id, component_definition);
    }
}

void LogicalCircuitParser::read_components() {
    auto seen = std::set<std::string>{};
    auto references = std::set<std::string>{};
    for (const auto &component : array_field(document_, "components")) {
        const auto id = local_id<ComponentId>(component, seen);
        const auto definition = resolve(component_def_ids_, string_field(component, "definition"));
        auto reference = ReferenceDesignator{string_field(component, "reference")};
        if (!references.insert(reference.value()).second) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Component reference designator already exists"};
        }
        const auto component_id = ComponentId{plan_.connectivity.components.size()};
        plan_.connectivity.components.push_back(RestoredComponentInstance{
            ComponentInstance{definition, std::move(reference),
                              properties(field(component, "properties"))},
            electrical_attributes(component, ElectricalAttributeOwner::ComponentInstance,
                                  ElectricalAttributeKind::DesignInput),
        });
        component_ids_.emplace(id, component_id);
        component_reference_ids_.emplace(
            plan_.connectivity.components.back().instance.reference().value(), component_id);
        if (const auto it = component.find("selected_physical_part"); it != component.end()) {
            plan_.selected_physical_parts.push_back(RestoredSelectedPhysicalPart{
                component_id,
                physical_part(*it),
                electrical_attributes(*it, ElectricalAttributeOwner::SelectedPart,
                                      ElectricalAttributeKind::DesignInput),
            });
        }
    }
}

void LogicalCircuitParser::read_pins() {
    auto seen = std::set<std::string>{};
    auto definitions_by_component =
        std::vector<std::vector<PinDefId>>(plan_.connectivity.components.size());
    for (const auto &pin : array_field(document_, "pins")) {
        const auto id = local_id<PinId>(pin, seen);
        const auto component = resolve(component_ids_, string_field(pin, "component"));
        const auto definition = resolve(pin_def_ids_, string_field(pin, "definition"));
        const auto &definition_pins = plan_.connectivity.component_definitions
                                          .at(plan_.connectivity.components.at(component.index())
                                                  .instance.definition()
                                                  .index())
                                          .definition.pins();
        if (std::find(definition_pins.begin(), definition_pins.end(), definition) ==
            definition_pins.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Pin definition does not belong to component definition"};
        }
        auto &materialized = definitions_by_component[component.index()];
        if (std::find(materialized.begin(), materialized.end(), definition) != materialized.end()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Component pin definition is already materialized"};
        }
        materialized.push_back(definition);
        const auto pin_id = PinId{plan_.connectivity.pins.size()};
        plan_.connectivity.pins.emplace_back(component, definition);
        pin_ids_.emplace(id, pin_id);
    }

    for (std::size_t index = 0; index < plan_.connectivity.components.size(); ++index) {
        const auto definition = plan_.connectivity.components[index].instance.definition();
        const auto &required =
            plan_.connectivity.component_definitions.at(definition.index()).definition.pins();
        const auto &materialized = definitions_by_component[index];
        require(materialized.size() == required.size() &&
                    std::all_of(required.begin(), required.end(),
                                [&](const auto pin) {
                                    return std::find(materialized.begin(), materialized.end(),
                                                     pin) != materialized.end();
                                }),
                "Concrete component pin set does not match component definition");
    }
}

void LogicalCircuitParser::read_nets() {
    auto seen = std::set<std::string>{};
    for (const auto &net_object : array_field(document_, "nets")) {
        const auto id = local_id<NetId>(net_object, seen);
        auto net = Net{NetName{string_field(net_object, "name")},
                       net_kind(string_field(net_object, "kind"))};
        for (const auto &pin : array_field(net_object, "pins")) {
            require(pin.is_string(), "Net pin reference must be a string");
            require(net.connect(resolve(pin_ids_, pin.get<std::string>())),
                    "Net contains a duplicate pin reference");
        }
        const auto net_id = NetId{plan_.nets.size()};
        net_ids_.emplace(id, net_id);
        plan_.nets.push_back(RestoredNet{
            net_id,
            std::move(net),
            electrical_attributes(net_object, ElectricalAttributeOwner::Net,
                                  ElectricalAttributeKind::DesignInput),
        });
    }
}

[[nodiscard]] NetClassLayerScope net_class_layer_scope(const std::string &value) {
    if (value == "AnyCopper") {
        return NetClassLayerScope::AnyCopper;
    }
    if (value == "OuterOnly") {
        return NetClassLayerScope::OuterOnly;
    }
    if (value == "InnerOnly") {
        return NetClassLayerScope::InnerOnly;
    }
    if (value == "TopOnly") {
        return NetClassLayerScope::TopOnly;
    }
    if (value == "BottomOnly") {
        return NetClassLayerScope::BottomOnly;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Unknown net class layer scope: " + value};
}

void LogicalCircuitParser::read_net_classes() {
    const auto it = document_.find("net_classes");
    if (it == document_.end()) {
        return;
    }
    require(it->is_object(), "Net classes must be an object");

    auto seen_net_classes = std::set<std::string>{};
    for (const auto &net_class_object : array_field(*it, "classes")) {
        require(net_class_object.is_object(), "Net class must be an object");
        const auto id = local_id<NetClassId>(net_class_object, seen_net_classes);
        auto net_class = NetClass{NetClassName{string_field(net_class_object, "name")}};

        if (const auto maximum_voltage = net_class_object.find("maximum_net_voltage");
            maximum_voltage != net_class_object.end()) {
            require(maximum_voltage->is_object(),
                    "Net class maximum net voltage must be an object");
            net_class.set_maximum_net_voltage(
                Quantity{unit_dimension(string_field(*maximum_voltage, "dimension")),
                         number_field(*maximum_voltage, "value")});
        }
        if (const auto copper_clearance = net_class_object.find("copper_clearance_mm");
            copper_clearance != net_class_object.end()) {
            require(copper_clearance->is_number(), "Net class copper clearance must be a number");
            net_class.set_copper_clearance_mm(copper_clearance->get<double>());
        }
        if (const auto derived_copper_clearance = net_class_object.find("derived_copper_clearance");
            derived_copper_clearance != net_class_object.end()) {
            net_class.derive_copper_clearance(
                read_derived_net_class_rule_value(*derived_copper_clearance));
        }
        if (const auto track_width = net_class_object.find("track_width_mm");
            track_width != net_class_object.end()) {
            require(track_width->is_number(), "Net class track width must be a number");
            net_class.set_track_width_mm(track_width->get<double>());
        }
        if (const auto derived_track_width = net_class_object.find("derived_track_width");
            derived_track_width != net_class_object.end()) {
            net_class.derive_track_width(read_derived_net_class_rule_value(*derived_track_width));
        }
        const auto via_drill = net_class_object.find("via_drill_mm");
        const auto via_diameter = net_class_object.find("via_diameter_mm");
        require((via_drill == net_class_object.end()) == (via_diameter == net_class_object.end()),
                "Net class via size requires both drill and diameter");
        if (via_drill != net_class_object.end()) {
            require(via_drill->is_number() && via_diameter->is_number(),
                    "Net class via sizes must be numbers");
            net_class.set_via_size_mm(via_drill->get<double>(), via_diameter->get<double>());
        }
        if (const auto layer_scope = net_class_object.find("layer_scope");
            layer_scope != net_class_object.end()) {
            require(layer_scope->is_string(), "Net class layer scope must be a string");
            net_class.set_layer_scope(net_class_layer_scope(layer_scope->get<std::string>()));
        }
        if (const auto allowed_layers = net_class_object.find("allowed_layers");
            allowed_layers != net_class_object.end()) {
            require(allowed_layers->is_array(), "Net class allowed layers must be an array");
            auto names = std::vector<std::string>{};
            for (const auto &layer : *allowed_layers) {
                require(layer.is_string(), "Net class allowed layer must be a string");
                names.push_back(layer.get<std::string>());
            }
            net_class.set_allowed_layer_names(std::move(names));
        }
        if (const auto priority = net_class_object.find("priority");
            priority != net_class_object.end()) {
            require(priority->is_number_integer(), "Net class priority must be an integer");
            net_class.set_priority(priority->get<int>());
        }
        if (const auto default_kind = net_class_object.find("default_for_net_kind");
            default_kind != net_class_object.end()) {
            require(default_kind->is_string(), "Net class default net kind must be a string");
            net_class.set_default_for_net_kind(net_kind(default_kind->get<std::string>()));
        }

        const auto net_class_id = NetClassId{plan_.net_classes.size()};
        net_class_ids_.emplace(id, net_class_id);
        plan_.net_classes.push_back(std::move(net_class));
    }

    const auto assignments = optional_array_field(*it, "net_assignments");
    if (assignments == nullptr) {
        return;
    }

    auto seen_assignment_nets = std::set<std::string>{};
    for (const auto &assignment : *assignments) {
        require(assignment.is_object(), "Net class net assignment must be an object");
        const auto net = string_field(assignment, "net");
        if (!seen_assignment_nets.insert(net).second) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Duplicate net-class net assignment"};
        }
        plan_.net_class_assignments.push_back(RestoredNetClassAssignment{
            resolve(net_ids_, net),
            resolve(net_class_ids_, string_field(assignment, "net_class")),
        });
    }
}

void LogicalCircuitParser::read_design_intent() {
    const auto it = document_.find("design_intent");
    if (it == document_.end()) {
        return;
    }
    require(it->is_object(), "Design intent must be an object");

    auto seen_stub_nets = std::set<std::string>{};
    for (const auto &net : array_field(*it, "stub_nets")) {
        require(net.is_string(), "Stub-net design intent reference must be a string");
        const auto id = net.get<std::string>();
        if (!seen_stub_nets.insert(id).second) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Duplicate stub-net design intent"};
        }
        plan_.intentional_stub_nets.push_back(resolve(net_ids_, id));
    }

    auto seen_no_connect_pins = std::set<std::string>{};
    for (const auto &pin : array_field(*it, "no_connect_pins")) {
        require(pin.is_string(), "No-connect design intent reference must be a string");
        const auto id = pin.get<std::string>();
        if (!seen_no_connect_pins.insert(id).second) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Duplicate no-connect pin design intent"};
        }
        plan_.intentional_no_connect_pins.push_back(resolve(pin_ids_, id));
    }

    const auto component_assembly = optional_array_field(*it, "component_assembly");
    if (component_assembly == nullptr) {
        return;
    }
    auto seen_components = std::set<std::string>{};
    for (const auto &intent : *component_assembly) {
        require(intent.is_object(), "Component assembly intent must be an object");
        const auto component_id = string_field(intent, "component");
        if (!seen_components.insert(component_id).second) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Duplicate component assembly intent"};
        }
        const auto dnp = intent.find("dnp");
        const auto &selection_override = field(intent, "selection_override");
        if (dnp != intent.end()) {
            require(dnp->is_boolean(), "Component assembly DNP intent must be a boolean");
        }
        require(selection_override.is_boolean(),
                "Component assembly selection override intent must be a boolean");
        require(dnp != intent.end() || selection_override.get<bool>(),
                "Component assembly intent must include DNP or selection override intent");
        const auto component = resolve(component_ids_, component_id);
        plan_.assembly_intent.push_back(RestoredAssemblyIntent{
            component,
            dnp == intent.end() ? std::nullopt : std::optional<bool>{dnp->get<bool>()},
            selection_override.get<bool>(),
        });
    }
}

[[nodiscard]] PhysicalPart LogicalCircuitParser::physical_part(const nlohmann::json &object) const {
    require(object.is_object(), "Selected physical part must be an object");
    const auto &manufacturer_part = field(object, "manufacturer_part");
    const auto &footprint = field(object, "footprint");
    auto mappings = std::vector<PinPadMapping>{};
    for (const auto &mapping : array_field(object, "pin_pad_mappings")) {
        mappings.emplace_back(resolve(pin_def_ids_, string_field(mapping, "pin")),
                              string_field(mapping, "pad"));
    }
    auto model_3d = std::optional<PartModel3D>{};
    const auto model_it = object.find("model_3d");
    if (model_it != object.end()) {
        require(model_it->is_object(), "Selected physical part model_3d must be an object");
        const auto &translation = array_field(*model_it, "translation_mm");
        require(translation.size() == 3U,
                "Selected physical part model_3d translation must contain three numbers");
        model_3d = PartModel3D{
            string_field(*model_it, "format"), string_field(*model_it, "file_name"),
            std::array<double, 3>{translation[0].get<double>(), translation[1].get<double>(),
                                  translation[2].get<double>()},
            number_field(*model_it, "rotation_deg")};
    }
    auto alternates = std::vector<std::string>{};
    const auto alternate_it = object.find("approved_alternate_mpns");
    if (alternate_it != object.end()) {
        require(alternate_it->is_array(), "Selected physical part alternates must be an array");
        for (const auto &alternate : *alternate_it) {
            require(alternate.is_string(), "Selected physical part alternate MPN must be a string");
            alternates.push_back(alternate.get<std::string>());
        }
    }
    return PhysicalPart{
        ManufacturerPart{string_field(manufacturer_part, "manufacturer"),
                         string_field(manufacturer_part, "part_number")},
        PackageRef{string_field(object, "package")},
        FootprintRef{string_field(footprint, "library"), string_field(footprint, "name")},
        std::move(mappings),
        properties(field(object, "properties")),
        model_3d,
        std::move(alternates)};
}

} // namespace volt::io::detail
