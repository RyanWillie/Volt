#include <volt/io/pcb_svg_writer.hpp>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <stdexcept>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb_schema.hpp>

namespace volt::io::detail {
namespace {

[[nodiscard]] BoardPoint feature_label_anchor(const BoardFeature &feature) {
    switch (feature.kind()) {
    case BoardFeatureKind::Hole:
        return BoardPoint{feature.hole().center().x_mm(),
                          feature.hole().center().y_mm() +
                              (feature.hole().drill_diameter_mm() / 2.0) + 2.0};
    case BoardFeatureKind::Slot:
        return BoardPoint{(feature.slot().start().x_mm() + feature.slot().end().x_mm()) / 2.0,
                          std::max(feature.slot().start().y_mm(), feature.slot().end().y_mm()) +
                              (feature.slot().width_mm() / 2.0) + 2.0};
    case BoardFeatureKind::Cutout:
        return feature.cutout().outline().front();
    case BoardFeatureKind::Circle:
        return BoardPoint{feature.circle().center().x_mm(),
                          feature.circle().center().y_mm() +
                              (feature.circle().diameter_mm() / 2.0) + 2.0};
    }
    throw std::logic_error{"Board feature kind has no SVG feature label anchor"};
}

} // namespace

void include_feature_bounds(PcbSvgBounds &bounds, const BoardFeature &feature) {
    switch (feature.kind()) {
    case BoardFeatureKind::Hole: {
        const auto radius = feature.hole().drill_diameter_mm() / 2.0;
        const auto center = feature.hole().center();
        include_board_point(bounds, BoardPoint{center.x_mm() - radius, center.y_mm()});
        include_board_point(bounds, BoardPoint{center.x_mm() + radius, center.y_mm()});
        include_board_point(bounds, BoardPoint{center.x_mm(), center.y_mm() - radius});
        include_board_point(bounds, BoardPoint{center.x_mm(), center.y_mm() + radius});
        break;
    }
    case BoardFeatureKind::Slot: {
        const auto radius = feature.slot().width_mm() / 2.0;
        for (const auto point : {feature.slot().start(), feature.slot().end()}) {
            include_board_point(bounds, BoardPoint{point.x_mm() - radius, point.y_mm()});
            include_board_point(bounds, BoardPoint{point.x_mm() + radius, point.y_mm()});
            include_board_point(bounds, BoardPoint{point.x_mm(), point.y_mm() - radius});
            include_board_point(bounds, BoardPoint{point.x_mm(), point.y_mm() + radius});
        }
        break;
    }
    case BoardFeatureKind::Cutout:
        for (const auto point : feature.cutout().outline()) {
            include_board_point(bounds, point);
        }
        break;
    case BoardFeatureKind::Circle: {
        const auto radius = feature.circle().diameter_mm() / 2.0;
        const auto center = feature.circle().center();
        include_board_point(bounds, BoardPoint{center.x_mm() - radius, center.y_mm()});
        include_board_point(bounds, BoardPoint{center.x_mm() + radius, center.y_mm()});
        include_board_point(bounds, BoardPoint{center.x_mm(), center.y_mm() - radius});
        include_board_point(bounds, BoardPoint{center.x_mm(), center.y_mm() + radius});
        break;
    }
    }
}

void write_pcb_svg_features(std::ostream &out, const Board &board) {
    out << "    <g class=\"layer layer-board-features\">\n";
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        const auto &feature = board.feature(id);
        switch (feature.kind()) {
        case BoardFeatureKind::Hole:
            out << "      <circle class=\"board-feature hole\" data-board-feature=\""
                << encode_local_id(id) << "\" cx=\"";
            write_pcb_svg_number(out, feature.hole().center().x_mm());
            out << "\" cy=\"";
            write_pcb_svg_number(out, feature.hole().center().y_mm());
            out << "\" r=\"";
            write_pcb_svg_number(out, feature.hole().drill_diameter_mm() / 2.0);
            out << "\"/>\n";
            break;
        case BoardFeatureKind::Slot:
            out << "      <line class=\"board-feature slot\" data-board-feature=\""
                << encode_local_id(id) << "\" x1=\"";
            write_pcb_svg_number(out, feature.slot().start().x_mm());
            out << "\" y1=\"";
            write_pcb_svg_number(out, feature.slot().start().y_mm());
            out << "\" x2=\"";
            write_pcb_svg_number(out, feature.slot().end().x_mm());
            out << "\" y2=\"";
            write_pcb_svg_number(out, feature.slot().end().y_mm());
            out << "\" stroke-width=\"";
            write_pcb_svg_number(out, feature.slot().width_mm());
            out << "\" stroke-linecap=\"round\"/>\n";
            break;
        case BoardFeatureKind::Cutout:
            out << "      <polygon class=\"board-feature cutout\" data-board-feature=\""
                << encode_local_id(id) << "\" points=\"";
            write_pcb_point_list(out, feature.cutout().outline());
            out << "\"/>\n";
            break;
        case BoardFeatureKind::Circle:
            out << "      <circle class=\"board-feature circle "
                << board_side_name(feature.circle().side()) << "\" data-board-feature=\""
                << encode_local_id(id) << "\" cx=\"";
            write_pcb_svg_number(out, feature.circle().center().x_mm());
            out << "\" cy=\"";
            write_pcb_svg_number(out, feature.circle().center().y_mm());
            out << "\" r=\"";
            write_pcb_svg_number(out, feature.circle().diameter_mm() / 2.0);
            out << "\"/>\n";
            break;
        }
        if (!feature.label().empty()) {
            const auto anchor = feature_label_anchor(feature);
            out << "      <text class=\"board-feature-label\" data-board-feature=\""
                << encode_local_id(id) << "\" x=\"";
            write_pcb_svg_number(out, anchor.x_mm());
            out << "\" y=\"";
            write_pcb_svg_number(out, anchor.y_mm());
            out << "\" text-anchor=\"middle\">" << pcb_svg_escape(feature.label()) << "</text>\n";
        }
    }
    out << "    </g>\n";
}

} // namespace volt::io::detail
