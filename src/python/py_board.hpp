#pragma once

#include "binding_conversions.hpp"

#include <volt/pcb/board.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace volt::python {

class PyCircuit;

/** Ascending unsigned UTF-8 byte ordering for exact BoardName registry keys. */
struct BoardNameByteLess {
    [[nodiscard]] bool operator()(const volt::BoardName &lhs,
                                  const volt::BoardName &rhs) const noexcept;
};

/** Direct bound owner for one native physical Board over a retained Circuit. */
class PyBoard {
  public:
    PyBoard(const PyCircuit &circuit, volt::BoardName name);

    [[nodiscard]] std::string name() const;

    [[nodiscard]] std::string units() const;

    [[nodiscard]] py::dict design_rules() const;

    void set_design_rules(double copper_clearance_mm, double minimum_track_width_mm,
                          double minimum_via_drill_diameter_mm,
                          double minimum_via_annular_diameter_mm, double board_outline_clearance_mm,
                          double package_assembly_clearance_mm);

    void set_capability_profile(const py::dict &profile);

    [[nodiscard]] std::size_t add_layer(const std::string &name, const std::string &role,
                                        const std::string &side, double thickness_mm, bool enabled,
                                        std::optional<double> copper_weight_oz);

    void set_layer_stack(const std::vector<std::size_t> &layers, double board_thickness_mm,
                         const std::vector<std::pair<double, double>> &dielectrics);

    void set_rectangular_outline(double x, double y, double width, double height);

    void set_polygon_outline(const std::vector<std::pair<double, double>> &vertices);

    [[nodiscard]] py::list outline_vertices() const;

    [[nodiscard]] std::size_t add_hole(const std::string &label, double x, double y,
                                       double drill_diameter_mm, bool plated,
                                       const std::string &role,
                                       std::optional<double> finished_diameter_mm);

    [[nodiscard]] std::size_t add_slot(const std::string &label, double start_x, double start_y,
                                       double end_x, double end_y, double width_mm, bool plated,
                                       const std::string &role);

    [[nodiscard]] std::size_t add_cutout(const std::string &label,
                                         const std::vector<std::pair<double, double>> &outline,
                                         const std::string &role);

    [[nodiscard]] std::size_t add_circle(const std::string &label, double x, double y,
                                         double diameter_mm, const std::string &side,
                                         const std::string &role);

    [[nodiscard]] std::size_t cache_footprint_definition(const py::dict &definition);

    [[nodiscard]] std::size_t place_component(std::size_t component, double x, double y,
                                              double rotation_degrees, const std::string &side,
                                              bool locked);

    [[nodiscard]] py::list placement_refs() const;

    [[nodiscard]] py::list stackup() const;

    [[nodiscard]] py::list component_footprint_pads(std::size_t component) const;

    [[nodiscard]] std::size_t add_track(std::size_t net, std::size_t layer,
                                        const std::vector<std::pair<double, double>> &points,
                                        double width_mm);

    [[nodiscard]] py::dict add_track_for_route(std::optional<std::size_t> net, std::size_t layer,
                                               const py::list &endpoints, double width_mm);

    [[nodiscard]] std::size_t track_net(std::size_t track) const;

    [[nodiscard]] std::size_t add_via(std::size_t net, double x, double y, std::size_t start_layer,
                                      std::size_t end_layer,
                                      std::optional<double> drill_diameter_mm,
                                      std::optional<double> annular_diameter_mm);

    [[nodiscard]] py::dict assisted_connect(std::size_t net, double start_x, double start_y,
                                            std::size_t start_layer, double end_x, double end_y,
                                            std::size_t end_layer);

    [[nodiscard]] py::dict escape(std::size_t component);

    [[nodiscard]] std::size_t add_zone(std::optional<std::size_t> net,
                                       const std::vector<std::size_t> &layers,
                                       const std::vector<std::pair<double, double>> &outline,
                                       const std::string &fill, int priority);

    [[nodiscard]] std::size_t add_keepout(const std::vector<std::size_t> &layers,
                                          const std::vector<std::pair<double, double>> &outline,
                                          const std::vector<std::string> &restrictions);

    [[nodiscard]] std::size_t add_room(const std::string &name,
                                       const std::vector<std::pair<double, double>> &outline,
                                       const std::vector<std::size_t> &layers,
                                       std::optional<double> copper_clearance_mm,
                                       std::optional<double> track_width_mm, int priority);

    [[nodiscard]] std::size_t add_text(const std::string &text, double x, double y,
                                       std::size_t layer, double rotation_degrees, double size_mm,
                                       bool locked);

    [[nodiscard]] py::list resolve_pads() const;

    [[nodiscard]] py::list validate() const;

    [[nodiscard]] py::list validate_assembly(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string cpl_json(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string cpl_csv(const py::dict &rotation_offsets) const;

    [[nodiscard]] std::string to_json() const;

    [[nodiscard]] std::string to_svg(bool pad_net_overlays, bool diagnostic_overlays,
                                     bool ratsnest_edges,
                                     std::optional<std::size_t> layer_filter) const;

    [[nodiscard]] py::dict to_kicad_pcb() const;

    [[nodiscard]] py::dict to_fabrication_files() const;

  private:
    volt::Board board_;
};

/** Design-scoped exact-name owner registry for separately bound Boards. */
class PyBoardRegistry {
  public:
    explicit PyBoardRegistry(const PyCircuit &circuit);

    [[nodiscard]] PyBoard &add(const std::string &name);

    [[nodiscard]] PyBoard &board(std::optional<std::string> name = std::nullopt);

    [[nodiscard]] py::tuple names() const;

  private:
    const PyCircuit *circuit_;
    std::map<volt::BoardName, std::unique_ptr<PyBoard>, BoardNameByteLess> boards_;
};

} // namespace volt::python
