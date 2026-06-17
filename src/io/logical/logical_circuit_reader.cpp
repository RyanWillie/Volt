#include <volt/io/logical/logical_circuit_reader.hpp>

#include <volt/circuit/connectivity/queries.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

#include "logical_circuit_reader_impl.hpp"
#include "logical_net_class_format.hpp"

namespace volt::io::detail {

[[nodiscard]] Circuit LogicalCircuitReader::read() {
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
    read_selected_physical_parts();

    return std::move(circuit_);
}

void LogicalCircuitReader::require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::logic_error{message};
    }
}

const nlohmann::json &LogicalCircuitReader::field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading logical circuit");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

std::string LogicalCircuitReader::string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

std::string LogicalCircuitReader::optional_string_field(const nlohmann::json &object,
                                                        const char *name,
                                                        std::string default_value) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return default_value;
    }
    require(it->is_string(), std::string{"Expected string field: "} + name);
    return it->get<std::string>();
}

void LogicalCircuitReader::require_format(const nlohmann::json &object) {
    const auto actual = string_field(object, "format");
    require(actual == logical_circuit_format_name(),
            "Unsupported logical circuit format: " + actual);
}

void LogicalCircuitReader::require_version(const nlohmann::json &object) {
    const auto &value = field(object, "version");
    require(value.is_number_integer(), "Expected integer field: version");
    const auto actual = value.get<std::int64_t>();
    require(actual == static_cast<std::int64_t>(logical_circuit_format_version()),
            "Unsupported logical circuit format version: " + std::to_string(actual));
}

const nlohmann::json &LogicalCircuitReader::array_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

const nlohmann::json *LogicalCircuitReader::optional_array_field(const nlohmann::json &object,
                                                                 const char *name) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return nullptr;
    }
    require(it->is_array(), std::string{"Expected array field: "} + name);
    return &*it;
}

[[nodiscard]] ConnectionRequirement
LogicalCircuitReader::connection_requirement(const std::string &value) {
    if (value == "Optional")
        return ConnectionRequirement::Optional;
    if (value == "Required")
        return ConnectionRequirement::Required;
    if (value == "MustNotConnect")
        return ConnectionRequirement::MustNotConnect;
    throw std::logic_error{"Invalid ConnectionRequirement value"};
}

[[nodiscard]] ElectricalTerminalKind
LogicalCircuitReader::electrical_terminal_kind(const std::string &value) {
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
    throw std::logic_error{"Invalid ElectricalTerminalKind value"};
}

[[nodiscard]] ElectricalDirection
LogicalCircuitReader::electrical_direction(const std::string &value) {
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
    throw std::logic_error{"Invalid ElectricalDirection value"};
}

[[nodiscard]] ElectricalSignalDomain
LogicalCircuitReader::electrical_signal_domain(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalSignalDomain::Unspecified;
    if (value == "Digital")
        return ElectricalSignalDomain::Digital;
    if (value == "Analog")
        return ElectricalSignalDomain::Analog;
    if (value == "Mixed")
        return ElectricalSignalDomain::Mixed;
    throw std::logic_error{"Invalid ElectricalSignalDomain value"};
}

[[nodiscard]] ElectricalDriveKind
LogicalCircuitReader::electrical_drive_kind(const std::string &value) {
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
    throw std::logic_error{"Invalid ElectricalDriveKind value"};
}

[[nodiscard]] ElectricalPolarity
LogicalCircuitReader::electrical_polarity(const std::string &value) {
    if (value == "None")
        return ElectricalPolarity::None;
    if (value == "ActiveHigh")
        return ElectricalPolarity::ActiveHigh;
    if (value == "ActiveLow")
        return ElectricalPolarity::ActiveLow;
    throw std::logic_error{"Invalid ElectricalPolarity value"};
}

[[nodiscard]] NetKind LogicalCircuitReader::net_kind(const std::string &value) {
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
    throw std::logic_error{"Invalid NetKind value"};
}

[[nodiscard]] PortRole LogicalCircuitReader::port_role(const std::string &value) {
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
    throw std::logic_error{"Invalid PortRole value"};
}

[[nodiscard]] UnitDimension LogicalCircuitReader::unit_dimension(const std::string &value) {
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
    throw std::logic_error{"Invalid unit dimension value"};
}

[[nodiscard]] ToleranceMode LogicalCircuitReader::tolerance_mode(const std::string &value) {
    if (value == "absolute")
        return ToleranceMode::Absolute;
    if (value == "percent")
        return ToleranceMode::Percent;
    throw std::logic_error{"Invalid tolerance mode value"};
}

[[nodiscard]] double LogicalCircuitReader::number_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    return value.get<double>();
}

[[nodiscard]] PropertyValue LogicalCircuitReader::property_value(const nlohmann::json &object) {
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
    throw std::logic_error{"Invalid property value type"};
}

[[nodiscard]] PropertyMap LogicalCircuitReader::properties(const nlohmann::json &object) {
    require(object.is_object(), "Properties must be an object");
    auto result = PropertyMap{};
    for (const auto &[key, value] : object.items()) {
        result.set(PropertyKey{key}, property_value(value));
    }
    return result;
}

[[nodiscard]] ElectricalAttributeValue
LogicalCircuitReader::electrical_attribute_value(const nlohmann::json &object) {
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
    throw std::logic_error{"Invalid electrical attribute value type"};
}

void LogicalCircuitReader::read_component_electrical_attributes(const nlohmann::json &object,
                                                                ComponentId component,
                                                                ElectricalAttributeOwner owner) {
    const auto it = object.find("electrical_attributes");
    if (it == object.end()) {
        return;
    }
    require(it->is_object(), "Electrical attributes must be an object");
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        const auto spec =
            ElectricalAttributeSpec{ElectricalAttributeName{name}, owner,
                                    ElectricalAttributeKind::DesignInput, attribute.dimension()};
        if (owner == ElectricalAttributeOwner::ComponentInstance) {
            circuit_.set_component_electrical_attribute(component, spec, attribute);
        } else if (owner == ElectricalAttributeOwner::SelectedPart) {
            circuit_.set_selected_part_electrical_attribute(component, spec, attribute);
        } else {
            throw std::logic_error{"Unsupported electrical attribute owner while reading"};
        }
    }
}

void LogicalCircuitReader::read_net_electrical_attributes(const nlohmann::json &object, NetId net) {
    const auto it = object.find("electrical_attributes");
    if (it == object.end()) {
        return;
    }
    require(it->is_object(), "Electrical attributes must be an object");
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        circuit_.set_net_electrical_attribute(
            net,
            ElectricalAttributeSpec{ElectricalAttributeName{name}, ElectricalAttributeOwner::Net,
                                    ElectricalAttributeKind::DesignInput, attribute.dimension()},
            attribute);
    }
}

void LogicalCircuitReader::read_pin_definition_electrical_attributes(const nlohmann::json &object,
                                                                     PinDefId pin_definition) {
    const auto it = object.find("electrical_attributes");
    if (it == object.end()) {
        return;
    }
    require(it->is_object(), "Electrical attributes must be an object");
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        circuit_.set_pin_definition_electrical_attribute(
            pin_definition,
            ElectricalAttributeSpec{ElectricalAttributeName{name},
                                    ElectricalAttributeOwner::PinSpec,
                                    ElectricalAttributeKind::Constraint, attribute.dimension()},
            attribute);
    }
}

[[nodiscard]] std::optional<DefinitionSource>
LogicalCircuitReader::definition_source(const nlohmann::json &object) {
    const auto it = object.find("source");
    if (it == object.end()) {
        return std::nullopt;
    }
    require(it->is_object(), "Definition source must be an object");
    return DefinitionSource{string_field(*it, "namespace"), string_field(*it, "name"),
                            string_field(*it, "version")};
}

[[nodiscard]] std::vector<SchematicSymbolReference>
LogicalCircuitReader::schematic_symbol_references(const nlohmann::json &object) {
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

void LogicalCircuitReader::read_pin_definitions() {
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
        const auto pin_definition_id = circuit_.add_pin_definition(
            PinDefinition{string_field(pin, "name"), string_field(pin, "number"), connection,
                          terminal, direction, signal_domain, drive, polarity});
        pin_def_ids_.emplace(id, pin_definition_id);
        read_pin_definition_electrical_attributes(pin, pin_definition_id);
    }
}

void LogicalCircuitReader::read_component_definitions() {
    auto seen = std::set<std::string>{};
    for (const auto &definition : array_field(document_, "component_definitions")) {
        const auto id = local_id<ComponentDefId>(definition, seen);
        auto pins = std::vector<PinDefId>{};
        for (const auto &pin : array_field(definition, "pins")) {
            require(pin.is_string(), "Component definition pin reference must be a string");
            pins.push_back(resolve(pin_def_ids_, pin.get<std::string>()));
        }
        component_def_ids_.emplace(
            id, circuit_.add_component_definition(ComponentDefinition{
                    string_field(definition, "name"), std::move(pins),
                    properties(field(definition, "properties")), definition_source(definition),
                    schematic_symbol_references(definition)}));
    }
}

void LogicalCircuitReader::read_components() {
    auto seen = std::set<std::string>{};
    for (const auto &component : array_field(document_, "components")) {
        const auto id = local_id<ComponentId>(component, seen);
        const auto definition = resolve(component_def_ids_, string_field(component, "definition"));
        const auto component_id = circuit_.add_component(
            ComponentInstance{definition, ReferenceDesignator{string_field(component, "reference")},
                              properties(field(component, "properties"))});
        component_ids_.emplace(id, component_id);
        read_component_electrical_attributes(component, component_id,
                                             ElectricalAttributeOwner::ComponentInstance);
        if (const auto it = component.find("selected_physical_part"); it != component.end()) {
            selected_parts_.emplace_back(id, *it);
        }
    }
}

void LogicalCircuitReader::read_pins() {
    auto seen = std::set<std::string>{};
    for (const auto &pin : array_field(document_, "pins")) {
        const auto id = local_id<PinId>(pin, seen);
        const auto component = resolve(component_ids_, string_field(pin, "component"));
        const auto definition = resolve(pin_def_ids_, string_field(pin, "definition"));
        const auto &definition_pins =
            circuit_.component_definition(circuit_.component(component).definition()).pins();
        require(std::find(definition_pins.begin(), definition_pins.end(), definition) !=
                    definition_pins.end(),
                "Concrete pin definition is not part of its component definition");
        pin_ids_.emplace(id, circuit_.add_pin(PinInstance{component, definition}));
    }
}

void LogicalCircuitReader::read_nets() {
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
        const auto net_id = circuit_.add_net(std::move(net));
        net_ids_.emplace(id, net_id);
        read_net_electrical_attributes(net_object, net_id);
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
    throw std::logic_error{"Unknown net class layer scope: " + value};
}

void LogicalCircuitReader::read_net_classes() {
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

        net_class_ids_.emplace(id, circuit_.add_net_class(std::move(net_class)));
    }

    const auto assignments = optional_array_field(*it, "net_assignments");
    if (assignments == nullptr) {
        return;
    }

    auto seen_assignment_nets = std::set<std::string>{};
    for (const auto &assignment : *assignments) {
        require(assignment.is_object(), "Net class net assignment must be an object");
        const auto net = string_field(assignment, "net");
        require(seen_assignment_nets.insert(net).second, "Duplicate net-class net assignment");
        [[maybe_unused]] const auto changed = circuit_.assign_net_class(
            resolve(net_ids_, net), resolve(net_class_ids_, string_field(assignment, "net_class")));
    }
}

void LogicalCircuitReader::read_design_intent() {
    const auto it = document_.find("design_intent");
    if (it == document_.end()) {
        return;
    }
    require(it->is_object(), "Design intent must be an object");

    auto seen_stub_nets = std::set<std::string>{};
    for (const auto &net : array_field(*it, "stub_nets")) {
        require(net.is_string(), "Stub-net design intent reference must be a string");
        const auto id = net.get<std::string>();
        require(seen_stub_nets.insert(id).second, "Duplicate stub-net design intent");
        [[maybe_unused]] const auto changed =
            circuit_.mark_intentional_stub_net(resolve(net_ids_, id));
    }

    auto seen_no_connect_pins = std::set<std::string>{};
    for (const auto &pin : array_field(*it, "no_connect_pins")) {
        require(pin.is_string(), "No-connect design intent reference must be a string");
        const auto id = pin.get<std::string>();
        require(seen_no_connect_pins.insert(id).second, "Duplicate no-connect pin design intent");
        [[maybe_unused]] const auto changed =
            circuit_.mark_intentional_no_connect_pin(resolve(pin_ids_, id));
    }

    const auto component_assembly = optional_array_field(*it, "component_assembly");
    if (component_assembly == nullptr) {
        return;
    }
    auto seen_components = std::set<std::string>{};
    for (const auto &intent : *component_assembly) {
        require(intent.is_object(), "Component assembly intent must be an object");
        const auto component_id = string_field(intent, "component");
        require(seen_components.insert(component_id).second, "Duplicate component assembly intent");
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
        if (dnp != intent.end()) {
            circuit_.set_component_dnp(component, dnp->get<bool>());
        }
        circuit_.set_component_selection_override(component, selection_override.get<bool>());
    }
}

void LogicalCircuitReader::read_module_definitions() {
    const auto modules = optional_array_field(document_, "module_definitions");
    if (modules == nullptr) {
        return;
    }

    auto seen = std::set<std::string>{};
    auto seen_template_nets = std::set<std::string>{};
    auto seen_module_components = std::set<std::string>{};
    auto seen_ports = std::set<std::string>{};
    for (const auto &module_object : *modules) {
        const auto id = local_id<ModuleDefId>(module_object, seen);
        const auto module = circuit_.add_module_definition(
            ModuleDefinition{ModuleName{string_field(module_object, "name")}});
        module_def_ids_.emplace(id, module);

        for (const auto &net_object : array_field(module_object, "local_nets")) {
            const auto net_id = local_id<TemplateNetDefId>(net_object, seen_template_nets);
            const auto template_net = circuit_.add_template_net(
                module, TemplateNetDefinition{NetName{string_field(net_object, "name")},
                                              net_kind(string_field(net_object, "kind"))});
            template_net_ids_.emplace(net_id, template_net);
        }

        if (const auto components = optional_array_field(module_object, "components")) {
            for (const auto &component_object : *components) {
                const auto component_id =
                    local_id<ModuleComponentId>(component_object, seen_module_components);
                const auto component = circuit_.add_module_component(
                    module,
                    ModuleComponentTemplate{
                        resolve(component_def_ids_, string_field(component_object, "definition")),
                        ReferenceDesignator{string_field(component_object, "reference")},
                        properties(field(component_object, "properties"))});
                module_component_ids_.emplace(component_id, component);
            }
        }

        if (const auto connections = optional_array_field(module_object, "connections")) {
            for (const auto &connection_object : *connections) {
                [[maybe_unused]] const auto changed = circuit_.connect_module_pin(
                    module, resolve(template_net_ids_, string_field(connection_object, "net")),
                    resolve(module_component_ids_, string_field(connection_object, "component")),
                    resolve(pin_def_ids_, string_field(connection_object, "pin")));
            }
        }

        for (const auto &port_object : array_field(module_object, "ports")) {
            const auto port_id = local_id<PortDefId>(port_object, seen_ports);
            const auto internal_net =
                resolve(template_net_ids_, string_field(port_object, "internal_net"));
            const auto required_it = port_object.find("required");
            auto required = true;
            if (required_it != port_object.end()) {
                require(required_it->is_boolean(), "Expected boolean field: required");
                required = required_it->get<bool>();
            }
            const auto port = circuit_.add_port_definition(
                module,
                PortDefinition{PortName{string_field(port_object, "name")}, internal_net,
                               port_role(optional_string_field(port_object, "role", "Passive")),
                               required});
            port_def_ids_.emplace(port_id, port);
        }
    }
}

void LogicalCircuitReader::read_module_instances() {
    const auto modules = optional_array_field(document_, "module_instances");
    if (modules == nullptr) {
        return;
    }

    auto seen = std::set<std::string>{};
    for (const auto &instance_object : *modules) {
        const auto id = local_id<ModuleInstanceId>(instance_object, seen);
        const auto definition =
            resolve(module_def_ids_, string_field(instance_object, "definition"));
        auto origins = std::vector<std::pair<TemplateNetDefId, NetId>>{};
        for (const auto &origin_object : array_field(instance_object, "net_origins")) {
            origins.emplace_back(
                resolve(template_net_ids_, string_field(origin_object, "template_net")),
                resolve(net_ids_, string_field(origin_object, "net")));
        }
        auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
        if (const auto components = optional_array_field(instance_object, "component_origins")) {
            for (const auto &origin_object : *components) {
                component_origins.emplace_back(
                    resolve(module_component_ids_,
                            string_field(origin_object, "template_component")),
                    resolve(component_ids_, string_field(origin_object, "component")));
            }
        } else {
            component_origins = infer_component_origins(
                definition, ModuleInstanceName{string_field(instance_object, "name")});
        }
        const auto instance = circuit_.restore_root_module_instance(
            definition, ModuleInstanceName{string_field(instance_object, "name")}, origins,
            component_origins);
        module_instance_ids_.emplace(id, instance);

        for (const auto &binding_object : array_field(instance_object, "port_bindings")) {
            const auto port = resolve(port_def_ids_, string_field(binding_object, "port"));
            const auto parent_net = resolve(net_ids_, string_field(binding_object, "parent_net"));
            [[maybe_unused]] const auto binding = circuit_.bind_port(instance, port, parent_net);
        }
    }
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
LogicalCircuitReader::infer_component_origins(ModuleDefId definition,
                                              const ModuleInstanceName &name) const {
    auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    for (const auto component : circuit_.module_definition(definition).components()) {
        const auto &template_component = circuit_.module_component_template(component);
        const auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + template_component.reference().value()};
        const auto concrete_component =
            queries::component_by_reference(circuit_, concrete_reference);
        require(concrete_component.has_value(),
                "Missing module instance concrete component for inferred component origin");
        component_origins.emplace_back(component, concrete_component.value());
    }
    return component_origins;
}

[[nodiscard]] PhysicalPart LogicalCircuitReader::physical_part(const nlohmann::json &object) const {
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

void LogicalCircuitReader::read_selected_physical_parts() {
    for (const auto &[component_id, part] : selected_parts_) {
        const auto component = resolve(component_ids_, component_id);
        circuit_.select_physical_part(component, physical_part(part));
        read_component_electrical_attributes(part, component,
                                             ElectricalAttributeOwner::SelectedPart);
    }
}

} // namespace volt::io::detail

namespace {

[[nodiscard]] volt::Circuit read_logical_circuit_document(const nlohmann::json &document) {
    return volt::io::detail::LogicalCircuitReader{document}.read();
}

} // namespace

namespace volt::io {

[[nodiscard]] Circuit read_logical_circuit_text(std::string_view text) {
    return read_logical_circuit_document(nlohmann::json::parse(text.begin(), text.end()));
}

[[nodiscard]] Circuit read_logical_circuit(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_logical_circuit_text(buffer.str());
}

} // namespace volt::io
