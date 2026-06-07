#include <volt/io/logical_circuit_writer.hpp>

#include <volt/circuit/queries.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string json_string(std::string_view value) {
    auto result = std::string{"\""};
    for (const auto character : value) {
        switch (character) {
        case '\"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr auto hex = std::string_view{"0123456789abcdef"};
                const auto byte = static_cast<unsigned char>(character);
                result += "\\u00";
                result += hex[(byte >> 4U) & 0x0FU];
                result += hex[byte & 0x0FU];
            } else {
                result += character;
            }
            break;
        }
    }
    result += '"';
    return result;
}

[[nodiscard]] std::string pin_role_name(PinRole role) {
    switch (role) {
    case PinRole::Passive:
        return "Passive";
    case PinRole::PowerInput:
        return "PowerInput";
    case PinRole::PowerOutput:
        return "PowerOutput";
    case PinRole::Ground:
        return "Ground";
    case PinRole::DigitalInput:
        return "DigitalInput";
    case PinRole::DigitalOutput:
        return "DigitalOutput";
    case PinRole::Bidirectional:
        return "Bidirectional";
    case PinRole::AnalogInput:
        return "AnalogInput";
    case PinRole::AnalogOutput:
        return "AnalogOutput";
    case PinRole::NoConnect:
        return "NoConnect";
    }
    throw std::logic_error{"Unhandled pin role"};
}

[[nodiscard]] std::string connection_requirement_name(ConnectionRequirement requirement) {
    switch (requirement) {
    case ConnectionRequirement::Optional:
        return "Optional";
    case ConnectionRequirement::Required:
        return "Required";
    case ConnectionRequirement::MustNotConnect:
        return "MustNotConnect";
    }
    throw std::logic_error{"Unhandled connection requirement"};
}

[[nodiscard]] std::string electrical_terminal_kind_name(ElectricalTerminalKind kind) {
    switch (kind) {
    case ElectricalTerminalKind::Unspecified:
        return "Unspecified";
    case ElectricalTerminalKind::Passive:
        return "Passive";
    case ElectricalTerminalKind::Signal:
        return "Signal";
    case ElectricalTerminalKind::Power:
        return "Power";
    case ElectricalTerminalKind::Ground:
        return "Ground";
    case ElectricalTerminalKind::NoConnect:
        return "NoConnect";
    }
    throw std::logic_error{"Unhandled electrical terminal kind"};
}

[[nodiscard]] std::string electrical_direction_name(ElectricalDirection direction) {
    switch (direction) {
    case ElectricalDirection::Unspecified:
        return "Unspecified";
    case ElectricalDirection::Input:
        return "Input";
    case ElectricalDirection::Output:
        return "Output";
    case ElectricalDirection::Bidirectional:
        return "Bidirectional";
    case ElectricalDirection::Passive:
        return "Passive";
    }
    throw std::logic_error{"Unhandled electrical direction"};
}

[[nodiscard]] std::string electrical_signal_domain_name(ElectricalSignalDomain domain) {
    switch (domain) {
    case ElectricalSignalDomain::Unspecified:
        return "Unspecified";
    case ElectricalSignalDomain::Digital:
        return "Digital";
    case ElectricalSignalDomain::Analog:
        return "Analog";
    case ElectricalSignalDomain::Mixed:
        return "Mixed";
    }
    throw std::logic_error{"Unhandled electrical signal domain"};
}

[[nodiscard]] std::string electrical_drive_kind_name(ElectricalDriveKind kind) {
    switch (kind) {
    case ElectricalDriveKind::Unspecified:
        return "Unspecified";
    case ElectricalDriveKind::PushPull:
        return "PushPull";
    case ElectricalDriveKind::OpenCollector:
        return "OpenCollector";
    case ElectricalDriveKind::OpenDrain:
        return "OpenDrain";
    case ElectricalDriveKind::HighImpedance:
        return "HighImpedance";
    case ElectricalDriveKind::Passive:
        return "Passive";
    }
    throw std::logic_error{"Unhandled electrical drive kind"};
}

[[nodiscard]] std::string electrical_polarity_name(ElectricalPolarity polarity) {
    switch (polarity) {
    case ElectricalPolarity::None:
        return "None";
    case ElectricalPolarity::ActiveHigh:
        return "ActiveHigh";
    case ElectricalPolarity::ActiveLow:
        return "ActiveLow";
    }
    throw std::logic_error{"Unhandled electrical polarity"};
}

[[nodiscard]] std::string net_kind_name(NetKind kind) {
    switch (kind) {
    case NetKind::Signal:
        return "Signal";
    case NetKind::Power:
        return "Power";
    case NetKind::Ground:
        return "Ground";
    case NetKind::Clock:
        return "Clock";
    case NetKind::Analog:
        return "Analog";
    case NetKind::HighCurrent:
        return "HighCurrent";
    }
    throw std::logic_error{"Unhandled net kind"};
}

[[nodiscard]] std::string port_role_name(PortRole role) {
    switch (role) {
    case PortRole::Passive:
        return "Passive";
    case PortRole::Input:
        return "Input";
    case PortRole::Output:
        return "Output";
    case PortRole::Bidirectional:
        return "Bidirectional";
    case PortRole::PowerInput:
        return "PowerInput";
    case PortRole::PowerOutput:
        return "PowerOutput";
    case PortRole::Ground:
        return "Ground";
    }
    throw std::logic_error{"Unhandled port role"};
}

[[nodiscard]] std::string unit_dimension_name(UnitDimension dimension) {
    switch (dimension) {
    case UnitDimension::Resistance:
        return "resistance";
    case UnitDimension::Capacitance:
        return "capacitance";
    case UnitDimension::Inductance:
        return "inductance";
    case UnitDimension::Voltage:
        return "voltage";
    case UnitDimension::Current:
        return "current";
    case UnitDimension::Power:
        return "power";
    case UnitDimension::Frequency:
        return "frequency";
    case UnitDimension::Time:
        return "time";
    case UnitDimension::Temperature:
        return "temperature";
    case UnitDimension::Ratio:
        return "ratio";
    }
    throw std::logic_error{"Unhandled unit dimension"};
}

[[nodiscard]] std::string tolerance_mode_name(ToleranceMode mode) {
    switch (mode) {
    case ToleranceMode::Absolute:
        return "absolute";
    case ToleranceMode::Percent:
        return "percent";
    }
    throw std::logic_error{"Unhandled tolerance mode"};
}

void write_json_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw std::logic_error{"Cannot write non-finite JSON number"};
    }
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

void write_property_value(std::ostream &out, const PropertyValue &value) {
    out << "{ \"type\": ";
    switch (value.kind()) {
    case PropertyValueKind::String:
        out << "\"string\", \"value\": " << json_string(value.as_string());
        break;
    case PropertyValueKind::Boolean:
        out << "\"boolean\", \"value\": " << (value.as_bool() ? "true" : "false");
        break;
    case PropertyValueKind::Integer:
        out << "\"integer\", \"value\": " << value.as_integer();
        break;
    case PropertyValueKind::Number:
        if (!std::isfinite(value.as_number())) {
            throw std::logic_error{"Cannot write non-finite JSON number"};
        }
        out << "\"number\", \"value\": "
            << std::setprecision(std::numeric_limits<double>::max_digits10) << value.as_number();
        break;
    }
    out << " }";
}

void write_properties(std::ostream &out, const PropertyMap &properties) {
    out << '{';
    if (!properties.empty()) {
        out << '\n';
        auto index = std::size_t{0};
        for (const auto &[key, value] : properties.entries()) {
            out << "        " << json_string(key.value()) << ": ";
            write_property_value(out, value);
            if (++index != properties.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "      ";
    }
    out << '}';
}

void write_quantity_payload(std::ostream &out, const Quantity &quantity) {
    out << "\"dimension\": " << json_string(unit_dimension_name(quantity.dimension()))
        << ", \"value\": ";
    write_json_number(out, quantity.value());
}

void write_electrical_attribute_value(std::ostream &out, const ElectricalAttributeValue &value) {
    out << "{ \"type\": ";
    switch (value.kind()) {
    case ElectricalAttributeValueKind::Quantity:
        out << "\"quantity\", ";
        write_quantity_payload(out, value.as_quantity());
        break;
    case ElectricalAttributeValueKind::Tolerance:
        out << "\"tolerance\", \"mode\": "
            << json_string(tolerance_mode_name(value.as_tolerance().mode())) << ", \"dimension\": "
            << json_string(unit_dimension_name(value.as_tolerance().minus().dimension()))
            << ", \"minus\": ";
        write_json_number(out, value.as_tolerance().minus().value());
        out << ", \"plus\": ";
        write_json_number(out, value.as_tolerance().plus().value());
        break;
    case ElectricalAttributeValueKind::Range:
        out << "\"range\", \"dimension\": "
            << json_string(unit_dimension_name(value.as_range().dimension()));
        if (value.as_range().minimum().has_value()) {
            out << ", \"minimum\": ";
            write_json_number(out, value.as_range().minimum()->value());
        }
        if (value.as_range().maximum().has_value()) {
            out << ", \"maximum\": ";
            write_json_number(out, value.as_range().maximum()->value());
        }
        break;
    }
    out << " }";
}

void write_electrical_attributes(std::ostream &out, const ElectricalAttributeMap &attributes,
                                 std::string_view entry_indent, std::string_view closing_indent) {
    out << '{';
    if (!attributes.empty()) {
        out << '\n';
        auto index = std::size_t{0};
        for (const auto &[name, value] : attributes.entries()) {
            out << entry_indent << json_string(name.value()) << ": ";
            write_electrical_attribute_value(out, value);
            if (++index != attributes.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << closing_indent;
    }
    out << '}';
}

[[nodiscard]] std::string module_component_id(ModuleComponentId id) { return encode_local_id(id); }

[[nodiscard]] std::string module_instance_id(ModuleInstanceId id) { return encode_local_id(id); }

[[nodiscard]] std::string rule_class_id(RuleClassId id) { return encode_local_id(id); }

void write_selected_physical_part(std::ostream &out, const PhysicalPart &part) {
    out << "{\n";
    out << "      \"manufacturer_part\": { \"manufacturer\": "
        << json_string(part.manufacturer_part().manufacturer())
        << ", \"part_number\": " << json_string(part.manufacturer_part().part_number()) << " },\n";
    out << "      \"package\": " << json_string(part.package().value()) << ",\n";
    out << "      \"footprint\": { \"library\": " << json_string(part.footprint().library())
        << ", \"name\": " << json_string(part.footprint().name()) << " },\n";
    out << "      \"pin_pad_mappings\": [\n";
    for (std::size_t index = 0; index < part.pin_pad_mappings().size(); ++index) {
        const auto &mapping = part.pin_pad_mappings()[index];
        out << "        { \"pin\": " << json_string(pin_def_id(mapping.pin()))
            << ", \"pad\": " << json_string(mapping.pad()) << " }";
        if (index + 1 != part.pin_pad_mappings().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "      ],\n";
    if (part.model_3d().has_value()) {
        const auto &model_3d = part.model_3d().value();
        out << "      \"model_3d\": {\n";
        out << "        \"format\": " << json_string(model_3d.format()) << ",\n";
        out << "        \"file_name\": " << json_string(model_3d.file_name()) << ",\n";
        out << "        \"translation_mm\": [";
        for (std::size_t index = 0; index < model_3d.translation_mm().size(); ++index) {
            write_json_number(out, model_3d.translation_mm()[index]);
            if (index + 1 != model_3d.translation_mm().size()) {
                out << ", ";
            }
        }
        out << "],\n";
        out << "        \"rotation_deg\": ";
        write_json_number(out, model_3d.rotation_deg());
        out << "\n";
        out << "      },\n";
    }
    out << "      \"properties\": ";
    write_properties(out, part.properties());
    if (!part.electrical_attributes().empty()) {
        out << ",\n      \"electrical_attributes\": ";
        write_electrical_attributes(out, part.electrical_attributes(), "        ", "      ");
    }
    out << "\n    }";
}

void write_pin_definition_semantics(std::ostream &out, const PinDefinition &pin) {
    if (pin.terminal_kind() != ElectricalTerminalKind::Unspecified) {
        out << ", \"terminal_kind\": "
            << json_string(electrical_terminal_kind_name(pin.terminal_kind()));
    }
    if (pin.direction() != ElectricalDirection::Unspecified) {
        out << ", \"direction\": " << json_string(electrical_direction_name(pin.direction()));
    }
    if (pin.signal_domain() != ElectricalSignalDomain::Unspecified) {
        out << ", \"signal_domain\": "
            << json_string(electrical_signal_domain_name(pin.signal_domain()));
    }
    if (pin.drive_kind() != ElectricalDriveKind::Unspecified) {
        out << ", \"drive_kind\": " << json_string(electrical_drive_kind_name(pin.drive_kind()));
    }
    if (pin.polarity() != ElectricalPolarity::None) {
        out << ", \"polarity\": " << json_string(electrical_polarity_name(pin.polarity()));
    }
}

void write_pin_definition_electrical_attributes(std::ostream &out,
                                                const ElectricalAttributeMap &attributes) {
    if (!attributes.empty()) {
        out << ", \"electrical_attributes\": ";
        write_electrical_attributes(out, attributes, "        ", "      ");
    }
}

void write_rule_classes(std::ostream &out, const Circuit &circuit) {
    out << "  \"rule_classes\": { \"classes\": [\n";
    for (std::size_t index = 0; index < circuit.rule_class_count(); ++index) {
        const auto id = RuleClassId{index};
        const auto &rule_class = circuit.rule_class(id);
        out << "    { \"id\": " << json_string(rule_class_id(id))
            << ", \"name\": " << json_string(rule_class.name().value());
        if (rule_class.maximum_net_voltage().has_value()) {
            out << ", \"maximum_net_voltage\": { ";
            write_quantity_payload(out, rule_class.maximum_net_voltage().value());
            out << " }";
        }
        if (rule_class.copper_clearance_mm().has_value()) {
            out << ", \"copper_clearance_mm\": ";
            write_json_number(out, rule_class.copper_clearance_mm().value());
        }
        out << " }";
        if (index + 1 != circuit.rule_class_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ], \"net_assignments\": [";
    if (!circuit.net_rule_class_assignments().empty()) {
        out << '\n';
        for (std::size_t index = 0; index < circuit.net_rule_class_assignments().size(); ++index) {
            const auto [net, rule_class] = circuit.net_rule_class_assignments()[index];
            out << "    { \"net\": " << json_string(net_id(net))
                << ", \"rule_class\": " << json_string(rule_class_id(rule_class)) << " }";
            if (index + 1 != circuit.net_rule_class_assignments().size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "  ";
    }
    out << "] }";
}

} // namespace volt::io::detail

namespace volt::io {

void write_logical_circuit(std::ostream &out, const Circuit &circuit) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(logical_circuit_format_name()) << ",\n";
    out << "  \"version\": " << logical_circuit_format_version() << ",\n";

    out << "  \"pin_definitions\": [\n";
    for (std::size_t index = 0; index < circuit.pin_definition_count(); ++index) {
        const auto id = PinDefId{index};
        const auto &pin = circuit.pin_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::pin_def_id(id))
            << ", \"name\": " << detail::json_string(pin.name())
            << ", \"number\": " << detail::json_string(pin.number())
            << ", \"role\": " << detail::json_string(detail::pin_role_name(pin.role()))
            << ", \"connection_requirement\": "
            << detail::json_string(
                   detail::connection_requirement_name(pin.connection_requirement()));
        detail::write_pin_definition_semantics(out, pin);
        detail::write_pin_definition_electrical_attributes(
            out, circuit.pin_definition_electrical_attributes(id));
        out << " }";
        if (index + 1 != circuit.pin_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"component_definitions\": [\n";
    for (std::size_t index = 0; index < circuit.component_definition_count(); ++index) {
        const auto id = ComponentDefId{index};
        const auto &definition = circuit.component_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::component_def_id(id))
            << ", \"name\": " << detail::json_string(definition.name());
        if (definition.source().has_value()) {
            out << ", \"source\": { \"namespace\": "
                << detail::json_string(definition.source()->namespace_name())
                << ", \"name\": " << detail::json_string(definition.source()->name())
                << ", \"version\": " << detail::json_string(definition.source()->version()) << " }";
        }
        if (!definition.schematic_symbols().empty()) {
            out << ", \"schematic_symbols\": [";
            for (std::size_t symbol_index = 0; symbol_index < definition.schematic_symbols().size();
                 ++symbol_index) {
                const auto &symbol = definition.schematic_symbols()[symbol_index];
                out << "{ \"name\": " << detail::json_string(symbol.name())
                    << ", \"variant\": " << detail::json_string(symbol.variant()) << " }";
                if (symbol_index + 1U != definition.schematic_symbols().size()) {
                    out << ", ";
                }
            }
            out << "]";
        }
        out << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < definition.pins().size(); ++pin_index) {
            out << detail::json_string(detail::pin_def_id(definition.pins()[pin_index]));
            if (pin_index + 1 != definition.pins().size()) {
                out << ", ";
            }
        }
        out << "], \"properties\": ";
        detail::write_properties(out, definition.properties());
        out << " }";
        if (index + 1 != circuit.component_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"components\": [\n";
    for (std::size_t index = 0; index < circuit.component_count(); ++index) {
        const auto id = ComponentId{index};
        const auto &component = circuit.component(id);
        out << "    { \"id\": " << detail::json_string(detail::component_id(id))
            << ", \"definition\": "
            << detail::json_string(detail::component_def_id(component.definition()))
            << ", \"reference\": " << detail::json_string(component.reference().value())
            << ", \"properties\": ";
        detail::write_properties(out, component.properties());
        const auto &component_attributes = circuit.component_electrical_attributes(id);
        if (!component_attributes.empty()) {
            out << ", \"electrical_attributes\": ";
            detail::write_electrical_attributes(out, component_attributes, "        ", "      ");
        }
        if (circuit.selected_physical_part(id).has_value()) {
            out << ", \"selected_physical_part\": ";
            detail::write_selected_physical_part(out, circuit.selected_physical_part(id).value());
        }
        out << " }";
        if (index + 1 != circuit.component_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"pins\": [\n";
    for (std::size_t index = 0; index < circuit.pin_count(); ++index) {
        const auto id = PinId{index};
        const auto &pin = circuit.pin(id);
        out << "    { \"id\": " << detail::json_string(detail::pin_id(id))
            << ", \"component\": " << detail::json_string(detail::component_id(pin.component()))
            << ", \"definition\": " << detail::json_string(detail::pin_def_id(pin.definition()))
            << " }";
        if (index + 1 != circuit.pin_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"nets\": [\n";
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto id = NetId{index};
        const auto &net = circuit.net(id);
        out << "    { \"id\": " << detail::json_string(detail::net_id(id))
            << ", \"name\": " << detail::json_string(net.name().value())
            << ", \"kind\": " << detail::json_string(detail::net_kind_name(net.kind()))
            << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < net.pins().size(); ++pin_index) {
            out << detail::json_string(detail::pin_id(net.pins()[pin_index]));
            if (pin_index + 1 != net.pins().size()) {
                out << ", ";
            }
        }
        out << "]";
        const auto &net_attributes = circuit.net_electrical_attributes(id);
        if (!net_attributes.empty()) {
            out << ", \"electrical_attributes\": ";
            detail::write_electrical_attributes(out, net_attributes, "        ", "      ");
        }
        out << " }";
        if (index + 1 != circuit.net_count()) {
            out << ',';
        }
        out << '\n';
    }
    const auto has_design_intent =
        !circuit.intentional_stub_nets().empty() || !circuit.intentional_no_connect_pins().empty();
    const auto has_rule_classes =
        circuit.rule_class_count() != 0 || !circuit.net_rule_class_assignments().empty();
    const auto has_hierarchy =
        circuit.module_definition_count() != 0 || circuit.module_instance_count() != 0;
    out << ((has_rule_classes || has_design_intent || has_hierarchy) ? "  ],\n" : "  ]\n");

    if (has_rule_classes) {
        detail::write_rule_classes(out, circuit);
        out << ((has_design_intent || has_hierarchy) ? ",\n" : "\n");
    }

    if (has_design_intent) {
        out << "  \"design_intent\": { \"stub_nets\": [";
        for (std::size_t index = 0; index < circuit.intentional_stub_nets().size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::net_id(circuit.intentional_stub_nets()[index]));
        }
        out << "], \"no_connect_pins\": [";
        for (std::size_t index = 0; index < circuit.intentional_no_connect_pins().size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << detail::json_string(
                detail::pin_id(circuit.intentional_no_connect_pins()[index]));
        }
        out << "] }" << (has_hierarchy ? ",\n" : "\n");
    }

    if (!has_hierarchy) {
        out << "}\n";
        return;
    }

    out << "  \"module_definitions\": [\n";
    for (std::size_t index = 0; index < circuit.module_definition_count(); ++index) {
        const auto id = ModuleDefId{index};
        const auto &definition = circuit.module_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::module_def_id(id))
            << ", \"name\": " << detail::json_string(definition.name().value())
            << ", \"local_nets\": [";
        for (std::size_t net_index = 0; net_index < definition.template_nets().size();
             ++net_index) {
            const auto template_net_id = definition.template_nets()[net_index];
            const auto &template_net = circuit.template_net_definition(template_net_id);
            if (net_index != 0) {
                out << ", ";
            }
            out << "{ \"id\": " << detail::json_string(detail::template_net_def_id(template_net_id))
                << ", \"name\": " << detail::json_string(template_net.name().value())
                << ", \"kind\": " << detail::json_string(detail::net_kind_name(template_net.kind()))
                << " }";
        }
        out << "], \"components\": [";
        for (std::size_t component_index = 0; component_index < definition.components().size();
             ++component_index) {
            const auto component_id = definition.components()[component_index];
            const auto &component = circuit.module_component_template(component_id);
            if (component_index != 0) {
                out << ", ";
            }
            out << "{ \"id\": " << detail::json_string(detail::module_component_id(component_id))
                << ", \"definition\": "
                << detail::json_string(detail::component_def_id(component.definition()))
                << ", \"reference\": " << detail::json_string(component.reference().value())
                << ", \"properties\": ";
            detail::write_properties(out, component.properties());
            out << " }";
        }
        out << "], \"connections\": [";
        auto wrote_connection = false;
        for (const auto component_id : definition.components()) {
            const auto &component = circuit.module_component_template(component_id);
            const auto &component_definition = circuit.component_definition(component.definition());
            for (const auto pin_id : component_definition.pins()) {
                const auto net = queries::template_net_for(circuit, id, component_id, pin_id);
                if (!net.has_value()) {
                    continue;
                }
                if (wrote_connection) {
                    out << ", ";
                }
                wrote_connection = true;
                out << "{ \"net\": "
                    << detail::json_string(detail::template_net_def_id(net.value()))
                    << ", \"component\": "
                    << detail::json_string(detail::module_component_id(component_id))
                    << ", \"pin\": " << detail::json_string(detail::pin_def_id(pin_id)) << " }";
            }
        }
        out << "], \"ports\": [";
        for (std::size_t port_index = 0; port_index < definition.ports().size(); ++port_index) {
            const auto port_id = definition.ports()[port_index];
            const auto &port = circuit.port_definition(port_id);
            if (port_index != 0) {
                out << ", ";
            }
            out << "{ \"id\": " << detail::json_string(detail::port_def_id(port_id))
                << ", \"name\": " << detail::json_string(port.name().value())
                << ", \"internal_net\": "
                << detail::json_string(detail::template_net_def_id(port.internal_net()))
                << ", \"role\": " << detail::json_string(detail::port_role_name(port.role()))
                << ", \"required\": " << (port.required() ? "true" : "false") << " }";
        }
        out << "] }";
        if (index + 1 != circuit.module_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"module_instances\": [\n";
    for (std::size_t index = 0; index < circuit.module_instance_count(); ++index) {
        const auto id = ModuleInstanceId{index};
        const auto &instance = circuit.module_instance(id);
        const auto &definition = circuit.module_definition(instance.definition());
        out << "    { \"id\": " << detail::json_string(detail::module_instance_id(id))
            << ", \"definition\": "
            << detail::json_string(detail::module_def_id(instance.definition()))
            << ", \"name\": " << detail::json_string(instance.name().value())
            << ", \"net_origins\": [";
        for (std::size_t net_index = 0; net_index < definition.template_nets().size();
             ++net_index) {
            const auto template_net_id = definition.template_nets()[net_index];
            const auto concrete_net = queries::concrete_net_for(circuit, id, template_net_id);
            if (!concrete_net.has_value()) {
                throw std::logic_error{"Module instance is missing concrete net origin"};
            }
            if (net_index != 0) {
                out << ", ";
            }
            out << "{ \"template_net\": "
                << detail::json_string(detail::template_net_def_id(template_net_id))
                << ", \"net\": " << detail::json_string(detail::net_id(concrete_net.value()))
                << " }";
        }
        out << "], \"component_origins\": [";
        for (std::size_t component_index = 0; component_index < definition.components().size();
             ++component_index) {
            const auto template_component_id = definition.components()[component_index];
            const auto concrete_component =
                queries::concrete_component_for(circuit, id, template_component_id);
            if (!concrete_component.has_value()) {
                throw std::logic_error{"Module instance is missing concrete component origin"};
            }
            if (component_index != 0) {
                out << ", ";
            }
            out << "{ \"template_component\": "
                << detail::json_string(detail::module_component_id(template_component_id))
                << ", \"component\": "
                << detail::json_string(detail::component_id(concrete_component.value())) << " }";
        }
        out << "], \"port_bindings\": [";
        auto wrote_binding = false;
        for (const auto port_id : definition.ports()) {
            const auto binding = queries::port_binding_for(circuit, id, port_id);
            if (!binding.has_value()) {
                continue;
            }
            const auto &port_binding = circuit.port_binding(binding.value());
            if (wrote_binding) {
                out << ", ";
            }
            wrote_binding = true;
            out << "{ \"port\": " << detail::json_string(detail::port_def_id(port_id))
                << ", \"parent_net\": "
                << detail::json_string(detail::net_id(port_binding.parent_net())) << " }";
        }
        out << "] }";
        if (index + 1 != circuit.module_instance_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] std::string write_logical_circuit(const Circuit &circuit) {
    auto out = std::ostringstream{};
    write_logical_circuit(out, circuit);
    return out.str();
}

} // namespace volt::io
