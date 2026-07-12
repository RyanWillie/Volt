#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/layout.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/validation.hpp>

#include "../support/circuit_test_helpers.hpp"

namespace {

[[maybe_unused]] volt::ComponentId add_resistor(volt::Circuit &circuit,
                                                const std::string &reference) {
    const auto definition = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    return volt::test::instantiate_component(circuit, definition, reference);
}

[[maybe_unused]] volt::ComponentId add_resistor(volt::Circuit &circuit) {
    return add_resistor(circuit, "R1");
}

[[maybe_unused]] volt::ComponentId add_three_pin_component(volt::Circuit &circuit) {
    const auto definition = volt::test::define_component(circuit, "ThreePin",
                                                         {volt::test::passive_pin("A", "1"),
                                                          volt::test::passive_pin("B", "2"),
                                                          volt::test::passive_pin("C", "3")});
    return volt::test::instantiate_component(circuit, definition, "U1");
}

[[maybe_unused]] volt::ComponentId add_four_pin_component(volt::Circuit &circuit,
                                                          const std::string &reference) {
    const auto definition = volt::test::define_component(
        circuit, "FourPin",
        {volt::test::passive_pin("A", "1"), volt::test::passive_pin("B", "2"),
         volt::test::passive_pin("C", "3"), volt::test::passive_pin("D", "4")});
    return volt::test::instantiate_component(circuit, definition, reference);
}

[[maybe_unused]] volt::NetId add_net(volt::Circuit &circuit) {
    return volt::test::add_net(circuit, "VCC", volt::NetKind::Power);
}

[[maybe_unused]] volt::NetId add_named_net(volt::Circuit &circuit, std::string name) {
    return volt::test::add_net(circuit, std::move(name));
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

template <typename Callable>
[[maybe_unused]] void check_kernel_error(Callable &&callable, volt::ErrorCode code,
                                         std::string_view message) {
    try {
        std::forward<Callable>(callable)();
        FAIL("expected typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == code);
        CHECK(std::string{error.what()} == std::string{message});
    }
}

} // namespace
