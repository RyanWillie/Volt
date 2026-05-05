#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/logical_circuit_writer.hpp>

namespace volt::io {

namespace detail {

/** Internal implementation for loading the v1 logical circuit JSON format. */
class LogicalCircuitReader {
  public:
    /** Construct a reader over a parsed JSON document. */
    explicit LogicalCircuitReader(const nlohmann::json &document) : document_{document} {}

    /** Load and structurally validate the document into a Circuit. */
    [[nodiscard]] Circuit read() {
        require(document_.is_object(), "Logical circuit document must be an object");
        require_format(document_);
        require_version(document_);

        read_pin_definitions();
        read_component_definitions();
        read_components();
        read_pins();
        read_nets();
        read_selected_physical_parts();

        return std::move(circuit_);
    }

  private:
    static void require(bool condition, const std::string &message) {
        if (!condition) {
            throw std::logic_error{message};
        }
    }

    static const nlohmann::json &field(const nlohmann::json &object, const char *name) {
        require(object.is_object(), "Expected object while reading logical circuit");
        const auto it = object.find(name);
        require(it != object.end(), std::string{"Missing required field: "} + name);
        return *it;
    }

    static std::string string_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_string(), std::string{"Expected string field: "} + name);
        return value.get<std::string>();
    }

    static void require_format(const nlohmann::json &object) {
        const auto actual = string_field(object, "format");
        require(actual == logical_circuit_format_name(),
                "Unsupported logical circuit format: " + actual);
    }

    static void require_version(const nlohmann::json &object) {
        const auto &value = field(object, "version");
        require(value.is_number_integer(), "Expected integer field: version");
        const auto actual = value.get<std::int64_t>();
        require(actual == static_cast<std::int64_t>(logical_circuit_format_version()),
                "Unsupported logical circuit format version: " + std::to_string(actual));
    }

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_array(), std::string{"Expected array field: "} + name);
        return value;
    }

    static std::string local_id(const nlohmann::json &object, const std::string &prefix,
                                std::set<std::string> &seen) {
        const auto id = string_field(object, "id");
        require(id.rfind(prefix, 0) == 0, "Local ID has the wrong typed prefix");
        require(seen.insert(id).second, "Duplicate local ID");
        return id;
    }

    [[nodiscard]] static PinRole pin_role(const std::string &value) {
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

    [[nodiscard]] static ConnectionRequirement connection_requirement(const std::string &value) {
        if (value == "Optional")
            return ConnectionRequirement::Optional;
        if (value == "Required")
            return ConnectionRequirement::Required;
        if (value == "MustNotConnect")
            return ConnectionRequirement::MustNotConnect;
        throw std::logic_error{"Invalid ConnectionRequirement value"};
    }

    [[nodiscard]] static NetKind net_kind(const std::string &value) {
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

    [[nodiscard]] static PropertyValue property_value(const nlohmann::json &object) {
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

    [[nodiscard]] static PropertyMap properties(const nlohmann::json &object) {
        require(object.is_object(), "Properties must be an object");
        auto result = PropertyMap{};
        for (const auto &[key, value] : object.items()) {
            result.set(PropertyKey{key}, property_value(value));
        }
        return result;
    }

    [[nodiscard]] static std::optional<DefinitionSource>
    definition_source(const nlohmann::json &object) {
        const auto it = object.find("source");
        if (it == object.end()) {
            return std::nullopt;
        }
        require(it->is_object(), "Definition source must be an object");
        return DefinitionSource{string_field(*it, "namespace"), string_field(*it, "name"),
                                string_field(*it, "version")};
    }

    template <typename Id>
    [[nodiscard]] Id resolve(const std::map<std::string, Id> &ids, const std::string &id) const {
        const auto it = ids.find(id);
        require(it != ids.end(), "Reference points to a missing local ID");
        return it->second;
    }

    void read_pin_definitions() {
        auto seen = std::set<std::string>{};
        for (const auto &pin : array_field(document_, "pin_definitions")) {
            const auto id = local_id(pin, "pin_def:", seen);
            pin_def_ids_.emplace(
                id, circuit_.add_pin_definition(PinDefinition{
                        string_field(pin, "name"), string_field(pin, "number"),
                        pin_role(string_field(pin, "role")),
                        connection_requirement(string_field(pin, "connection_requirement"))}));
        }
    }

    void read_component_definitions() {
        auto seen = std::set<std::string>{};
        for (const auto &definition : array_field(document_, "component_definitions")) {
            const auto id = local_id(definition, "component_def:", seen);
            auto pins = std::vector<PinDefId>{};
            for (const auto &pin : array_field(definition, "pins")) {
                require(pin.is_string(), "Component definition pin reference must be a string");
                pins.push_back(resolve(pin_def_ids_, pin.get<std::string>()));
            }
            component_def_ids_.emplace(id, circuit_.add_component_definition(ComponentDefinition{
                                               string_field(definition, "name"), std::move(pins),
                                               properties(field(definition, "properties")),
                                               definition_source(definition)}));
        }
    }

    void read_components() {
        auto seen = std::set<std::string>{};
        for (const auto &component : array_field(document_, "components")) {
            const auto id = local_id(component, "component:", seen);
            const auto definition =
                resolve(component_def_ids_, string_field(component, "definition"));
            component_ids_.emplace(
                id, circuit_.add_component(ComponentInstance{
                        definition, ReferenceDesignator{string_field(component, "reference")},
                        properties(field(component, "properties"))}));
            if (const auto it = component.find("selected_physical_part"); it != component.end()) {
                selected_parts_.emplace_back(id, *it);
            }
        }
    }

    void read_pins() {
        auto seen = std::set<std::string>{};
        for (const auto &pin : array_field(document_, "pins")) {
            const auto id = local_id(pin, "pin:", seen);
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

    void read_nets() {
        auto seen = std::set<std::string>{};
        for (const auto &net_object : array_field(document_, "nets")) {
            const auto id = local_id(net_object, "net:", seen);
            auto net = Net{NetName{string_field(net_object, "name")},
                           net_kind(string_field(net_object, "kind"))};
            for (const auto &pin : array_field(net_object, "pins")) {
                require(pin.is_string(), "Net pin reference must be a string");
                require(net.connect(resolve(pin_ids_, pin.get<std::string>())),
                        "Net contains a duplicate pin reference");
            }
            net_ids_.emplace(id, circuit_.add_net(std::move(net)));
        }
    }

    [[nodiscard]] PhysicalPart physical_part(const nlohmann::json &object) const {
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

    void read_selected_physical_parts() {
        for (const auto &[component_id, part] : selected_parts_) {
            circuit_.select_physical_part(resolve(component_ids_, component_id),
                                          physical_part(part));
        }
    }

    const nlohmann::json &document_;
    Circuit circuit_;
    std::map<std::string, PinDefId> pin_def_ids_;
    std::map<std::string, ComponentDefId> component_def_ids_;
    std::map<std::string, ComponentId> component_ids_;
    std::map<std::string, PinId> pin_ids_;
    std::map<std::string, NetId> net_ids_;
    std::vector<std::pair<std::string, nlohmann::json>> selected_parts_;
};

} // namespace detail

/** Read a logical circuit from parsed JSON, rejecting structurally invalid input. */
[[nodiscard]] inline Circuit read_logical_circuit(const nlohmann::json &document) {
    return detail::LogicalCircuitReader{document}.read();
}

/** Read a logical circuit from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] inline Circuit read_logical_circuit_text(std::string_view text) {
    return read_logical_circuit(nlohmann::json::parse(text.begin(), text.end()));
}

/** Read a logical circuit from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] inline Circuit read_logical_circuit(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_logical_circuit_text(buffer.str());
}

} // namespace volt::io
