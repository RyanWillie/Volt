#pragma once

#include "binding_conversions.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <volt/library/part_library.hpp>

namespace volt::python {

class PyPartLibrary;

using PyConnectivityEndpoint = std::variant<std::size_t, std::pair<std::size_t, std::size_t>>;

class PyCircuit {
  public:
    PyCircuit();

    /** Read-only logical dependency for separately bound projection owners. */
    [[nodiscard]] const volt::Circuit &logical_circuit() const noexcept { return circuit_; }

    [[nodiscard]] std::size_t define_resistor();

    [[nodiscard]] std::size_t define_capacitor();

    [[nodiscard]] std::size_t define_polarized_capacitor();

    [[nodiscard]] std::size_t define_inductor();

    [[nodiscard]] std::size_t define_diode();

    [[nodiscard]] std::size_t define_led();

    [[nodiscard]] std::size_t define_switch_spst();

    [[nodiscard]] std::size_t define_crystal_2pin();

    [[nodiscard]] std::size_t define_test_point();

    [[nodiscard]] std::size_t define_connector_1x01();

    [[nodiscard]] std::size_t define_connector_1x02();

    [[nodiscard]] std::size_t define_connector_1x03();

    [[nodiscard]] std::size_t define_regulator_3pin();

    [[nodiscard]] std::size_t define_op_amp_5pin();

    [[nodiscard]] std::size_t
    define_component(const std::string &name, const py::list &pins, const py::dict &properties,
                     const std::string &source_namespace, const std::string &source_name,
                     const std::string &source_version, const py::list &schematic_symbols,
                     py::object contract);

    [[nodiscard]] std::size_t define_library_part(const PyPartLibrary &library,
                                                  const std::string &part_key);

    void select_library_part(std::size_t component, const PyPartLibrary &library,
                             const std::string &part_key);

    [[nodiscard]] py::list validate_selected_part_erc() const;

    [[nodiscard]] std::size_t add_net(const std::string &name, const std::string &kind);

    [[nodiscard]] std::size_t add_net_class(const std::string &name, const py::dict &options);

    void assign_net_class(const std::vector<std::size_t> &nets, std::size_t net_class);

    [[nodiscard]] py::dict net_class_info(std::size_t net_class) const;

    [[nodiscard]] py::list net_refs() const;

    [[nodiscard]] py::list component_refs() const;

    [[nodiscard]] py::object component_selected_part_model_3d(std::size_t component) const;

    void select_physical_part(std::size_t component, const std::string &manufacturer,
                              const std::string &part_number, const std::string &package,
                              const std::string &footprint_library,
                              const std::string &footprint_name, const py::dict &pin_pads,
                              const py::dict &properties, py::object model_3d,
                              py::object approved_alternate_mpns);

    void set_component_quantity(std::size_t component, const std::string &name,
                                const std::string &dimension_name, double value);

    void set_component_percent_tolerance(std::size_t component, double value);

    void set_net_quantity(std::size_t net, const std::string &name,
                          const std::string &dimension_name, double value);

    void select_generic_physical_part(std::size_t component);

    void set_selected_part_quantity(std::size_t component, const std::string &name,
                                    const std::string &dimension_name, double value);

    [[nodiscard]] std::size_t instantiate_ref(std::size_t definition, const std::string &reference,
                                              const py::dict &properties);

    [[nodiscard]] std::size_t instantiate_auto(std::size_t definition, const std::string &prefix,
                                               const py::dict &properties);

    [[nodiscard]] std::size_t pin_by_name(std::size_t component, const std::string &name) const;

    [[nodiscard]] std::size_t pin_by_number(std::size_t component, const std::string &number) const;

    [[nodiscard]] std::size_t pin_component(std::size_t pin) const;

    [[nodiscard]] std::string component_reference(std::size_t component) const;

    [[nodiscard]] py::list pin_refs(std::size_t component) const;

    [[nodiscard]] std::optional<std::string>
    component_schematic_symbol(std::size_t component, const std::string &variant) const;

    void connect(std::size_t net, std::size_t pin);

    void connect_endpoints(std::size_t net, const std::vector<PyConnectivityEndpoint> &endpoints);

    [[nodiscard]] std::optional<std::size_t> net_of(std::size_t pin) const;

    [[nodiscard]] py::list net_pins(std::size_t net) const;

    void mark_intentional_stub_net(std::size_t net);

    void mark_intentional_no_connect_pin(std::size_t pin);

    void set_component_dnp(std::size_t component, bool dnp);

    void set_component_selection_override(std::size_t component, bool selection_override);

    [[nodiscard]] std::size_t define_module(const std::string &name);

    [[nodiscard]] std::size_t add_template_net(std::size_t module, const std::string &name,
                                               const std::string &kind);

    [[nodiscard]] std::pair<std::size_t, std::size_t>
    add_module_port(std::size_t module, const std::string &name, const std::string &kind,
                    const std::string &role, bool required);

    [[nodiscard]] std::size_t add_module_component(std::size_t module, std::size_t definition,
                                                   const std::string &reference,
                                                   const py::dict &properties);

    [[nodiscard]] std::size_t module_component_pin_by_name(std::size_t component,
                                                           const std::string &name) const;

    [[nodiscard]] std::size_t module_component_pin_by_number(std::size_t component,
                                                             const std::string &number) const;

    [[nodiscard]] py::list module_component_pin_refs(std::size_t component) const;

    void
    connect_module_pins(std::size_t module, std::size_t net,
                        const std::vector<std::pair<std::size_t, std::size_t>> &component_pins);

    [[nodiscard]] std::size_t instantiate_root_module(std::size_t definition,
                                                      const std::string &name);

    [[nodiscard]] std::size_t concrete_component_for(std::size_t instance,
                                                     std::size_t component) const;

    void bind_port(std::size_t instance, std::size_t port, std::size_t parent_net);

    [[nodiscard]] py::list template_nets(std::size_t module) const;

    [[nodiscard]] py::list module_ports(std::size_t module) const;

    [[nodiscard]] py::list module_components(std::size_t module) const;

    [[nodiscard]] py::list module_connections(std::size_t module) const;

    [[nodiscard]] py::list module_net_origins(std::size_t instance) const;

    [[nodiscard]] py::list module_component_origins(std::size_t instance) const;

    [[nodiscard]] py::list port_bindings(std::size_t instance) const;

    [[nodiscard]] py::list validate() const;

    [[nodiscard]] py::list validate_for_pcb() const;

    [[nodiscard]] py::list validate_bom_readiness() const;

    [[nodiscard]] std::string bom_json(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string bom_csv(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string bom_sourcing_snapshot_json(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string to_json() const;

  private:
    /** Binding-local typed draft committed atomically through Circuit::define_module. */
    struct ModuleDraft {
        std::size_t handle;
        volt::ModuleSpec spec;
        std::vector<std::size_t> template_net_handles;
        std::vector<std::size_t> port_handles;
        std::vector<std::size_t> component_handles;
        std::optional<volt::ModuleDefId> committed_id = std::nullopt;
    };

    [[nodiscard]] ModuleDraft &module_draft(std::size_t module);

    [[nodiscard]] const ModuleDraft &module_draft(std::size_t module) const;

    [[nodiscard]] std::pair<const ModuleDraft *, std::size_t>
    template_net_draft(std::size_t net) const;

    [[nodiscard]] std::pair<const ModuleDraft *, std::size_t>
    module_component_draft(std::size_t component) const;

    [[nodiscard]] std::pair<const ModuleDraft *, std::size_t> port_draft(std::size_t port) const;

    void preflight_module_drafts(std::size_t module, const volt::ModuleSpec &candidate) const;

    [[nodiscard]] volt::PortDefId resolved_port_id(std::size_t port) const;

    [[nodiscard]] volt::ModuleComponentId resolved_module_component_id(std::size_t component) const;

    [[nodiscard]] std::size_t public_template_net_index(volt::TemplateNetDefId net) const;

    [[nodiscard]] std::size_t public_port_index(volt::PortDefId port) const;

    [[nodiscard]] std::size_t
    public_module_component_index(volt::ModuleComponentId component) const;

    [[nodiscard]] volt::Circuit materialized_circuit() const;

    [[nodiscard]] std::vector<volt::PinId> pins_by_name(volt::ComponentId component,
                                                        const std::string &name) const;

    [[nodiscard]] std::vector<volt::PinDefId>
    module_component_pins_by_name(std::size_t component, const std::string &name) const;

    volt::Circuit circuit_;
    std::vector<ModuleDraft> module_drafts_;
    std::vector<volt::PartLibrary> part_libraries_;
    std::size_t next_template_net_handle_ = 0;
    std::size_t next_port_handle_ = 0;
    std::size_t next_module_component_handle_ = 0;
};

} // namespace volt::python
