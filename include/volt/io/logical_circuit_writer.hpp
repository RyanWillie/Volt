#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/circuit/circuit.hpp>
#include <volt/io/detail/typed_id.hpp>

namespace volt::io {

/** Return the canonical v1 logical circuit format name. */
[[nodiscard]] inline constexpr std::string_view logical_circuit_format_name() noexcept {
    return "volt.logical_circuit";
}

/** Return the canonical logical circuit format version written by this library. */
[[nodiscard]] inline constexpr int logical_circuit_format_version() noexcept { return 1; }

namespace detail {

[[nodiscard]] std::string json_string(std::string_view value);

[[nodiscard]] std::string connection_requirement_name(ConnectionRequirement requirement);

[[nodiscard]] std::string electrical_terminal_kind_name(ElectricalTerminalKind kind);

[[nodiscard]] std::string electrical_direction_name(ElectricalDirection direction);

[[nodiscard]] std::string electrical_signal_domain_name(ElectricalSignalDomain domain);

[[nodiscard]] std::string electrical_drive_kind_name(ElectricalDriveKind kind);

[[nodiscard]] std::string electrical_polarity_name(ElectricalPolarity polarity);

[[nodiscard]] std::string net_kind_name(NetKind kind);

[[nodiscard]] std::string port_role_name(PortRole role);

[[nodiscard]] std::string unit_dimension_name(UnitDimension dimension);

[[nodiscard]] std::string tolerance_mode_name(ToleranceMode mode);

void write_json_number(std::ostream &out, double value);

void write_property_value(std::ostream &out, const PropertyValue &value);

void write_properties(std::ostream &out, const PropertyMap &properties);

void write_quantity_payload(std::ostream &out, const Quantity &quantity);

void write_electrical_attribute_value(std::ostream &out, const ElectricalAttributeValue &value);

void write_electrical_attributes(std::ostream &out, const ElectricalAttributeMap &attributes,
                                 std::string_view entry_indent, std::string_view closing_indent);

[[nodiscard]] inline std::string pin_def_id(PinDefId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string component_def_id(ComponentDefId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string component_id(ComponentId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string pin_id(PinId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string net_id(NetId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string module_def_id(ModuleDefId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string template_net_def_id(TemplateNetDefId id) {
    return encode_local_id(id);
}

[[nodiscard]] std::string module_component_id(ModuleComponentId id);

[[nodiscard]] inline std::string port_def_id(PortDefId id) { return encode_local_id(id); }

[[nodiscard]] std::string module_instance_id(ModuleInstanceId id);

void write_selected_physical_part(std::ostream &out, const PhysicalPart &part);

void write_pin_definition_semantics(std::ostream &out, const PinDefinition &pin);

} // namespace detail

/** Write a deterministic JSON representation of the logical circuit to an output stream. */
void write_logical_circuit(std::ostream &out, const Circuit &circuit);

/** Return a deterministic JSON representation of the logical circuit. */
[[nodiscard]] std::string write_logical_circuit(const Circuit &circuit);

} // namespace volt::io
