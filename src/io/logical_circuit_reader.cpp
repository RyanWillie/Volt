#include <volt/io/logical_circuit_reader.hpp>

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

#include <volt/circuit/circuit_view.hpp>
#include <volt/circuit/design_intent_mutations.hpp>
#include <volt/circuit/electrical_mutations.hpp>
#include <volt/circuit/hierarchy_mutations.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical_circuit_writer.hpp>

namespace volt::io::detail {

/** Internal implementation for loading the v1 logical circuit JSON format. */
class LogicalCircuitReader {
  public:
    /** Construct a reader over a parsed JSON document. */
    explicit LogicalCircuitReader(const nlohmann::json &document) : document_{document} {}

    /** Load and structurally validate the document into a Circuit. */
    [[nodiscard]] Circuit read();

  private:
    static void require(bool condition, const std::string &message);

    static const nlohmann::json &field(const nlohmann::json &object, const char *name);

    static std::string string_field(const nlohmann::json &object, const char *name);

    static std::string optional_string_field(const nlohmann::json &object, const char *name,
                                             std::string default_value);

    static void require_format(const nlohmann::json &object);

    static void require_version(const nlohmann::json &object);

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name);

    static const nlohmann::json *optional_array_field(const nlohmann::json &object,
                                                      const char *name);

    template <typename Id>
    static std::string local_id(const nlohmann::json &object, std::set<std::string> &seen) {
        const auto id = string_field(object, "id");
        static_cast<void>(decode_local_id<Id>(id));
        require(seen.insert(id).second, "Duplicate local ID");
        return id;
    }

    [[nodiscard]] static PinRole pin_role(const std::string &value);

    [[nodiscard]] static ConnectionRequirement connection_requirement(const std::string &value);

    [[nodiscard]] static ElectricalTerminalKind electrical_terminal_kind(const std::string &value);

    [[nodiscard]] static ElectricalDirection electrical_direction(const std::string &value);

    [[nodiscard]] static ElectricalSignalDomain electrical_signal_domain(const std::string &value);

    [[nodiscard]] static ElectricalDriveKind electrical_drive_kind(const std::string &value);

    [[nodiscard]] static ElectricalPolarity electrical_polarity(const std::string &value);

    [[nodiscard]] static NetKind net_kind(const std::string &value);

    [[nodiscard]] static PortRole port_role(const std::string &value);

    [[nodiscard]] static UnitDimension unit_dimension(const std::string &value);

    [[nodiscard]] static ToleranceMode tolerance_mode(const std::string &value);

    [[nodiscard]] static double number_field(const nlohmann::json &object, const char *name);

    [[nodiscard]] static PropertyValue property_value(const nlohmann::json &object);

    [[nodiscard]] static PropertyMap properties(const nlohmann::json &object);

    [[nodiscard]] static ElectricalAttributeValue
    electrical_attribute_value(const nlohmann::json &object);

    void read_component_electrical_attributes(const nlohmann::json &object, ComponentId component,
                                              ElectricalAttributeOwner owner);

    void read_net_electrical_attributes(const nlohmann::json &object, NetId net);

    void read_pin_definition_electrical_attributes(const nlohmann::json &object,
                                                   PinDefId pin_definition);

    [[nodiscard]] static std::optional<DefinitionSource>
    definition_source(const nlohmann::json &object);

    [[nodiscard]] static std::vector<SchematicSymbolReference>
    schematic_symbol_references(const nlohmann::json &object);

    template <typename Id>
    [[nodiscard]] Id resolve(const std::map<std::string, Id> &ids, const std::string &id) const {
        const auto it = ids.find(id);
        require(it != ids.end(), "Reference points to a missing local ID");
        return it->second;
    }

    void read_pin_definitions();

    void read_component_definitions();

    void read_components();

    void read_pins();

    void read_nets();

    void read_design_intent();

    void read_module_definitions();

    void read_module_instances();

    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    infer_component_origins(ModuleDefId definition, const ModuleInstanceName &name) const;

    [[nodiscard]] PhysicalPart physical_part(const nlohmann::json &object) const;

    void read_selected_physical_parts();

    const nlohmann::json &document_;
    Circuit circuit_;
    std::map<std::string, PinDefId> pin_def_ids_;
    std::map<std::string, ComponentDefId> component_def_ids_;
    std::map<std::string, ComponentId> component_ids_;
    std::map<std::string, PinId> pin_ids_;
    std::map<std::string, NetId> net_ids_;
    std::map<std::string, ModuleDefId> module_def_ids_;
    std::map<std::string, TemplateNetDefId> template_net_ids_;
    std::map<std::string, ModuleComponentId> module_component_ids_;
    std::map<std::string, PortDefId> port_def_ids_;
    std::map<std::string, ModuleInstanceId> module_instance_ids_;
    std::vector<std::pair<std::string, nlohmann::json>> selected_parts_;
};

[[nodiscard]] Circuit LogicalCircuitReader::read() {
    require(document_.is_object(), "Logical circuit document must be an object");
    require_format(document_);
    require_version(document_);

    read_pin_definitions();
    read_component_definitions();
    read_components();
    read_pins();
    read_nets();
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
[[nodiscard]] PinRole LogicalCircuitReader::pin_role(const std::string &value) {
    if (value == "Passive")
        return PinRole::Passive;
    if (value == "PowerInput")
        return PinRole::PowerInput;
    if (value == "PowerOutput")
        return PinRole::PowerOutput;
    if (value == "Ground")
        return PinRole::Ground;
    if (value == "DigitalInput")
        return PinRole::DigitalInput;
    if (value == "DigitalOutput")
        return PinRole::DigitalOutput;
    if (value == "Bidirectional")
        return PinRole::Bidirectional;
    if (value == "AnalogInput")
        return PinRole::AnalogInput;
    if (value == "AnalogOutput")
        return PinRole::AnalogOutput;
    if (value == "NoConnect")
        return PinRole::NoConnect;
    throw std::logic_error{"Invalid PinRole value"};
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
        auto electrical = CircuitElectrical{circuit_};
        if (owner == ElectricalAttributeOwner::ComponentInstance) {
            electrical.set_component_electrical_attribute(component, spec, attribute);
        } else if (owner == ElectricalAttributeOwner::SelectedPart) {
            electrical.set_selected_part_electrical_attribute(component, spec, attribute);
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
    auto electrical = CircuitElectrical{circuit_};
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        electrical.set_net_electrical_attribute(
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
    auto electrical = CircuitElectrical{circuit_};
    for (const auto &[name, value] : it->items()) {
        const auto attribute = electrical_attribute_value(value);
        electrical.set_pin_definition_electrical_attribute(
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
        const auto pin_definition_id = circuit_.add_pin_definition(PinDefinition{
            string_field(pin, "name"), string_field(pin, "number"),
            pin_role(string_field(pin, "role")),
            connection_requirement(string_field(pin, "connection_requirement")),
            electrical_terminal_kind(optional_string_field(pin, "terminal_kind", "Unspecified")),
            electrical_direction(optional_string_field(pin, "direction", "Unspecified")),
            electrical_signal_domain(optional_string_field(pin, "signal_domain", "Unspecified")),
            electrical_drive_kind(optional_string_field(pin, "drive_kind", "Unspecified")),
            electrical_polarity(optional_string_field(pin, "polarity", "None"))});
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
        const auto view = CircuitView{circuit_};
        const auto &definition_pins =
            view.component_definition(view.component(component).definition()).pins();
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
void LogicalCircuitReader::read_design_intent() {
    const auto it = document_.find("design_intent");
    if (it == document_.end()) {
        return;
    }
    require(it->is_object(), "Design intent must be an object");
    auto intent = CircuitDesignIntent{circuit_};

    auto seen_stub_nets = std::set<std::string>{};
    for (const auto &net : array_field(*it, "stub_nets")) {
        require(net.is_string(), "Stub-net design intent reference must be a string");
        const auto id = net.get<std::string>();
        require(seen_stub_nets.insert(id).second, "Duplicate stub-net design intent");
        [[maybe_unused]] const auto changed =
            intent.mark_intentional_stub_net(resolve(net_ids_, id));
    }

    auto seen_no_connect_pins = std::set<std::string>{};
    for (const auto &pin : array_field(*it, "no_connect_pins")) {
        require(pin.is_string(), "No-connect design intent reference must be a string");
        const auto id = pin.get<std::string>();
        require(seen_no_connect_pins.insert(id).second, "Duplicate no-connect pin design intent");
        [[maybe_unused]] const auto changed =
            intent.mark_intentional_no_connect_pin(resolve(pin_ids_, id));
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
    auto hierarchy = CircuitHierarchy{circuit_};
    for (const auto &module_object : *modules) {
        const auto id = local_id<ModuleDefId>(module_object, seen);
        const auto module = hierarchy.add_module_definition(
            ModuleDefinition{ModuleName{string_field(module_object, "name")}});
        module_def_ids_.emplace(id, module);

        for (const auto &net_object : array_field(module_object, "local_nets")) {
            const auto net_id = local_id<TemplateNetDefId>(net_object, seen_template_nets);
            const auto template_net = hierarchy.add_template_net(
                module, TemplateNetDefinition{NetName{string_field(net_object, "name")},
                                              net_kind(string_field(net_object, "kind"))});
            template_net_ids_.emplace(net_id, template_net);
        }

        if (const auto components = optional_array_field(module_object, "components")) {
            for (const auto &component_object : *components) {
                const auto component_id =
                    local_id<ModuleComponentId>(component_object, seen_module_components);
                const auto component = hierarchy.add_module_component(
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
                [[maybe_unused]] const auto changed = hierarchy.connect_module_pin(
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
            const auto port = hierarchy.add_port_definition(
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
    auto hierarchy = CircuitHierarchy{circuit_};
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
        const auto instance = hierarchy.restore_root_module_instance(
            definition, ModuleInstanceName{string_field(instance_object, "name")}, origins,
            component_origins);
        module_instance_ids_.emplace(id, instance);

        for (const auto &binding_object : array_field(instance_object, "port_bindings")) {
            const auto port = resolve(port_def_ids_, string_field(binding_object, "port"));
            const auto parent_net = resolve(net_ids_, string_field(binding_object, "parent_net"));
            [[maybe_unused]] const auto binding = hierarchy.bind_port(instance, port, parent_net);
        }
    }
}
[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
LogicalCircuitReader::infer_component_origins(ModuleDefId definition,
                                              const ModuleInstanceName &name) const {
    auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    const auto view = CircuitView{circuit_};
    for (const auto component : view.module_definition(definition).components()) {
        const auto &template_component = view.module_component_template(component);
        const auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + template_component.reference().value()};
        const auto concrete_component = view.component_by_reference(concrete_reference);
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
    return PhysicalPart{
        ManufacturerPart{string_field(manufacturer_part, "manufacturer"),
                         string_field(manufacturer_part, "part_number")},
        PackageRef{string_field(object, "package")},
        FootprintRef{string_field(footprint, "library"), string_field(footprint, "name")},
        std::move(mappings), properties(field(object, "properties"))};
}
void LogicalCircuitReader::read_selected_physical_parts() {
    auto electrical = CircuitElectrical{circuit_};
    for (const auto &[component_id, part] : selected_parts_) {
        const auto component = resolve(component_ids_, component_id);
        electrical.select_physical_part(component, physical_part(part));
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
