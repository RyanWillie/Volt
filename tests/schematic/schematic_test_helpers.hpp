#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/layout.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/validation.hpp>

namespace {

[[maybe_unused]] volt::ComponentId add_resistor(volt::Circuit &circuit,
                                                const std::string &reference) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{reference});
}

[[maybe_unused]] volt::ComponentId add_resistor(volt::Circuit &circuit) {
    return add_resistor(circuit, "R1");
}

[[maybe_unused]] volt::ComponentId add_three_pin_component(volt::Circuit &circuit) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto third_pin = circuit.add_pin_definition(volt::PinDefinition{
        "C", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"ThreePin", std::vector{first_pin, second_pin, third_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{"U1"});
}

[[maybe_unused]] volt::ComponentId add_four_pin_component(volt::Circuit &circuit,
                                                          const std::string &reference) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto third_pin = circuit.add_pin_definition(volt::PinDefinition{
        "C", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto fourth_pin = circuit.add_pin_definition(volt::PinDefinition{
        "D", "4", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(volt::ComponentDefinition{
        "FourPin", std::vector{first_pin, second_pin, third_pin, fourth_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{reference});
}

[[maybe_unused]] volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
}

[[maybe_unused]] volt::NetId add_named_net(volt::Circuit &circuit, std::string name) {
    return circuit.add_net(volt::Net{volt::NetName{std::move(name)}, volt::NetKind::Signal});
}

[[maybe_unused]] void connect_pin_by_number(volt::Circuit &circuit, volt::NetId net,
                                            volt::ComponentId component,
                                            const std::string &number) {
    circuit.connect(net, volt::queries::pin_by_number(circuit, component, number).value());
}

[[maybe_unused]] volt::SymbolDefinition make_resistor_symbol() {
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    return symbol;
}

[[maybe_unused]] volt::SymbolDefinition make_four_pin_ic_symbol() {
    auto symbol = volt::SymbolDefinition{"FourPinIC"};
    symbol.add_pin(
        volt::SymbolPin{"OSC_IN", "1", volt::Point{0.0, -10.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"NRST", "2", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"BOOT0", "3", volt::Point{0.0, 10.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"VSS", "4", volt::Point{28.0, 10.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolRectangle{volt::Point{0.0, -16.0}, volt::Point{28.0, 16.0}});
    return symbol;
}

[[maybe_unused]] bool report_has_code(const volt::DiagnosticReport &report,
                                      const std::string &code) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [&code](const volt::Diagnostic &diagnostic) {
                           return diagnostic.code() == volt::DiagnosticCode{code};
                       });
}

[[maybe_unused]] std::size_t diagnostic_count(const volt::DiagnosticReport &report,
                                              const std::string &code) {
    return static_cast<std::size_t>(
        std::count_if(report.diagnostics().begin(), report.diagnostics().end(),
                      [&code](const volt::Diagnostic &diagnostic) {
                          return diagnostic.code() == volt::DiagnosticCode{code};
                      }));
}

[[maybe_unused]] bool report_has_code_and_entities(const volt::DiagnosticReport &report,
                                                   const std::string &code,
                                                   const std::vector<volt::EntityRef> &entities) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [&code, &entities](const volt::Diagnostic &diagnostic) {
                           return diagnostic.code() == volt::DiagnosticCode{code} &&
                                  diagnostic.entities() == entities;
                       });
}

[[maybe_unused]] const volt::Diagnostic &require_diagnostic(const volt::DiagnosticReport &report,
                                                            std::string_view code) {
    const auto it = std::find_if(
        report.diagnostics().begin(), report.diagnostics().end(),
        [&code](const volt::Diagnostic &diagnostic) { return diagnostic.code().value() == code; });
    REQUIRE(it != report.diagnostics().end());
    return *it;
}

} // namespace
