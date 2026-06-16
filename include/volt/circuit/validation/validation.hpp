#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

namespace detail {

[[nodiscard]] bool is_no_connect_pin(const PinDefinition &definition);

[[nodiscard]] bool is_output_pin(const PinDefinition &definition);

[[nodiscard]] bool is_input_pin(const PinDefinition &definition);

[[nodiscard]] bool can_drive_signal_net(const PinDefinition &definition);

[[nodiscard]] bool is_power_input(const PinDefinition &definition);

[[nodiscard]] bool is_power_source(const PinDefinition &definition);

/** Validation-local view that treats bound module port nets as electrical continuity. */
class NetContinuityView {
  public:
    /** Build continuity groups from canonical nets plus explicit module port bindings. */
    explicit NetContinuityView(const Circuit &circuit);

    /** Return all pins electrically continuous with this net for validation checks. */
    [[nodiscard]] std::vector<PinId> pins_for_group(const Circuit &circuit, NetId net) const;

    /** Return whether any net in this electrical continuity group asserts power intent. */
    [[nodiscard]] bool group_has_authored_power_supply(const Circuit &circuit, NetId net) const;

  private:
    [[nodiscard]] std::size_t find(std::size_t index) const;

    void join(NetId first, NetId second);

    std::vector<std::size_t> parent_;
};

void validate_pin_connection_requirements(const Circuit &circuit, DiagnosticReport &report);

void validate_net_shape(NetId net_id, const Net &net, const std::vector<PinId> &group_pins,
                        DiagnosticReport &report);

void validate_power_and_ground_semantics(const Circuit &circuit, NetId net_id, const Net &net,
                                         const std::vector<PinId> &group_pins,
                                         bool has_authored_power_supply, DiagnosticReport &report);

void validate_selected_part_voltage_ratings(const Circuit &circuit, NetId net_id, const Net &net,
                                            const std::vector<PinId> &group_pins,
                                            DiagnosticReport &report);

void validate_pin_voltage_ranges(const Circuit &circuit, NetId net_id, const Net &net,
                                 const std::vector<PinId> &group_pins, DiagnosticReport &report);

void validate_output_driver_conflicts(const Circuit &circuit, NetId net_id,
                                      const std::vector<PinId> &group_pins,
                                      DiagnosticReport &report);

void validate_input_signal_domains(const Circuit &circuit, NetId net_id,
                                   const std::vector<PinId> &group_pins, DiagnosticReport &report);

void validate_net_shapes(const Circuit &circuit, const NetContinuityView &continuity,
                         DiagnosticReport &report);

void validate_net_electrical_rules(const Circuit &circuit, const NetContinuityView &continuity,
                                   DiagnosticReport &report);

void validate_net_semantics(const Circuit &circuit, const NetContinuityView &continuity,
                            DiagnosticReport &report);

void validate_required_module_ports(const Circuit &circuit, DiagnosticReport &report);

void validate_physical_part_selection(const Circuit &circuit, DiagnosticReport &report);

void validate_bom_component_readiness(const Circuit &circuit, DiagnosticReport &report);

} // namespace detail

/** Validate logical connectivity shape and pin connection requirements. */
[[nodiscard]] DiagnosticReport validate_connectivity(const Circuit &circuit);

/** Validate electrical rules over the existing logical circuit connectivity. */
[[nodiscard]] DiagnosticReport validate_electrical_rules(const Circuit &circuit);

/** Run the default logical circuit validation suite. */
[[nodiscard]] DiagnosticReport validate_circuit(const Circuit &circuit);

/** Validate whether a circuit is ready for PCB/layout work. */
[[nodiscard]] DiagnosticReport validate_for_pcb(const Circuit &circuit);

/** Validate whether a circuit is ready for deterministic BOM projection and handoff. */
[[nodiscard]] DiagnosticReport validate_bom_readiness(const Circuit &circuit);

} // namespace volt
