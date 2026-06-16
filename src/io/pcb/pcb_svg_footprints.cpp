#include <volt/io/pcb/pcb_svg_writer.hpp>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb/pcb_schema.hpp>
#include <volt/pcb/board.hpp>

namespace volt::io::detail {
namespace {

void include_footprint_point(PcbSvgBounds &bounds, FootprintPoint point) {
    bounds.min_x = std::min(bounds.min_x, point.x_mm());
    bounds.min_y = std::min(bounds.min_y, point.y_mm());
    bounds.max_x = std::max(bounds.max_x, point.x_mm());
    bounds.max_y = std::max(bounds.max_y, point.y_mm());
}

void include_local_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                                    const PcbSvgBounds &local_bounds) {
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

[[nodiscard]] bool footprint_has_declared_geometry(const FootprintDefinition &definition) {
    return definition.body().has_value() || definition.courtyard().has_value();
}

[[nodiscard]] PcbSvgBounds footprint_pad_bounds(const FootprintDefinition &definition) {
    const auto &first_pad = definition.pad(FootprintPadId{0});
    const auto first_half_width = first_pad.size().width_mm() / 2.0;
    const auto first_half_height = first_pad.size().height_mm() / 2.0;
    auto bounds = PcbSvgBounds{first_pad.position().x_mm() - first_half_width,
                               first_pad.position().y_mm() - first_half_height,
                               first_pad.position().x_mm() + first_half_width,
                               first_pad.position().y_mm() + first_half_height};
    for (std::size_t index = 1; index < definition.pad_count(); ++index) {
        const auto &pad = definition.pad(FootprintPadId{index});
        const auto half_width = pad.size().width_mm() / 2.0;
        const auto half_height = pad.size().height_mm() / 2.0;
        include_footprint_point(bounds, FootprintPoint{pad.position().x_mm() - half_width,
                                                       pad.position().y_mm() - half_height});
        include_footprint_point(bounds, FootprintPoint{pad.position().x_mm() + half_width,
                                                       pad.position().y_mm() + half_height});
    }
    return bounds;
}

[[nodiscard]] PcbSvgBounds synthetic_footprint_envelope(const FootprintDefinition &definition) {
    const auto pad_bounds = footprint_pad_bounds(definition);
    return PcbSvgBounds{pad_bounds.min_x - 0.5, pad_bounds.min_y - 0.5, pad_bounds.max_x + 0.5,
                        pad_bounds.max_y + 0.5};
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

void write_declared_footprint_geometry(std::ostream &out,
                                       const ProjectedFootprintGeometry &geometry) {
    if (geometry.courtyard().has_value()) {
        write_projected_footprint_polygon(out, "footprint-courtyard declared", geometry,
                                          geometry.courtyard().value());
    }
    if (geometry.body().has_value()) {
        write_projected_footprint_polygon(out, "footprint-body declared", geometry,
                                          geometry.body().value());
    }
}

void write_synthetic_footprint_geometry(std::ostream &out, const FootprintDefinition &definition) {
    const auto local_bounds = synthetic_footprint_envelope(definition);
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
    const auto local_bounds = footprint_local_bounds(definition);
    const auto anchor = volt::detail::transform_footprint_point(
        placement, FootprintPoint{0.0, local_bounds.min_y - 1.0});
    out << "      <text class=\"reference-designator\" data-placement=\""
        << encode_local_id(placement_id) << "\" data-component=\""
        << encode_local_id(placement.component()) << "\" x=\"";
    write_pcb_svg_number(out, anchor.x_mm());
    out << "\" y=\"";
    write_pcb_svg_number(out, anchor.y_mm());
    out << "\" text-anchor=\"middle\">" << pcb_svg_escape(component.reference().value())
        << "</text>\n";
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
    auto bounds = footprint_pad_bounds(definition);
    if (!footprint_has_declared_geometry(definition)) {
        return synthetic_footprint_envelope(definition);
    }

    if (definition.courtyard().has_value()) {
        for (const auto point : definition.courtyard()->vertices()) {
            include_footprint_point(bounds, point);
        }
    }
    if (definition.body().has_value()) {
        for (const auto point : definition.body()->vertices()) {
            include_footprint_point(bounds, point);
        }
    }
    return bounds;
}

void include_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                              const FootprintDefinition &definition,
                              const ProjectedFootprintGeometry *projected_geometry) {
    if (!footprint_has_declared_geometry(definition)) {
        include_local_footprint_bounds(bounds, placement, synthetic_footprint_envelope(definition));
        return;
    }

    include_local_footprint_bounds(bounds, placement, footprint_pad_bounds(definition));
    if (projected_geometry == nullptr) {
        include_local_footprint_bounds(bounds, placement, footprint_local_bounds(definition));
        return;
    }
    include_projected_polygon_bounds(bounds, projected_geometry->courtyard());
    include_projected_polygon_bounds(bounds, projected_geometry->body());
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
    throw std::logic_error{"Unhandled PCB footprint pad shape"};
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
                      PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-footprints\">\n";
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

        const auto &component = board.circuit().component(placement.component());
        const auto *projected_geometry =
            projected_footprint_geometry_for_placement(footprint_geometries, placement_id);
        if (placement_context_selected && footprint_has_declared_geometry(*definition) &&
            projected_geometry != nullptr) {
            write_declared_footprint_geometry(out, *projected_geometry);
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
        if (placement_context_selected && !footprint_has_declared_geometry(*definition)) {
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
        if (placement_context_selected) {
            write_reference_designator(out, placement_id, placement, *definition, component);
        }
    }
    out << "    </g>\n";
}

} // namespace volt::io::detail
