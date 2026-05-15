#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

[[nodiscard]] inline bool wire_covers_point(const WireRun &wire, Point point) noexcept {
    for (std::size_t index = 1; index < wire.points().size(); ++index) {
        if (point_on_schematic_segment(
                point, SchematicSegment{wire.points()[index - 1U], wire.points()[index]})) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool sheet_visually_covers_net_at_pin(const Schematic &schematic,
                                                           const Sheet &sheet, NetId net,
                                                           Point point) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        if (wire.net() == net && wire_covers_point(wire, point)) {
            return true;
        }
    }

    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        if (label.net() == net && same_schematic_point(label.position(), point)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool schematic_readiness_exempts_pin(const Circuit &circuit, PinId pin_id,
                                                          PinDefId pin_def_id) {
    const auto &definition = circuit.pin_definition(pin_def_id);
    return definition.role() == PinRole::NoConnect ||
           definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
           circuit.is_intentional_no_connect_pin(pin_id);
}

inline void validate_symbol_instance_net_coverage(const Schematic &schematic, const Sheet &sheet,
                                                  SymbolInstanceId instance_id,
                                                  DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    const auto &instance = schematic.symbol_instance(instance_id);
    const auto &component = circuit.component(instance.component());
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    for (const auto &symbol_pin : symbol.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }

        const auto pin_id = pin.value();
        const auto &pin_instance = circuit.pin(pin_id);
        const auto pin_def_id = pin_instance.definition();
        const auto net = circuit.net_of(pin_id);
        if (!net.has_value() || schematic_readiness_exempts_pin(circuit, pin_id, pin_def_id)) {
            continue;
        }

        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        if (sheet_visually_covers_net_at_pin(schematic, sheet, net.value(), pin_point)) {
            continue;
        }

        const auto &pin_definition = circuit.pin_definition(pin_def_id);
        const auto &net_model = circuit.net(net.value());
        report.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{"SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"},
            "Schematic omits visual net coverage for " + component.reference().value() + " pin " +
                pin_definition.number() + " (" + pin_definition.name() + ") on " +
                net_model.name().value(),
            std::vector{EntityRef::component(instance.component()), EntityRef::pin(pin_id),
                        EntityRef::pin_def(pin_def_id), EntityRef::net(net.value())},
        });
    }
}

} // namespace detail

/** Validate that placed connected schematic pins have visible net geometry on their sheet. */
[[nodiscard]] inline DiagnosticReport validate_schematic_readiness(const Schematic &schematic) {
    auto report = DiagnosticReport{};

    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        for (const auto instance_id : sheet.symbol_instances()) {
            detail::validate_symbol_instance_net_coverage(schematic, sheet, instance_id, report);
        }
    }

    return report;
}

} // namespace volt
