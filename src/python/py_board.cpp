#include "py_board.hpp"

#include "binding_pcb_conversions.hpp"
#include "py_board_helpers.hpp"
#include "py_circuit.hpp"

#include <algorithm>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/io/pcb/board_resolution.hpp>
#include <volt/io/pcb/pcb_svg_writer.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/assembly/cpl.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_router.hpp>

namespace volt::python {
namespace {

[[nodiscard]] volt::FootprintRef footprint_ref_from_key(py::handle key) {
    if (!py::isinstance<py::tuple>(key)) {
        throw py::type_error{"CPL rotation offset keys must be (library, name) tuples"};
    }
    const auto tuple = py::cast<py::tuple>(key);
    if (py::len(tuple) != 2U) {
        throw py::type_error{"CPL rotation offset keys must be (library, name) tuples"};
    }
    return volt::FootprintRef{py::cast<std::string>(tuple[0]), py::cast<std::string>(tuple[1])};
}

[[nodiscard]] volt::CplProjectionOptions
cpl_projection_options_from_dict(const py::dict &rotation_offsets) {
    auto options = volt::CplProjectionOptions{};
    options.rotation_offsets.reserve(static_cast<std::size_t>(py::len(rotation_offsets)));
    for (const auto item : rotation_offsets) {
        options.rotation_offsets.emplace_back(footprint_ref_from_key(item.first),
                                              py::cast<double>(item.second));
    }
    return options;
}

} // namespace

bool BoardNameByteLess::operator()(const volt::BoardName &lhs,
                                   const volt::BoardName &rhs) const noexcept {
    const auto &lhs_value = lhs.value();
    const auto &rhs_value = rhs.value();
    return std::lexicographical_compare(lhs_value.begin(), lhs_value.end(), rhs_value.begin(),
                                        rhs_value.end(), [](char left, char right) {
                                            return static_cast<unsigned char>(left) <
                                                   static_cast<unsigned char>(right);
                                        });
}

PyBoard::PyBoard(const PyCircuit &circuit, volt::BoardName name)
    : circuit_{&circuit}, board_{circuit.logical_circuit(), std::move(name)} {}

volt::BoardResolution PyBoard::resolve(std::vector<volt::BoardAssetCapability> additional) const {
    return volt::io::resolve_board(
        board_, circuit_->selected_part_bundle(),
        volt::BoardResolutionCapabilities{board_.capability_profile(), std::move(additional)});
}

std::string PyBoard::name() const { return board_.name().value(); }

std::string PyBoard::units() const { return "mm"; }

py::dict PyBoard::design_rules() const {
    const auto &rules = board_.design_rules();
    auto result = py::dict{};
    result["copper_clearance_mm"] = rules.copper_clearance_mm();
    result["minimum_track_width_mm"] = rules.minimum_track_width_mm();
    result["minimum_via_drill_diameter_mm"] = rules.minimum_via_drill_diameter_mm();
    result["minimum_via_annular_diameter_mm"] = rules.minimum_via_annular_diameter_mm();
    result["board_outline_clearance_mm"] = rules.board_outline_clearance_mm();
    result["package_assembly_clearance_mm"] = rules.package_assembly_clearance_mm();
    return result;
}

void PyBoard::set_design_rules(double copper_clearance_mm, double minimum_track_width_mm,
                               double minimum_via_drill_diameter_mm,
                               double minimum_via_annular_diameter_mm,
                               double board_outline_clearance_mm,
                               double package_assembly_clearance_mm) {
    board_.set_design_rules(volt::BoardDesignRules{
        copper_clearance_mm,
        minimum_track_width_mm,
        minimum_via_drill_diameter_mm,
        minimum_via_annular_diameter_mm,
        board_outline_clearance_mm,
        package_assembly_clearance_mm,
    });
}

void PyBoard::set_capability_profile(const py::dict &profile) {
    board_.set_capability_profile(board_capability_profile_from_dict(profile));
}

std::size_t PyBoard::add_layer(const std::string &name, const std::string &role,
                               const std::string &side, double thickness_mm, bool enabled,
                               std::optional<double> copper_weight_oz) {
    auto layer = volt::BoardLayer{name, parse_board_layer_role(role), parse_board_layer_side(side),
                                  thickness_mm, enabled};
    if (copper_weight_oz.has_value()) {
        layer.set_copper_weight_oz(copper_weight_oz.value());
    }
    return board_.add_layer(std::move(layer)).index();
}

void PyBoard::set_layer_stack(const std::vector<std::size_t> &layers, double board_thickness_mm,
                              const std::vector<std::pair<double, double>> &dielectrics) {
    auto layer_ids = std::vector<volt::BoardLayerId>{};
    layer_ids.reserve(layers.size());
    for (const auto layer : layers) {
        layer_ids.emplace_back(layer);
    }
    auto dielectric_specs = std::vector<volt::BoardDielectric>{};
    dielectric_specs.reserve(dielectrics.size());
    for (const auto &[thickness_mm, relative_permittivity] : dielectrics) {
        dielectric_specs.emplace_back(thickness_mm, relative_permittivity);
    }
    board_.set_layer_stack(
        volt::LayerStack{std::move(layer_ids), board_thickness_mm, std::move(dielectric_specs)});
}

void PyBoard::set_rectangular_outline(double x, double y, double width, double height) {
    board_.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{x, y}, volt::BoardSize{width, height}));
}

void PyBoard::set_polygon_outline(const std::vector<std::pair<double, double>> &vertices) {
    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(vertices.size());
    for (const auto &[x, y] : vertices) {
        points.emplace_back(x, y);
    }
    board_.set_outline(volt::BoardOutline{std::move(points)});
}

py::list PyBoard::outline_vertices() const {
    auto result = py::list{};
    const auto &outline = board_.outline();
    if (!outline.has_value()) {
        return result;
    }
    for (const auto point : outline->vertices()) {
        result.append(py::make_tuple(point.x_mm(), point.y_mm()));
    }
    return result;
}

std::size_t PyBoard::add_hole(const std::string &label, double x, double y,
                              double drill_diameter_mm, bool plated, const std::string &role,
                              std::optional<double> finished_diameter_mm) {
    return board_
        .add_feature(volt::BoardFeature::hole(label, volt::BoardPoint{x, y}, drill_diameter_mm,
                                              plated, role, finished_diameter_mm))
        .index();
}

std::size_t PyBoard::add_slot(const std::string &label, double start_x, double start_y,
                              double end_x, double end_y, double width_mm, bool plated,
                              const std::string &role) {
    return board_
        .add_feature(volt::BoardFeature::slot(label, volt::BoardPoint{start_x, start_y},
                                              volt::BoardPoint{end_x, end_y}, width_mm, plated,
                                              role))
        .index();
}

std::size_t PyBoard::add_cutout(const std::string &label,
                                const std::vector<std::pair<double, double>> &outline,
                                const std::string &role) {
    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(outline.size());
    for (const auto &[x, y] : outline) {
        points.emplace_back(x, y);
    }

    return board_.add_feature(volt::BoardFeature::cutout(label, std::move(points), role)).index();
}

std::size_t PyBoard::add_circle(const std::string &label, double x, double y, double diameter_mm,
                                const std::string &side, const std::string &role) {
    return board_
        .add_feature(volt::BoardFeature::circle(label, volt::BoardPoint{x, y}, diameter_mm,
                                                parse_board_side(side), role))
        .index();
}

std::size_t PyBoard::place_component(std::size_t component, double x, double y,
                                     double rotation_degrees, const std::string &side,
                                     bool locked) {
    return board_
        .place_component(volt::ComponentPlacement{component_id(component), volt::BoardPoint{x, y},
                                                  volt::BoardRotation::degrees(rotation_degrees),
                                                  parse_board_side(side), locked})
        .index();
}

py::list PyBoard::placement_refs() const {
    auto result = py::list{};
    const auto &board = board_;
    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto placement_id = volt::ComponentPlacementId{index};
        const auto &placement = board.get(placement_id);
        auto item = py::dict{};
        item["index"] = placement_id.index();
        item["component"] = placement.component().index();
        item["position"] = py::make_tuple(placement.position().x_mm(), placement.position().y_mm());
        item["rotation_deg"] = placement.rotation().degrees();
        item["side"] = board_side_name(placement.side());
        item["locked"] = placement.locked();
        result.append(std::move(item));
    }
    return result;
}

py::list PyBoard::placed_model_3d_refs() const {
    const auto resolution = resolve({volt::BoardAssetCapability::Models3D});
    auto result = py::list{};
    for (std::size_t index = 0; index < board_.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto placement_id = volt::ComponentPlacementId{index};
        const auto &placement = board_.get(placement_id);
        const auto &component = board_.circuit().get(placement.component());
        const auto *part = resolution.part(placement.component());
        auto item = py::dict{};
        item["placement"] = placement_id.index();
        item["component"] = placement.component().index();
        item["reference"] = component.reference().value();
        if (part == nullptr || !part->physical_part().model_3d().has_value()) {
            item["model"] = py::none{};
            item["bytes"] = py::none{};
        } else {
            const auto &model = *part->physical_part().model_3d();
            auto metadata = py::dict{};
            metadata["format"] = model.format();
            metadata["file_name"] = model.file_name();
            metadata["translation_mm"] = py::make_tuple(
                model.translation_mm()[0], model.translation_mm()[1], model.translation_mm()[2]);
            metadata["rotation_deg"] = model.rotation_deg();
            item["model"] = std::move(metadata);
            item["bytes"] = py::bytes{*part->model_3d_bytes()};
        }
        result.append(std::move(item));
    }
    return result;
}

py::list PyBoard::stackup() const {
    auto result = py::list{};
    const auto &board = board_;
    if (!board.layer_stack().has_value()) {
        return result;
    }
    const auto &stack = board.layer_stack().value();
    for (std::size_t index = 0; index < stack.layers().size(); ++index) {
        const auto layer_id = stack.layers()[index];
        const auto &layer = board.get(layer_id);
        auto item = py::dict{};
        item["index"] = layer_id.index();
        item["name"] = layer.name();
        item["side"] = board_layer_side_name(layer.side());
        item["z_mm"] = layer_z_mm(board, stack, index);
        result.append(std::move(item));
    }
    return result;
}

py::list PyBoard::component_footprint_pads(std::size_t component) const {
    const auto component_handle = component_id(component);
    static_cast<void>(board_.circuit().get(component_handle));
    const auto resolution = resolve();
    const auto &resolved_board = resolution.board();

    auto result = py::list{};
    const auto &selected_part =
        volt::queries::selected_physical_part(resolved_board.circuit(), component_handle);
    if (!selected_part.has_value()) {
        return result;
    }

    const auto resolution_footprints =
        volt::queries::board_resolution_footprints(resolved_board, resolution.footprints());
    const auto footprint_resolution =
        volt::resolve_footprint(selected_part.value(), resolution_footprints);
    const auto *definition = footprint_resolution.definition();
    if (definition == nullptr) {
        return result;
    }

    for (std::size_t index = 0; index < definition->pad_count(); ++index) {
        const auto pad_id = volt::FootprintPadId{index};
        const auto &pad = definition->pad(pad_id);
        const auto binding = std::find_if(footprint_resolution.pad_bindings().begin(),
                                          footprint_resolution.pad_bindings().end(),
                                          [pad_id](const volt::FootprintPadBinding &candidate) {
                                              return candidate.pad() == pad_id;
                                          });

        auto item = py::dict{};
        item["pad"] = pad_id.index();
        item["pad_label"] = pad.label();
        item["position"] = py::make_tuple(pad.position().x_mm(), pad.position().y_mm());
        item["pin"] = py::none{};
        if (binding != footprint_resolution.pad_bindings().end()) {
            const auto pin = queries::pin_by_definition(resolved_board.circuit(), component_handle,
                                                        binding->pin());
            if (pin.has_value()) {
                item["pin"] = pin->index();
            }
        }
        result.append(std::move(item));
    }

    return result;
}

std::size_t PyBoard::add_track(std::size_t net, std::size_t layer,
                               const std::vector<std::pair<double, double>> &points,
                               double width_mm) {
    auto board_points = std::vector<volt::BoardPoint>{};
    board_points.reserve(points.size());
    for (const auto &[x, y] : points) {
        board_points.emplace_back(x, y);
    }

    return board_
        .add_track(volt::BoardTrack{net_id(net), volt::BoardLayerId{layer}, std::move(board_points),
                                    width_mm})
        .index();
}

py::dict PyBoard::add_track_for_route(std::optional<std::size_t> net, std::size_t layer,
                                      const py::list &endpoints, double width_mm) {
    auto route_net = std::optional<volt::NetId>{};
    if (net.has_value()) {
        route_net = net_id(net.value());
    }

    const auto resolution = resolve();
    auto router = volt::BoardRouter{board_, resolution};
    const auto result = router.add_track(volt::BoardTrackRouteRequest{
        route_net,
        volt::BoardLayerId{layer},
        board_route_endpoints_from_list(endpoints),
        width_mm,
    });

    auto lowered = py::dict{};
    lowered["track"] = result.track.index();
    lowered["net"] = result.net.index();
    return lowered;
}

std::size_t PyBoard::track_net(std::size_t track) const {
    return board_.get(volt::BoardTrackId{track}).net().index();
}

std::size_t PyBoard::add_via(std::size_t net, double x, double y, std::size_t start_layer,
                             std::size_t end_layer, std::optional<double> drill_diameter_mm,
                             std::optional<double> annular_diameter_mm) {
    const auto net_id_value = net_id(net);
    auto &board = board_;
    const auto default_via_size = volt::resolve_via_size(
        board, net_id_value, default_authoring_via_drill_mm, default_authoring_via_annular_mm);
    const auto resolved_drill_diameter_mm =
        drill_diameter_mm.value_or(default_via_size.drill_diameter_mm);
    const auto resolved_annular_diameter_mm =
        annular_diameter_mm.value_or(default_via_size.annular_diameter_mm);
    if (resolved_annular_diameter_mm <= resolved_drill_diameter_mm) {
        throw py::value_error{
            "Resolved via annular diameter must be greater than drill diameter; specify both "
            "drill and annular for an explicit via size"};
    }
    return board
        .add_via(volt::BoardVia{net_id_value, volt::BoardPoint{x, y},
                                volt::BoardLayerId{start_layer}, volt::BoardLayerId{end_layer},
                                resolved_drill_diameter_mm, resolved_annular_diameter_mm})
        .index();
}

py::dict PyBoard::assisted_connect(std::size_t net, double start_x, double start_y,
                                   std::size_t start_layer, double end_x, double end_y,
                                   std::size_t end_layer) {
    const auto resolution = resolve();
    auto router = volt::BoardRouter{board_, resolution};
    const auto result = router.connect(volt::BoardRouteRequest{
        net_id(net), volt::BoardPoint{start_x, start_y}, volt::BoardPoint{end_x, end_y},
        volt::BoardLayerId{start_layer}, volt::BoardLayerId{end_layer}});

    auto tracks = py::list{};
    for (const auto track : result.tracks) {
        tracks.append(track.index());
    }
    auto vias = py::list{};
    for (const auto via : result.vias) {
        vias.append(via.index());
    }
    auto blockers = py::list{};
    for (const auto &blocker : result.blockers) {
        blockers.append(board_spatial_blocker_to_dict(blocker));
    }

    auto lowered = py::dict{};
    lowered["routed"] = result.routed;
    lowered["tracks"] = std::move(tracks);
    lowered["vias"] = std::move(vias);
    lowered["blockers"] = std::move(blockers);
    return lowered;
}

py::dict PyBoard::escape(std::size_t component) {
    const auto resolution = resolve();
    auto router = volt::BoardRouter{board_, resolution};
    const auto result = router.escape(component_id(component));

    auto pads = py::list{};
    for (const auto &pad : result.pads) {
        auto tracks = py::list{};
        for (const auto track : pad.tracks) {
            tracks.append(track.index());
        }
        auto vias = py::list{};
        for (const auto via : pad.vias) {
            vias.append(via.index());
        }
        auto blockers = py::list{};
        for (const auto &blocker : pad.blockers) {
            blockers.append(board_spatial_blocker_to_dict(blocker));
        }

        auto item = py::dict{};
        item["pad"] = pad.pad.index();
        item["pad_label"] = pad.pad_label;
        item["pin"] = optional_id_to_object(pad.pin);
        item["net"] = optional_id_to_object(pad.net);
        item["pad_position"] = py::make_tuple(pad.pad_position.x_mm(), pad.pad_position.y_mm());
        item["endpoint"] = py::make_tuple(pad.endpoint.x_mm(), pad.endpoint.y_mm());
        item["escaped"] = pad.escaped;
        item["failure_reason"] = board_escape_failure_reason_name(pad.failure_reason);
        item["tracks"] = std::move(tracks);
        item["vias"] = std::move(vias);
        item["blockers"] = std::move(blockers);
        pads.append(std::move(item));
    }

    auto lowered = py::dict{};
    lowered["complete"] = result.complete();
    lowered["component"] = result.component.index();
    lowered["placement"] = optional_id_to_object(result.placement);
    lowered["room"] = optional_id_to_object(result.room);
    lowered["pads"] = std::move(pads);
    return lowered;
}

std::size_t PyBoard::add_zone(std::optional<std::size_t> net,
                              const std::vector<std::size_t> &layers,
                              const std::vector<std::pair<double, double>> &outline,
                              const std::string &fill, int priority) {
    auto board_layers = std::vector<volt::BoardLayerId>{};
    board_layers.reserve(layers.size());
    for (const auto layer : layers) {
        board_layers.emplace_back(layer);
    }

    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(outline.size());
    for (const auto &[x, y] : outline) {
        points.emplace_back(x, y);
    }

    auto board_net = std::optional<volt::NetId>{};
    if (net.has_value()) {
        board_net = net_id(net.value());
    }

    return board_
        .add_zone(volt::BoardZone{std::move(points), std::move(board_layers), board_net,
                                  parse_board_zone_fill(fill), priority})
        .index();
}

std::size_t PyBoard::add_keepout(const std::vector<std::size_t> &layers,
                                 const std::vector<std::pair<double, double>> &outline,
                                 const std::vector<std::string> &restrictions) {
    auto board_layers = std::vector<volt::BoardLayerId>{};
    board_layers.reserve(layers.size());
    for (const auto layer : layers) {
        board_layers.emplace_back(layer);
    }

    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(outline.size());
    for (const auto &[x, y] : outline) {
        points.emplace_back(x, y);
    }

    auto keepout_restrictions = std::vector<volt::BoardKeepoutRestriction>{};
    keepout_restrictions.reserve(restrictions.size());
    for (const auto &restriction : restrictions) {
        keepout_restrictions.push_back(parse_board_keepout_restriction(restriction));
    }

    return board_
        .add_keepout(volt::BoardKeepout{std::move(points), std::move(board_layers),
                                        std::move(keepout_restrictions)})
        .index();
}

std::size_t PyBoard::add_room(const std::string &name,
                              const std::vector<std::pair<double, double>> &outline,
                              const std::vector<std::size_t> &layers,
                              std::optional<double> copper_clearance_mm,
                              std::optional<double> track_width_mm, int priority) {
    auto board_layers = std::vector<volt::BoardLayerId>{};
    board_layers.reserve(layers.size());
    for (const auto layer : layers) {
        board_layers.emplace_back(layer);
    }

    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(outline.size());
    for (const auto &[x, y] : outline) {
        points.emplace_back(x, y);
    }

    auto room = volt::BoardRoom{name, volt::BoardOutline{std::move(points)},
                                std::move(board_layers), priority};
    if (copper_clearance_mm.has_value()) {
        room.set_copper_clearance_mm(copper_clearance_mm.value());
    }
    if (track_width_mm.has_value()) {
        room.set_track_width_mm(track_width_mm.value());
    }

    return board_.add_room(std::move(room)).index();
}

std::size_t PyBoard::add_text(const std::string &text, double x, double y, std::size_t layer,
                              double rotation_degrees, double size_mm, bool locked) {
    return board_
        .add_text(volt::BoardText{text, volt::BoardPoint{x, y},
                                  volt::BoardRotation::degrees(rotation_degrees),
                                  volt::BoardLayerId{layer}, size_mm, locked})
        .index();
}

py::list PyBoard::resolve_pads() const {
    const auto board_resolution = resolve();
    auto result = py::list{};
    for (const auto &resolution :
         volt::queries::resolve_pads(board_resolution.board(), board_resolution.footprints())) {
        auto item = py::dict{};
        item["placement"] = resolution.placement().index();
        item["component"] = resolution.component().index();
        item["pad"] = resolution.pad().index();
        item["pad_label"] = resolution.pad_label();
        item["position"] =
            py::make_tuple(resolution.position().x_mm(), resolution.position().y_mm());
        item["pin"] =
            resolution.pin().has_value() ? py::cast(resolution.pin()->index()) : py::none{};
        item["net"] =
            resolution.net().has_value() ? py::cast(resolution.net()->index()) : py::none{};
        item["status"] = pad_resolution_status_name(resolution.status());
        result.append(std::move(item));
    }
    return result;
}

py::list PyBoard::validate() const {
    const auto resolution = resolve();
    return diagnostics_to_list(validate_board(resolution.board(), resolution.footprints()));
}

py::list PyBoard::validate_assembly(const py::dict &rotation_offsets) const {
    const auto options = cpl_projection_options_from_dict(rotation_offsets);
    const auto resolution = resolve();
    return diagnostics_to_list(
        volt::project_cpl(resolution.board(), resolution.footprints(), options).diagnostics());
}

std::string PyBoard::cpl_json(const py::dict &rotation_offsets) const {
    const auto options = cpl_projection_options_from_dict(rotation_offsets);
    const auto resolution = resolve();
    return volt::io::write_cpl_json(
        volt::project_cpl(resolution.board(), resolution.footprints(), options));
}

std::string PyBoard::cpl_csv(const py::dict &rotation_offsets) const {
    const auto options = cpl_projection_options_from_dict(rotation_offsets);
    const auto resolution = resolve();
    return volt::io::write_cpl_csv(
        volt::project_cpl(resolution.board(), resolution.footprints(), options));
}

std::string PyBoard::to_json() const {
    const auto resolution = resolve();
    return volt::io::write_pcb_board(resolution.board(), resolution.footprints());
}

std::string PyBoard::to_svg(bool pad_net_overlays, bool diagnostic_overlays, bool ratsnest_edges,
                            std::optional<std::size_t> layer_filter) const {
    auto options = volt::io::PcbPlacementSvgOptions{.pad_net_overlays = pad_net_overlays,
                                                    .diagnostic_overlays = diagnostic_overlays,
                                                    .ratsnest_edges = ratsnest_edges};
    if (layer_filter.has_value()) {
        options.layer_filter = volt::BoardLayerId{layer_filter.value()};
    }
    const auto resolution = resolve();
    return volt::io::write_pcb_placement_svg(resolution.board(), resolution.footprints(), options);
}

PyBoardRegistry::PyBoardRegistry(const PyCircuit &circuit) : circuit_{&circuit} {}

PyBoard &PyBoardRegistry::add(const std::string &name) {
    auto board_name = volt::BoardName{name};
    if (boards_.contains(board_name)) {
        throw volt::KernelLogicError{volt::ErrorCode::DuplicateName,
                                     "Board name already exists: " + name};
    }
    auto board = std::make_unique<PyBoard>(*circuit_, board_name);
    auto *result = board.get();
    boards_.emplace(std::move(board_name), std::move(board));
    return *result;
}

PyBoard &PyBoardRegistry::board(std::optional<std::string> name) {
    if (!name.has_value()) {
        if (boards_.size() != 1U) {
            const auto count = boards_.empty() ? std::string{"no Boards"}
                                               : std::to_string(boards_.size()) + " Boards";
            throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                         "Board registry has " + count};
        }
        return *boards_.begin()->second;
    }

    const auto board_name = volt::BoardName{*name};
    const auto found = boards_.find(board_name);
    if (found == boards_.end()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Board registry has no Board named " + *name};
    }
    return *found->second;
}

py::tuple PyBoardRegistry::names() const {
    auto result = py::tuple{boards_.size()};
    auto index = std::size_t{0};
    for (const auto &[name, board] : boards_) {
        static_cast<void>(board);
        result[index] = name.value();
        ++index;
    }
    return result;
}

} // namespace volt::python
