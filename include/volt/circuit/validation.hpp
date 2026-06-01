#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <volt/circuit/circuit_view.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

namespace detail {

[[nodiscard]] bool is_no_connect_pin(const PinDefinition &definition);

[[nodiscard]] bool is_output_pin(const PinDefinition &definition);

[[nodiscard]] bool is_power_input(const PinDefinition &definition);

[[nodiscard]] bool is_power_source(const PinDefinition &definition);

/** Validation-local view that treats bound module port nets as electrical continuity. */
class NetContinuityView {
  public:
    /** Build continuity groups from canonical nets plus explicit module port bindings. */
    explicit NetContinuityView(CircuitView circuit);

    /** Return all pins electrically continuous with this net for validation checks. */
    [[nodiscard]] std::vector<PinId> pins_for_group(CircuitView circuit, NetId net) const;

  private:
    [[nodiscard]] std::size_t find(std::size_t index) const;

    void join(NetId first, NetId second);

    std::vector<std::size_t> parent_;
};

void validate_pin_connection_requirements(CircuitView circuit, DiagnosticReport &report);

void validate_net_shape(NetId net_id, const Net &net, const std::vector<PinId> &group_pins,
                        DiagnosticReport &report);

void validate_power_and_ground_semantics(CircuitView circuit, NetId net_id, const Net &net,
                                         const std::vector<PinId> &group_pins,
                                         DiagnosticReport &report);

void validate_selected_part_voltage_ratings(CircuitView circuit, NetId net_id, const Net &net,
                                            const std::vector<PinId> &group_pins,
                                            DiagnosticReport &report);

void validate_pin_voltage_ranges(CircuitView circuit, NetId net_id, const Net &net,
                                 const std::vector<PinId> &group_pins, DiagnosticReport &report);

void validate_output_driver_conflicts(CircuitView circuit, NetId net_id,
                                      const std::vector<PinId> &group_pins,
                                      DiagnosticReport &report);

void validate_net_shapes(CircuitView circuit, const NetContinuityView &continuity,
                         DiagnosticReport &report);

void validate_net_electrical_rules(CircuitView circuit, const NetContinuityView &continuity,
                                   DiagnosticReport &report);

void validate_net_semantics(CircuitView circuit, const NetContinuityView &continuity,
                            DiagnosticReport &report);

void validate_required_module_ports(CircuitView circuit, DiagnosticReport &report);

void validate_physical_part_selection(CircuitView circuit, DiagnosticReport &report);

} // namespace detail

/** Validate logical connectivity shape and pin connection requirements. */
[[nodiscard]] DiagnosticReport validate_connectivity(CircuitView circuit);

/** Validate electrical rules over the existing logical circuit connectivity. */
[[nodiscard]] DiagnosticReport validate_electrical_rules(CircuitView circuit);

/** Run the default logical circuit validation suite. */
[[nodiscard]] DiagnosticReport validate_circuit(CircuitView circuit);

/** Validate whether a circuit is ready for PCB/layout work. */
[[nodiscard]] DiagnosticReport validate_for_pcb(CircuitView circuit);

} // namespace volt
