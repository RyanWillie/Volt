#pragma once

#include "binding_conversions.hpp"

#include <volt/pcb/board.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace volt::python {

using PyConnectivityEndpoint = std::variant<std::size_t, std::pair<std::size_t, std::size_t>>;

class PyCircuit {
  public:
    PyCircuit();

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
                     const std::string &source_version, const py::list &schematic_symbols);

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

    [[nodiscard]] std::size_t schematic_sheet(const std::string &name, const py::dict &metadata);

    [[nodiscard]] std::size_t schematic_region(std::size_t sheet, const py::dict &region_data);

    [[nodiscard]] std::size_t register_schematic_symbol(const py::dict &symbol_data);

    [[nodiscard]] std::size_t place_schematic_symbol(std::size_t sheet, std::size_t component,
                                                     const std::string &symbol, double x, double y,
                                                     const std::string &orientation,
                                                     std::optional<std::size_t> authored_region);

    [[nodiscard]] std::string schematic_symbol_orientation(std::size_t instance);

    [[nodiscard]] std::pair<double, double> schematic_symbol_pin_anchor(std::size_t instance,
                                                                        const std::string &number);

    [[nodiscard]] py::list schematic_symbol_pin_refs(std::size_t instance);

    [[nodiscard]] std::size_t
    add_schematic_wire(std::size_t sheet, std::size_t net,
                       const std::vector<std::pair<double, double>> &points,
                       const std::string &route_intent, std::optional<std::size_t> authored_region);

    [[nodiscard]] py::tuple
    add_schematic_wire_for_endpoints(std::size_t sheet, std::optional<std::size_t> net,
                                     const std::vector<std::pair<double, double>> &points,
                                     const py::list &endpoints, const std::string &route_intent,
                                     std::optional<std::size_t> authored_region);

    [[nodiscard]] std::size_t add_schematic_net_label(std::size_t sheet, std::size_t net, double x,
                                                      double y, const std::string &orientation,
                                                      std::optional<std::size_t> authored_region,
                                                      std::optional<std::string> label,
                                                      const std::string &horizontal_alignment,
                                                      const std::string &vertical_alignment,
                                                      std::optional<double> font_size);

    [[nodiscard]] py::tuple add_schematic_net_label_for_endpoint(
        std::size_t sheet, std::optional<std::size_t> net, const py::tuple &endpoint,
        const std::string &orientation, std::optional<std::size_t> authored_region,
        std::optional<std::string> label, const std::string &horizontal_alignment,
        const std::string &vertical_alignment, std::optional<double> font_size);

    [[nodiscard]] std::size_t add_schematic_junction(std::size_t sheet, std::size_t net, double x,
                                                     double y,
                                                     std::optional<std::size_t> authored_region);

    [[nodiscard]] py::tuple
    add_schematic_junction_for_endpoint(std::size_t sheet, std::optional<std::size_t> net,
                                        const py::tuple &endpoint,
                                        std::optional<std::size_t> authored_region);

    [[nodiscard]] std::size_t
    add_schematic_terminal_marker(std::size_t sheet, std::size_t net, const std::string &kind,
                                  double x, double y, const std::string &orientation,
                                  std::optional<std::size_t> authored_region,
                                  std::optional<std::string> label);

    [[nodiscard]] py::tuple add_schematic_terminal_marker_for_endpoint(
        std::size_t sheet, std::optional<std::size_t> net, const std::string &kind,
        const py::tuple &endpoint, const std::string &orientation,
        std::optional<std::size_t> authored_region, std::optional<std::string> label);

    [[nodiscard]] std::size_t
    add_schematic_no_connect_marker(std::size_t sheet, std::size_t pin, double x, double y,
                                    const std::string &orientation, const std::string &reason,
                                    std::optional<std::size_t> authored_region);

    [[nodiscard]] std::size_t add_schematic_sheet_port(std::size_t sheet, std::size_t net,
                                                       const std::string &name,
                                                       const std::string &kind, double x, double y,
                                                       const std::string &orientation,
                                                       std::optional<std::size_t> authored_region);

    [[nodiscard]] py::tuple
    add_schematic_sheet_port_for_endpoint(std::size_t sheet, std::optional<std::size_t> net,
                                          const std::string &name, const std::string &kind,
                                          const py::tuple &endpoint, const std::string &orientation,
                                          std::optional<std::size_t> authored_region);

    [[nodiscard]] std::size_t add_schematic_symbol_field(
        std::size_t sheet, std::size_t instance, const std::string &name, const std::string &value,
        double x, double y, const std::string &orientation,
        std::optional<std::size_t> authored_region, const std::string &horizontal_alignment,
        const std::string &vertical_alignment, std::optional<double> font_size);

    [[nodiscard]] std::string schematic_to_json();

    [[nodiscard]] std::string schematic_to_svg();

    [[nodiscard]] std::string schematic_to_body_svg(std::size_t sheet, double margin);

    [[nodiscard]] py::list schematic_svg_pages();

    void load_schematic_json(const std::string &text);

    [[nodiscard]] std::vector<std::string> schematic_sheet_names() const;

    [[nodiscard]] py::list validate() const;

    [[nodiscard]] py::list validate_schematic();

    [[nodiscard]] py::list validate_schematic_readability();

    [[nodiscard]] py::list validate_for_pcb() const;

    [[nodiscard]] py::list validate_bom_readiness() const;

    [[nodiscard]] std::string bom_json(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string bom_csv(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string bom_sourcing_snapshot_json(const py::dict &sourcing_snapshot) const;

    [[nodiscard]] std::string to_json() const;

    [[nodiscard]] py::dict board(const std::string &name);

    [[nodiscard]] py::dict board_design_rules() const;

    void board_set_design_rules(double copper_clearance_mm, double minimum_track_width_mm,
                                double minimum_via_drill_diameter_mm,
                                double minimum_via_annular_diameter_mm,
                                double board_outline_clearance_mm,
                                double package_assembly_clearance_mm);

    void board_set_capability_profile(const py::dict &profile);

    [[nodiscard]] std::size_t board_add_layer(const std::string &name, const std::string &role,
                                              const std::string &side, double thickness_mm,
                                              bool enabled, std::optional<double> copper_weight_oz);

    void board_set_layer_stack(const std::vector<std::size_t> &layers, double board_thickness_mm,
                               const std::vector<std::pair<double, double>> &dielectrics);

    void board_set_rectangular_outline(double x, double y, double width, double height);

    void board_set_polygon_outline(const std::vector<std::pair<double, double>> &vertices);

    [[nodiscard]] py::list board_outline_vertices() const;

    [[nodiscard]] std::size_t board_add_hole(const std::string &label, double x, double y,
                                             double drill_diameter_mm, bool plated,
                                             const std::string &role,
                                             std::optional<double> finished_diameter_mm);

    [[nodiscard]] std::size_t board_add_slot(const std::string &label, double start_x,
                                             double start_y, double end_x, double end_y,
                                             double width_mm, bool plated, const std::string &role);

    [[nodiscard]] std::size_t
    board_add_cutout(const std::string &label,
                     const std::vector<std::pair<double, double>> &outline,
                     const std::string &role);

    [[nodiscard]] std::size_t board_add_circle(const std::string &label, double x, double y,
                                               double diameter_mm, const std::string &side,
                                               const std::string &role);

    [[nodiscard]] std::size_t board_cache_footprint_definition(const py::dict &definition);

    [[nodiscard]] std::size_t board_place_component(std::size_t component, double x, double y,
                                                    double rotation_degrees,
                                                    const std::string &side, bool locked);

    [[nodiscard]] py::list board_placement_refs() const;

    [[nodiscard]] py::list board_stackup() const;

    [[nodiscard]] py::list board_component_footprint_pads(std::size_t component) const;

    [[nodiscard]] std::size_t board_add_track(std::size_t net, std::size_t layer,
                                              const std::vector<std::pair<double, double>> &points,
                                              double width_mm);

    [[nodiscard]] py::dict board_add_track_for_route(std::optional<std::size_t> net,
                                                     std::size_t layer, const py::list &endpoints,
                                                     double width_mm);

    [[nodiscard]] std::size_t board_track_net(std::size_t track) const;

    [[nodiscard]] std::size_t board_add_via(std::size_t net, double x, double y,
                                            std::size_t start_layer, std::size_t end_layer,
                                            std::optional<double> drill_diameter_mm,
                                            std::optional<double> annular_diameter_mm);

    [[nodiscard]] py::dict board_assisted_connect(std::size_t net, double start_x, double start_y,
                                                  std::size_t start_layer, double end_x,
                                                  double end_y, std::size_t end_layer);

    [[nodiscard]] py::dict board_escape(std::size_t component);

    [[nodiscard]] std::size_t board_add_zone(std::optional<std::size_t> net,
                                             const std::vector<std::size_t> &layers,
                                             const std::vector<std::pair<double, double>> &outline,
                                             const std::string &fill, int priority);

    [[nodiscard]] std::size_t
    board_add_keepout(const std::vector<std::size_t> &layers,
                      const std::vector<std::pair<double, double>> &outline,
                      const std::vector<std::string> &restrictions);

    [[nodiscard]] std::size_t board_add_room(const std::string &name,
                                             const std::vector<std::pair<double, double>> &outline,
                                             const std::vector<std::size_t> &layers,
                                             std::optional<double> copper_clearance_mm,
                                             std::optional<double> track_width_mm, int priority);

    [[nodiscard]] std::size_t board_add_text(const std::string &text, double x, double y,
                                             std::size_t layer, double rotation_degrees,
                                             double size_mm, bool locked);

    [[nodiscard]] py::list board_resolve_pads() const;

    [[nodiscard]] py::list board_validate() const;

    [[nodiscard]] py::list board_validate_assembly(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string board_cpl_json(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string board_cpl_csv(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string board_to_json() const;

    [[nodiscard]] std::string board_to_svg(bool pad_net_overlays, bool diagnostic_overlays,
                                           bool ratsnest_edges,
                                           std::optional<std::size_t> layer_filter) const;

    [[nodiscard]] py::dict board_to_kicad_pcb() const;

    [[nodiscard]] py::dict board_to_fabrication_files() const;

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

    [[nodiscard]] volt::Schematic &schematic_projection();

    [[nodiscard]] volt::SymbolDefId ensure_schematic_symbol(const std::string &name);

    [[nodiscard]] volt::Board &board_projection(const std::string &name);

    [[nodiscard]] volt::Board &board_projection();

    [[nodiscard]] const volt::Board &board_projection() const;

    volt::Circuit circuit_;
    volt::SchematicDocument schematic_document_;
    std::optional<volt::Board> board_projection_;
    std::vector<ModuleDraft> module_drafts_;
    std::size_t next_template_net_handle_ = 0;
    std::size_t next_port_handle_ = 0;
    std::size_t next_module_component_handle_ = 0;
};

} // namespace volt::python
