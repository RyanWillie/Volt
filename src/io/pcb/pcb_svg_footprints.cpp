#include <volt/io/pcb/pcb_svg_writer.hpp>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb/pcb_schema.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/projection/footprint_visual_projection.hpp>

namespace volt::io::detail {
namespace {

[[nodiscard]] std::string pcb_reference_label(const ComponentInstance &component) {
    const auto key = PropertyKey{"pcb_reference"};
    if (component.properties().contains(key)) {
        const auto &value = component.properties().get(key);
        if (value.kind() == PropertyValueKind::String) {
            return value.as_string();
        }
    }
    return component.reference().value();
}

void include_local_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                                    const ::volt::detail::FootprintVisualBounds &local_bounds) {
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.min_x, local_bounds.min_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.max_x, local_bounds.min_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.max_x, local_bounds.max_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.min_x, local_bounds.max_y}));
}

void include_projected_polygon_bounds(PcbSvgBounds &bounds,
                                      const std::optional<std::vector<BoardPoint>> &polygon) {
    if (!polygon.has_value()) {
        return;
    }
    for (const auto point : polygon.value()) {
        include_board_point(bounds, point);
    }
}

void write_projected_footprint_polygon(std::ostream &out, std::string_view class_name,
                                       const ProjectedFootprintGeometry &geometry,
                                       const std::vector<BoardPoint> &points) {
    out << "      <polygon class=\"" << class_name << "\" data-placement=\""
        << encode_local_id(geometry.placement()) << "\" data-component=\""
        << encode_local_id(geometry.component()) << "\" points=\"";
    write_pcb_point_list(out, points);
    out << "\"/>\n";
}

[[nodiscard]] bool projected_geometry_selected(const Board &board,
                                               const ProjectedFootprintGeometry &geometry,
                                               PcbPlacementSvgOptions options) {
    if (geometry.placement().index() >= board.placement_count()) {
        return false;
    }
    return placement_selected(board, board.placement(geometry.placement()), options);
}

void write_projected_footprint_polygon_layer(
    std::ostream &out, const Board &board,
    const std::vector<ProjectedFootprintGeometry> &footprint_geometries,
    PcbPlacementSvgOptions options, std::string_view layer_class, std::string_view polygon_class,
    const std::optional<std::vector<BoardPoint>> &(ProjectedFootprintGeometry::*member)() const) {
    out << "      <g class=\"layer " << layer_class << "\">\n";
    for (const auto &geometry : footprint_geometries) {
        const auto &polygon = (geometry.*member)();
        if (!projected_geometry_selected(board, geometry, options) || !polygon.has_value()) {
            continue;
        }
        write_projected_footprint_polygon(out, polygon_class, geometry, polygon.value());
    }
    out << "      </g>\n";
}

void write_projected_footprint_marking_layer(
    std::ostream &out, const Board &board,
    const std::vector<ProjectedFootprintGeometry> &footprint_geometries,
    PcbPlacementSvgOptions options) {
    out << "      <g class=\"layer layer-silkscreen\">\n";
    for (const auto &geometry : footprint_geometries) {
        if (!projected_geometry_selected(board, geometry, options)) {
            continue;
        }
        for (std::size_t index = 0; index < geometry.markings().size(); ++index) {
            const auto &marking = geometry.markings()[index];
            out << "      <polygon class=\"footprint-marking declared kind-"
                << footprint_marking_kind_name(marking.kind()) << "\" data-placement=\""
                << encode_local_id(geometry.placement()) << "\" data-component=\""
                << encode_local_id(geometry.component()) << "\" data-marking=\""
                << encode_local_id(FootprintMarkingId{index}) << "\" points=\"";
            write_pcb_point_list(out, marking.polygon());
            out << "\"/>\n";
        }
    }
    out << "      </g>\n";
}

void write_package_geometry_layers(
    std::ostream &out, const Board &board,
    const std::vector<ProjectedFootprintGeometry> &footprint_geometries,
    PcbPlacementSvgOptions options) {
    write_projected_footprint_polygon_layer(
        out, board, footprint_geometries, options, "layer-package-courtyards",
        "footprint-courtyard declared", &ProjectedFootprintGeometry::courtyard);
    write_projected_footprint_polygon_layer(out, board, footprint_geometries, options,
                                            "layer-package-bodies", "footprint-body declared",
                                            &ProjectedFootprintGeometry::body);
    write_projected_footprint_polygon_layer(
        out, board, footprint_geometries, options, "layer-package-fabrication",
        "footprint-fabrication declared", &ProjectedFootprintGeometry::fabrication_outline);
    write_projected_footprint_polygon_layer(out, board, footprint_geometries, options,
                                            "layer-package-assembly", "footprint-assembly declared",
                                            &ProjectedFootprintGeometry::assembly_outline);
    write_projected_footprint_marking_layer(out, board, footprint_geometries, options);
}

void write_synthetic_footprint_geometry(std::ostream &out, const FootprintDefinition &definition) {
    const auto local_bounds = ::volt::detail::synthetic_footprint_envelope(definition);
    out << "        <rect class=\"footprint-envelope synthetic\" x=\"";
    write_pcb_svg_number(out, local_bounds.min_x);
    out << "\" y=\"";
    write_pcb_svg_number(out, local_bounds.min_y);
    out << "\" width=\"";
    write_pcb_svg_number(out, local_bounds.max_x - local_bounds.min_x);
    out << "\" height=\"";
    write_pcb_svg_number(out, local_bounds.max_y - local_bounds.min_y);
    out << "\"/>\n";
}

void write_reference_designator(std::ostream &out, ComponentPlacementId placement_id,
                                const ComponentPlacement &placement,
                                const FootprintDefinition &definition,
                                const ComponentInstance &component) {
    const auto anchor = ::volt::detail::default_reference_designator_anchor(placement, definition);
    out << "      <text class=\"reference-designator\" data-placement=\""
        << encode_local_id(placement_id) << "\" data-component=\""
        << encode_local_id(placement.component()) << "\" x=\"";
    write_pcb_svg_number(out, anchor.x_mm());
    out << "\" y=\"";
    write_pcb_svg_number(out, anchor.y_mm());
    out << "\" text-anchor=\"middle\">" << pcb_svg_escape(pcb_reference_label(component))
        << "</text>\n";
}

[[nodiscard]] bool reference_designator_suppressed(const DiagnosticReport &diagnostics,
                                                   ComponentPlacementId placement) {
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code().value() != pcb_visual_diagnostic_codes::ReferenceDesignatorHidden) {
            continue;
        }
        if (diagnostic.entities().size() >= 2U &&
            diagnostic.entities()[1] == EntityRef::component_placement(placement)) {
            return true;
        }
    }
    return false;
}

} // namespace

[[nodiscard]] const ProjectedFootprintGeometry *projected_footprint_geometry_for_placement(
    const std::vector<ProjectedFootprintGeometry> &geometries, ComponentPlacementId placement) {
    const auto match = std::find_if(geometries.begin(), geometries.end(),
                                    [placement](const ProjectedFootprintGeometry &geometry) {
                                        return geometry.placement() == placement;
                                    });
    if (match == geometries.end()) {
        return nullptr;
    }
    return &*match;
}

[[nodiscard]] PcbSvgBounds footprint_local_bounds(const FootprintDefinition &definition) {
    const auto bounds = ::volt::detail::footprint_visual_bounds(definition);
    return PcbSvgBounds{bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y};
}

void include_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                              const FootprintDefinition &definition,
                              const ProjectedFootprintGeometry *projected_geometry) {
    if (!::volt::detail::footprint_has_declared_visual_geometry(definition)) {
        include_local_footprint_bounds(bounds, placement,
                                       ::volt::detail::synthetic_footprint_envelope(definition));
        return;
    }

    include_local_footprint_bounds(bounds, placement,
                                   ::volt::detail::footprint_pad_bounds(definition));
    if (projected_geometry == nullptr) {
        include_local_footprint_bounds(bounds, placement,
                                       ::volt::detail::footprint_visual_bounds(definition));
        return;
    }
    include_projected_polygon_bounds(bounds, projected_geometry->courtyard());
    include_projected_polygon_bounds(bounds, projected_geometry->body());
    include_projected_polygon_bounds(bounds, projected_geometry->fabrication_outline());
    include_projected_polygon_bounds(bounds, projected_geometry->assembly_outline());
    for (const auto &marking : projected_geometry->markings()) {
        for (const auto point : marking.polygon()) {
            include_board_point(bounds, point);
        }
    }
}

[[nodiscard]] std::string pad_shape_class(FootprintPadShape shape) {
    switch (shape) {
    case FootprintPadShape::Rectangle:
        return "rectangle";
    case FootprintPadShape::RoundedRectangle:
        return "rounded-rectangle";
    case FootprintPadShape::Circle:
        return "circle";
    case FootprintPadShape::Oval:
        return "oval";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint pad shape"};
}

void write_pad(std::ostream &out, const FootprintPad &pad, FootprintPadId pad_id,
               const PadResolution *resolution) {
    const auto status = resolution == nullptr ? std::string{"invalid"}
                                              : pad_resolution_status_name(resolution->status());
    const auto class_name = "footprint-pad " + pad_shape_class(pad.shape()) + " " + status;

    const auto write_common_attributes = [&]() {
        out << " class=\"" << class_name << "\"";
        if (resolution != nullptr) {
            out << " data-pad-projection=\""
                << pcb_pad_projection_id(resolution->placement(), resolution->pad()) << "\"";
        }
        out << " data-pad=\"" << encode_local_id(pad_id) << "\"";
        if (resolution != nullptr && resolution->pin().has_value()) {
            out << " data-pin=\"" << encode_local_id(resolution->pin().value()) << "\"";
        }
        if (resolution != nullptr && resolution->net().has_value()) {
            out << " data-net=\"" << encode_local_id(resolution->net().value()) << "\"";
        }
    };

    const auto x = pad.position().x_mm() - (pad.size().width_mm() / 2.0);
    const auto y = pad.position().y_mm() - (pad.size().height_mm() / 2.0);
    if (pad.shape() == FootprintPadShape::Circle) {
        out << "        <circle";
        write_common_attributes();
        out << " cx=\"";
        write_pcb_svg_number(out, pad.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, pad.position().y_mm());
        out << "\" r=\"";
        write_pcb_svg_number(out, std::min(pad.size().width_mm(), pad.size().height_mm()) / 2.0);
        out << "\"/>\n";
        return;
    }
    if (pad.shape() == FootprintPadShape::Oval) {
        out << "        <ellipse";
        write_common_attributes();
        out << " cx=\"";
        write_pcb_svg_number(out, pad.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, pad.position().y_mm());
        out << "\" rx=\"";
        write_pcb_svg_number(out, pad.size().width_mm() / 2.0);
        out << "\" ry=\"";
        write_pcb_svg_number(out, pad.size().height_mm() / 2.0);
        out << "\"/>\n";
        return;
    }

    out << "        <rect";
    write_common_attributes();
    out << " x=\"";
    write_pcb_svg_number(out, x);
    out << "\" y=\"";
    write_pcb_svg_number(out, y);
    out << "\" width=\"";
    write_pcb_svg_number(out, pad.size().width_mm());
    out << "\" height=\"";
    write_pcb_svg_number(out, pad.size().height_mm());
    out << '"';
    if (pad.shape() == FootprintPadShape::RoundedRectangle) {
        out << " rx=\"0.1\" ry=\"0.1\"";
    }
    out << "/>\n";
}

void write_placements(std::ostream &out, const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions,
                      const std::vector<ProjectedFootprintGeometry> &footprint_geometries,
                      const DiagnosticReport &diagnostics, PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-footprints\">\n";
    write_package_geometry_layers(out, board, footprint_geometries, options);
    out << "      <g class=\"layer layer-pads\">\n";
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto *definition = resolve_definition_for_placement(board, placement, footprints);
        if (definition == nullptr) {
            continue;
        }
        const auto placement_context_selected = placement_selected(board, placement, options);
        auto has_selected_pad = !options.layer_filter.has_value();
        if (options.layer_filter.has_value()) {
            for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
                if (pad_selected_for_layer(board, definition->pad(FootprintPadId{pad_index}),
                                           placement.side(), options.layer_filter.value())) {
                    has_selected_pad = true;
                    break;
                }
            }
        }
        if (!placement_context_selected && !has_selected_pad) {
            continue;
        }

        out << "      <g class=\"component-placement " << board_side_name(placement.side());
        if (placement.locked()) {
            out << " locked";
        }
        out << "\" data-placement=\"" << encode_local_id(placement_id) << "\" data-component=\""
            << encode_local_id(placement.component()) << '"';
        const auto footprint_definition_id =
            projection_footprint_definition_id_for_placement(board, placement_id, footprints);
        if (footprint_definition_id.has_value()) {
            out << " data-footprint-def=\"" << encode_local_id(footprint_definition_id.value())
                << '"';
        }
        out << " data-footprint=\"" << pcb_svg_escape(footprint_ref_token(definition->ref()))
            << "\" transform=\"translate(";
        write_pcb_svg_number(out, placement.position().x_mm());
        out << ' ';
        write_pcb_svg_number(out, placement.position().y_mm());
        out << ") rotate(";
        write_pcb_svg_number(out, placement.rotation().degrees());
        out << ')';
        if (placement.side() == BoardSide::Bottom) {
            out << " scale(-1 1)";
        }
        out << "\">\n";
        if (placement_context_selected &&
            !::volt::detail::footprint_has_declared_visual_geometry(*definition)) {
            write_synthetic_footprint_geometry(out, *definition);
        }
        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            if (options.layer_filter.has_value() &&
                !pad_selected_for_layer(board, definition->pad(pad_id), placement.side(),
                                        options.layer_filter.value())) {
                continue;
            }
            write_pad(out, definition->pad(pad_id), pad_id,
                      find_pad_resolution(resolutions, placement_id, pad_id));
        }
        out << "      </g>\n";
    }
    out << "      </g>\n";
    out << "      <g class=\"layer layer-reference-designators\">\n";
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto *definition = resolve_definition_for_placement(board, placement, footprints);
        if (definition == nullptr || !placement_selected(board, placement, options) ||
            reference_designator_suppressed(diagnostics, placement_id)) {
            continue;
        }
        const auto &component = board.circuit().component(placement.component());
        write_reference_designator(out, placement_id, placement, *definition, component);
    }
    out << "      </g>\n";
    out << "    </g>\n";
}

} // namespace volt::io::detail
