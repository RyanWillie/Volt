#pragma once

#include "binding_conversions.hpp"

#include <volt/schematic/schematic_document.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace volt::python {

class PyCircuit;

/** Direct bound owner for one native schematic document over a retained Circuit. */
class PySchematicDocument {
  public:
    explicit PySchematicDocument(const PyCircuit &circuit);

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

    [[nodiscard]] py::list validate_schematic();

    [[nodiscard]] py::list validate_schematic_readability();

  private:
    [[nodiscard]] volt::Schematic &schematic_projection();

    [[nodiscard]] volt::SymbolDefId ensure_schematic_symbol(const std::string &name);

    volt::SchematicDocument schematic_document_;
};

} // namespace volt::python
