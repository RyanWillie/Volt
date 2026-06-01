#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

BoardName::BoardName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Board name must not be empty"};
    }
}
BoardPoint::BoardPoint(double x_mm, double y_mm) : x_mm_{x_mm}, y_mm_{y_mm} {
    if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
        throw std::invalid_argument{"Board point coordinates must be finite"};
    }
}
BoardSize::BoardSize(double width_mm, double height_mm)
    : width_mm_{width_mm}, height_mm_{height_mm} {
    if (!std::isfinite(width_mm_) || !std::isfinite(height_mm_)) {
        throw std::invalid_argument{"Board size dimensions must be finite"};
    }
    if (width_mm_ <= 0.0 || height_mm_ <= 0.0) {
        throw std::invalid_argument{"Board size dimensions must be positive"};
    }
}
BoardRotation::BoardRotation(double degrees) : degrees_{degrees} {
    if (!std::isfinite(degrees_)) {
        throw std::invalid_argument{"Board rotation must be finite"};
    }
}
Board::Board(const Circuit &circuit, BoardName name) : circuit_{&circuit}, name_{std::move(name)} {}
BoardLayer::BoardLayer(std::string name, BoardLayerRole role, BoardLayerSide side,
                       double thickness_mm, bool enabled)
    : name_{std::move(name)}, role_{role}, side_{side}, thickness_mm_{thickness_mm},
      enabled_{enabled} {
    if (name_.empty()) {
        throw std::invalid_argument{"Board layer name must not be empty"};
    }
    if (!std::isfinite(thickness_mm_)) {
        throw std::invalid_argument{"Board layer thickness must be finite"};
    }
    if (thickness_mm_ < 0.0) {
        throw std::invalid_argument{"Board layer thickness must not be negative"};
    }
}
LayerStack::LayerStack(std::vector<BoardLayerId> layers, double board_thickness_mm)
    : layers_{std::move(layers)}, board_thickness_mm_{board_thickness_mm} {
    if (layers_.empty()) {
        throw std::invalid_argument{"Layer stack must contain at least one layer"};
    }
    if (!std::isfinite(board_thickness_mm_)) {
        throw std::invalid_argument{"Board thickness must be finite"};
    }
    if (board_thickness_mm_ <= 0.0) {
        throw std::invalid_argument{"Board thickness must be positive"};
    }

    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Layer stack must not contain duplicate layers"};
    }
}
BoardOutline::BoardOutline(std::vector<BoardPoint> vertices) : vertices_{std::move(vertices)} {
    drop_duplicate_closing_vertex();
    if (vertices_.size() < 3) {
        throw std::invalid_argument{"Board outline must contain at least three vertices"};
    }
    if (std::abs(signed_area_twice(vertices_)) <= kGeometryEpsilon) {
        throw std::invalid_argument{"Board outline area must be positive"};
    }
    if (has_self_intersection(vertices_)) {
        throw std::invalid_argument{"Board outline must not self-intersect"};
    }
}
[[nodiscard]] BoardOutline BoardOutline::rectangle(BoardPoint origin, BoardSize size) {
    return BoardOutline{std::vector{
        origin,
        BoardPoint{origin.x_mm() + size.width_mm(), origin.y_mm()},
        BoardPoint{origin.x_mm() + size.width_mm(), origin.y_mm() + size.height_mm()},
        BoardPoint{origin.x_mm(), origin.y_mm() + size.height_mm()},
    }};
}
[[nodiscard]] bool BoardOutline::contains(BoardPoint point) const noexcept {
    bool inside = false;
    std::size_t previous = vertices_.size() - 1U;
    for (std::size_t current = 0; current < vertices_.size(); ++current) {
        const auto &a = vertices_[previous];
        const auto &b = vertices_[current];
        if (point_on_segment(point, a, b)) {
            return true;
        }

        const auto crosses_y = (a.y_mm() > point.y_mm()) != (b.y_mm() > point.y_mm());
        if (crosses_y) {
            const auto intersection_x =
                ((b.x_mm() - a.x_mm()) * (point.y_mm() - a.y_mm()) / (b.y_mm() - a.y_mm())) +
                a.x_mm();
            if (point.x_mm() < intersection_x) {
                inside = !inside;
            }
        }

        previous = current;
    }

    return inside;
}
void BoardOutline::drop_duplicate_closing_vertex() {
    if (vertices_.size() > 1 && vertices_.front() == vertices_.back()) {
        vertices_.pop_back();
    }
}
[[nodiscard]] double BoardOutline::signed_area_twice(const std::vector<BoardPoint> &vertices) {
    double area = 0.0;
    std::size_t previous = vertices.size() - 1U;
    for (std::size_t current = 0; current < vertices.size(); ++current) {
        area += vertices[previous].x_mm() * vertices[current].y_mm();
        area -= vertices[current].x_mm() * vertices[previous].y_mm();
        previous = current;
    }
    return area;
}
[[nodiscard]] bool BoardOutline::point_on_segment(BoardPoint point, BoardPoint a,
                                                  BoardPoint b) noexcept {
    const auto cross = ((point.y_mm() - a.y_mm()) * (b.x_mm() - a.x_mm())) -
                       ((point.x_mm() - a.x_mm()) * (b.y_mm() - a.y_mm()));
    if (std::abs(cross) > kGeometryEpsilon) {
        return false;
    }

    const auto dot = ((point.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                     ((point.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    if (dot < -kGeometryEpsilon) {
        return false;
    }

    const auto length_squared = ((b.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                                ((b.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    return dot <= length_squared + kGeometryEpsilon;
}
[[nodiscard]] double BoardOutline::orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept {
    return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
           ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
}
[[nodiscard]] bool BoardOutline::segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                                    BoardPoint d) noexcept {
    const auto ab_c = orientation(a, b, c);
    const auto ab_d = orientation(a, b, d);
    const auto cd_a = orientation(c, d, a);
    const auto cd_b = orientation(c, d, b);

    if (std::abs(ab_c) <= kGeometryEpsilon && point_on_segment(c, a, b)) {
        return true;
    }
    if (std::abs(ab_d) <= kGeometryEpsilon && point_on_segment(d, a, b)) {
        return true;
    }
    if (std::abs(cd_a) <= kGeometryEpsilon && point_on_segment(a, c, d)) {
        return true;
    }
    if (std::abs(cd_b) <= kGeometryEpsilon && point_on_segment(b, c, d)) {
        return true;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}
[[nodiscard]] bool BoardOutline::has_self_intersection(const std::vector<BoardPoint> &vertices) {
    for (std::size_t first = 0; first < vertices.size(); ++first) {
        const auto first_next = (first + 1U) % vertices.size();
        for (std::size_t second = first + 1U; second < vertices.size(); ++second) {
            const auto second_next = (second + 1U) % vertices.size();
            const auto adjacent = first == second || first_next == second || second_next == first ||
                                  (first == 0U && second_next == 0U);
            if (adjacent) {
                continue;
            }

            if (segments_intersect(vertices[first], vertices[first_next], vertices[second],
                                   vertices[second_next])) {
                return true;
            }
        }
    }

    return false;
}
BoardPolygon::BoardPolygon(std::vector<BoardPoint> vertices) : vertices_{std::move(vertices)} {
    drop_duplicate_closing_vertex();
    if (vertices_.size() < 3) {
        throw std::invalid_argument{"Board polygon must contain at least three vertices"};
    }
    if (std::abs(signed_area_twice(vertices_)) <= kGeometryEpsilon) {
        throw std::invalid_argument{"Board polygon area must be positive"};
    }
    if (has_self_intersection(vertices_)) {
        throw std::invalid_argument{"Board polygon must not self-intersect"};
    }
}
void BoardPolygon::drop_duplicate_closing_vertex() {
    if (vertices_.size() > 1 && vertices_.front() == vertices_.back()) {
        vertices_.pop_back();
    }
}
[[nodiscard]] double BoardPolygon::signed_area_twice(const std::vector<BoardPoint> &vertices) {
    double area = 0.0;
    std::size_t previous = vertices.size() - 1U;
    for (std::size_t current = 0; current < vertices.size(); ++current) {
        area += vertices[previous].x_mm() * vertices[current].y_mm();
        area -= vertices[current].x_mm() * vertices[previous].y_mm();
        previous = current;
    }
    return area;
}
[[nodiscard]] bool BoardPolygon::point_on_segment(BoardPoint point, BoardPoint a,
                                                  BoardPoint b) noexcept {
    const auto cross = ((point.y_mm() - a.y_mm()) * (b.x_mm() - a.x_mm())) -
                       ((point.x_mm() - a.x_mm()) * (b.y_mm() - a.y_mm()));
    if (std::abs(cross) > kGeometryEpsilon) {
        return false;
    }

    const auto dot = ((point.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                     ((point.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    if (dot < -kGeometryEpsilon) {
        return false;
    }

    const auto length_squared = ((b.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                                ((b.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    return dot <= length_squared + kGeometryEpsilon;
}
[[nodiscard]] double BoardPolygon::orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept {
    return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
           ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
}
[[nodiscard]] bool BoardPolygon::segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                                    BoardPoint d) noexcept {
    const auto ab_c = orientation(a, b, c);
    const auto ab_d = orientation(a, b, d);
    const auto cd_a = orientation(c, d, a);
    const auto cd_b = orientation(c, d, b);

    if (std::abs(ab_c) <= kGeometryEpsilon && point_on_segment(c, a, b)) {
        return true;
    }
    if (std::abs(ab_d) <= kGeometryEpsilon && point_on_segment(d, a, b)) {
        return true;
    }
    if (std::abs(cd_a) <= kGeometryEpsilon && point_on_segment(a, c, d)) {
        return true;
    }
    if (std::abs(cd_b) <= kGeometryEpsilon && point_on_segment(b, c, d)) {
        return true;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}
[[nodiscard]] bool BoardPolygon::has_self_intersection(const std::vector<BoardPoint> &vertices) {
    for (std::size_t first = 0; first < vertices.size(); ++first) {
        const auto first_next = (first + 1U) % vertices.size();
        for (std::size_t second = first + 1U; second < vertices.size(); ++second) {
            const auto second_next = (second + 1U) % vertices.size();
            const auto adjacent = first == second || first_next == second || second_next == first ||
                                  (first == 0U && second_next == 0U);
            if (adjacent) {
                continue;
            }

            if (segments_intersect(vertices[first], vertices[first_next], vertices[second],
                                   vertices[second_next])) {
                return true;
            }
        }
    }

    return false;
}
[[nodiscard]] BoardFeature BoardFeature::mounting_hole(std::string label, BoardPoint center,
                                                       double drill_diameter_mm) {
    return BoardFeature{BoardFeatureKind::MountingHole, std::move(label), center,
                        drill_diameter_mm};
}
BoardFeature::BoardFeature(BoardFeatureKind kind, std::string label, BoardPoint position,
                           double diameter_mm)
    : kind_{kind}, label_{std::move(label)}, position_{position}, diameter_mm_{diameter_mm} {
    if (!std::isfinite(diameter_mm_)) {
        throw std::invalid_argument{"Board feature diameter must be finite"};
    }
    if (diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board feature diameter must be positive"};
    }
}
ComponentPlacement::ComponentPlacement(ComponentId component, BoardPoint position,
                                       BoardRotation rotation, BoardSide side, bool locked)
    : component_{component}, position_{position}, rotation_{rotation}, side_{side},
      locked_{locked} {}
PadResolution::PadResolution(ComponentPlacementId placement, ComponentId component,
                             FootprintPadId pad, std::string pad_label, BoardPoint position,
                             std::optional<PinId> pin, std::optional<NetId> net,
                             PadResolutionStatus status)
    : placement_{placement}, component_{component}, pad_{pad}, pad_label_{std::move(pad_label)},
      position_{position}, pin_{pin}, net_{net}, status_{status} {}
RatsnestEndpoint::RatsnestEndpoint(ComponentPlacementId placement, ComponentId component,
                                   FootprintPadId pad, BoardPoint position)
    : placement_{placement}, component_{component}, pad_{pad}, position_{position} {}
RatsnestEdge::RatsnestEdge(NetId net, RatsnestEndpoint from, RatsnestEndpoint to)
    : net_{net}, from_{from}, to_{to} {}
BoardZone::BoardZone(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                     std::optional<NetId> net, BoardZoneFill fill, int priority)
    : outline_{std::move(outline)}, layers_{std::move(layers)}, net_{net}, fill_{fill},
      priority_{priority} {
    validate_layers();
}
[[nodiscard]] const std::vector<BoardPoint> &BoardZone::outline() const noexcept {
    return outline_.vertices();
}
void BoardZone::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board zone layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board zone layers must not contain duplicates"};
    }
}
BoardKeepout::BoardKeepout(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                           std::vector<BoardKeepoutRestriction> restrictions)
    : outline_{std::move(outline)}, layers_{std::move(layers)},
      restrictions_{std::move(restrictions)} {
    validate_layers();
    validate_restrictions();
}
[[nodiscard]] const std::vector<BoardPoint> &BoardKeepout::outline() const noexcept {
    return outline_.vertices();
}
[[nodiscard]] const std::vector<BoardKeepoutRestriction> &
BoardKeepout::restrictions() const noexcept {
    return restrictions_;
}
void BoardKeepout::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board keepout layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout layers must not contain duplicates"};
    }
}
void BoardKeepout::validate_restrictions() const {
    if (restrictions_.empty()) {
        throw std::invalid_argument{"Board keepout restrictions must not be empty"};
    }
    auto sorted = restrictions_;
    std::sort(sorted.begin(), sorted.end());
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout restrictions must not contain duplicates"};
    }
}
BoardText::BoardText(std::string text, BoardPoint position, BoardRotation rotation,
                     BoardLayerId layer, double size_mm, bool locked)
    : text_{std::move(text)}, position_{position}, rotation_{rotation}, layer_{layer},
      size_mm_{size_mm}, locked_{locked} {
    if (text_.empty()) {
        throw std::invalid_argument{"Board text must not be empty"};
    }
    if (!std::isfinite(size_mm_)) {
        throw std::invalid_argument{"Board text size must be finite"};
    }
    if (size_mm_ <= 0.0) {
        throw std::invalid_argument{"Board text size must be positive"};
    }
}
BoardTrack::BoardTrack(NetId net, BoardLayerId layer, std::vector<BoardPoint> points,
                       double width_mm)
    : net_{net}, layer_{layer}, points_{std::move(points)}, width_mm_{width_mm} {
    if (points_.size() < 2U) {
        throw std::invalid_argument{"Board track must contain at least two points"};
    }
    if (!std::isfinite(width_mm_)) {
        throw std::invalid_argument{"Board track width must be finite"};
    }
    if (width_mm_ <= 0.0) {
        throw std::invalid_argument{"Board track width must be positive"};
    }
    for (std::size_t index = 1; index < points_.size(); ++index) {
        if (points_[index - 1U] == points_[index]) {
            throw std::invalid_argument{"Board track points must not repeat adjacent vertices"};
        }
    }
}
BoardVia::BoardVia(NetId net, BoardPoint position, BoardLayerId start_layer, BoardLayerId end_layer,
                   double drill_diameter_mm, double annular_diameter_mm)
    : net_{net}, position_{position}, start_layer_{start_layer}, end_layer_{end_layer},
      drill_diameter_mm_{drill_diameter_mm}, annular_diameter_mm_{annular_diameter_mm} {
    if (start_layer_ == end_layer_) {
        throw std::invalid_argument{"Board via layer span must reference distinct layers"};
    }
    if (!std::isfinite(drill_diameter_mm_) || !std::isfinite(annular_diameter_mm_)) {
        throw std::invalid_argument{"Board via diameters must be finite"};
    }
    if (drill_diameter_mm_ <= 0.0 || annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board via diameters must be positive"};
    }
    if (annular_diameter_mm_ <= drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board via annular diameter must be greater than drill diameter"};
    }
}
BoardDesignRules::BoardDesignRules(double copper_clearance_mm, double minimum_track_width_mm,
                                   double minimum_via_drill_diameter_mm,
                                   double minimum_via_annular_diameter_mm,
                                   double board_outline_clearance_mm)
    : copper_clearance_mm_{copper_clearance_mm}, minimum_track_width_mm_{minimum_track_width_mm},
      minimum_via_drill_diameter_mm_{minimum_via_drill_diameter_mm},
      minimum_via_annular_diameter_mm_{minimum_via_annular_diameter_mm},
      board_outline_clearance_mm_{board_outline_clearance_mm} {
    if (!std::isfinite(copper_clearance_mm_) || !std::isfinite(board_outline_clearance_mm_)) {
        throw std::invalid_argument{"Board design rule clearances must be finite"};
    }
    if (copper_clearance_mm_ < 0.0 || board_outline_clearance_mm_ < 0.0) {
        throw std::invalid_argument{"Board design rule clearances must not be negative"};
    }
    if (!std::isfinite(minimum_track_width_mm_) || !std::isfinite(minimum_via_drill_diameter_mm_) ||
        !std::isfinite(minimum_via_annular_diameter_mm_)) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be finite"};
    }
    if (minimum_track_width_mm_ <= 0.0 || minimum_via_drill_diameter_mm_ <= 0.0 ||
        minimum_via_annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be positive"};
    }
    if (minimum_via_annular_diameter_mm_ <= minimum_via_drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board design rule via annular diameter must be greater than drill diameter"};
    }
}
[[nodiscard]] double BoardDesignRules::minimum_via_drill_diameter_mm() const noexcept {
    return minimum_via_drill_diameter_mm_;
}
[[nodiscard]] double BoardDesignRules::minimum_via_annular_diameter_mm() const noexcept {
    return minimum_via_annular_diameter_mm_;
}
[[nodiscard]] double BoardDesignRules::board_outline_clearance_mm() const noexcept {
    return board_outline_clearance_mm_;
}
[[nodiscard]] std::vector<RatsnestEdge>
derive_ratsnest_edges(const std::vector<PadResolution> &resolutions) {
    struct EndpointWithNet {
        NetId net;
        RatsnestEndpoint endpoint;
    };

    auto endpoints = std::vector<EndpointWithNet>{};
    endpoints.reserve(resolutions.size());
    for (const auto &resolution : resolutions) {
        if (resolution.status() != PadResolutionStatus::Connected ||
            !resolution.net().has_value()) {
            continue;
        }
        endpoints.push_back(EndpointWithNet{
            resolution.net().value(),
            RatsnestEndpoint{resolution.placement(), resolution.component(), resolution.pad(),
                             resolution.position()},
        });
    }

    std::sort(endpoints.begin(), endpoints.end(),
              [](const EndpointWithNet &lhs, const EndpointWithNet &rhs) {
                  if (lhs.net.index() != rhs.net.index()) {
                      return lhs.net.index() < rhs.net.index();
                  }
                  return detail::ratsnest_endpoint_less(lhs.endpoint, rhs.endpoint);
              });

    auto edges = std::vector<RatsnestEdge>{};
    std::size_t group_begin = 0;
    while (group_begin < endpoints.size()) {
        std::size_t group_end = group_begin + 1U;
        while (group_end < endpoints.size() &&
               endpoints[group_end].net == endpoints[group_begin].net) {
            ++group_end;
        }

        const auto group_size = group_end - group_begin;
        if (group_size >= 2U) {
            const auto edges_before_group = edges.size();
            struct CandidateEdge {
                std::size_t from;
                std::size_t to;
                double distance_squared;
            };

            auto candidates = std::vector<CandidateEdge>{};
            candidates.reserve((group_size * (group_size - 1U)) / 2U);
            for (std::size_t from = 0; from < group_size; ++from) {
                for (std::size_t to = from + 1U; to < group_size; ++to) {
                    candidates.push_back(CandidateEdge{
                        from,
                        to,
                        detail::ratsnest_distance_squared(endpoints[group_begin + from].endpoint,
                                                          endpoints[group_begin + to].endpoint),
                    });
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                      [&](const CandidateEdge &lhs, const CandidateEdge &rhs) {
                          if (lhs.distance_squared != rhs.distance_squared) {
                              return lhs.distance_squared < rhs.distance_squared;
                          }
                          const auto &lhs_from = endpoints[group_begin + lhs.from].endpoint;
                          const auto &rhs_from = endpoints[group_begin + rhs.from].endpoint;
                          if (!detail::same_ratsnest_endpoint(lhs_from, rhs_from)) {
                              return detail::ratsnest_endpoint_less(lhs_from, rhs_from);
                          }
                          return detail::ratsnest_endpoint_less(
                              endpoints[group_begin + lhs.to].endpoint,
                              endpoints[group_begin + rhs.to].endpoint);
                      });

            auto parents = std::vector<std::size_t>{};
            parents.reserve(group_size);
            for (std::size_t index = 0; index < group_size; ++index) {
                parents.push_back(index);
            }

            for (const auto candidate : candidates) {
                const auto from_root = detail::ratsnest_root(parents, candidate.from);
                const auto to_root = detail::ratsnest_root(parents, candidate.to);
                if (from_root == to_root) {
                    continue;
                }

                parents[to_root] = from_root;
                edges.push_back(detail::make_ratsnest_edge(
                    endpoints[group_begin].net, endpoints[group_begin + candidate.from].endpoint,
                    endpoints[group_begin + candidate.to].endpoint));
                if (edges.size() - edges_before_group >= group_size - 1U) {
                    break;
                }
            }
        }

        group_begin = group_end;
    }

    return edges;
}
[[nodiscard]] BoardLayerId Board::add_layer(BoardLayer layer) {
    if (layer_by_name(layer.name()).has_value()) {
        throw std::logic_error{"Board layer name already exists"};
    }

    return layers_.insert(std::move(layer));
}
void Board::set_layer_stack(LayerStack stack) {
    for (const auto layer : stack.layers()) {
        require_layer(layer);
    }
    layer_stack_ = std::move(stack);
}
void Board::set_outline(BoardOutline outline) { outline_ = std::move(outline); }
void Board::set_design_rules(BoardDesignRules rules) { design_rules_ = std::move(rules); }
[[nodiscard]] BoardFeatureId Board::add_feature(BoardFeature feature) {
    return features_.insert(std::move(feature));
}
[[nodiscard]] FootprintDefId Board::cache_footprint_definition(FootprintDefinition footprint) {
    if (footprint_definition_id(footprint.ref()).has_value()) {
        throw std::logic_error{"Board footprint definition already exists"};
    }

    return footprint_definitions_.insert(std::move(footprint));
}
[[nodiscard]] ComponentPlacementId Board::place_component(ComponentPlacement placement) {
    static_cast<void>(circuit().component(placement.component()));
    if (placement_for_component(placement.component()).has_value()) {
        throw std::logic_error{"Component already has a board placement"};
    }

    return placements_.insert(std::move(placement));
}
[[nodiscard]] BoardTrackId Board::add_track(BoardTrack track) {
    require_net(track.net());
    require_copper_layer(track.layer());
    return tracks_.insert(std::move(track));
}
[[nodiscard]] BoardViaId Board::add_via(BoardVia via) {
    require_net(via.net());
    require_copper_layer(via.start_layer());
    require_copper_layer(via.end_layer());
    return vias_.insert(std::move(via));
}
[[nodiscard]] BoardZoneId Board::add_zone(BoardZone zone) {
    if (zone.net().has_value()) {
        require_net(zone.net().value());
    }
    for (const auto layer_id : zone.layers()) {
        require_layer(layer_id);
        if (layer(layer_id).role() != BoardLayerRole::Copper) {
            throw std::logic_error{"Board copper zones require copper layers"};
        }
    }
    return zones_.insert(std::move(zone));
}
[[nodiscard]] BoardKeepoutId Board::add_keepout(BoardKeepout keepout) {
    for (const auto layer : keepout.layers()) {
        require_layer(layer);
    }
    return keepouts_.insert(std::move(keepout));
}
[[nodiscard]] BoardTextId Board::add_text(BoardText text) {
    require_layer(text.layer());
    return texts_.insert(std::move(text));
}
[[nodiscard]] const std::optional<LayerStack> &Board::layer_stack() const noexcept {
    return layer_stack_;
}
[[nodiscard]] const FootprintDefinition &Board::footprint_definition(FootprintDefId id) const {
    return footprint_definitions_.get(id);
}
[[nodiscard]] std::size_t Board::footprint_definition_count() const noexcept {
    return footprint_definitions_.size();
}
[[nodiscard]] std::optional<FootprintDefId>
Board::footprint_definition_id(const FootprintRef &ref) const noexcept {
    for (std::size_t index = 0; index < footprint_definitions_.size(); ++index) {
        const auto id = FootprintDefId{index};
        if (footprint_definitions_.get(id).ref() == ref) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] const ComponentPlacement &Board::placement(ComponentPlacementId id) const {
    return placements_.get(id);
}
[[nodiscard]] std::optional<ComponentPlacementId>
Board::placement_for_component(ComponentId component) const noexcept {
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (placements_.get(id).component() == component) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::vector<PadResolution>
Board::resolve_pads(const FootprintLibrary &footprints) const {
    auto resolutions = std::vector<PadResolution>{};
    const auto resolution_footprints = detail::board_resolution_footprints(*this, footprints);
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &component_placement = placement(placement_id);
        const auto &selected_part =
            circuit().selected_physical_part(component_placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        append_pad_resolutions(placement_id, component_placement, *definition,
                               footprint_resolution.pad_bindings(), resolutions);
    }

    return resolutions;
}
[[nodiscard]] std::vector<RatsnestEdge>
Board::ratsnest_edges(const FootprintLibrary &footprints) const {
    return derive_ratsnest_edges(resolve_pads(footprints));
}
void Board::require_layer(BoardLayerId layer) const {
    if (!layers_.contains(layer)) {
        throw std::out_of_range{"Board layer ID does not belong to this board"};
    }
}
void Board::require_net(NetId net) const { static_cast<void>(circuit().net(net)); }
void Board::require_copper_layer(BoardLayerId layer_id) const {
    require_layer(layer_id);
    if (layer(layer_id).role() != BoardLayerRole::Copper) {
        throw std::logic_error{"Board copper primitives require copper layers"};
    }
}
[[nodiscard]] std::optional<BoardLayerId> Board::layer_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < layers_.size(); ++index) {
        const auto id = BoardLayerId{index};
        if (layers_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}
void Board::append_pad_resolutions(ComponentPlacementId placement_id,
                                   const ComponentPlacement &component_placement,
                                   const FootprintDefinition &definition,
                                   const std::vector<FootprintPadBinding> &bindings,
                                   std::vector<PadResolution> &resolutions) const {
    for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
        const auto pad_id = FootprintPadId{pad_index};
        const auto &pad = definition.pad(pad_id);
        const auto position =
            detail::transform_footprint_point(component_placement, pad.position());
        if (!pad.requires_pin_mapping()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::NonElectrical);
            continue;
        }

        const auto binding = std::find_if(
            bindings.begin(), bindings.end(),
            [pad_id](const FootprintPadBinding &candidate) { return candidate.pad() == pad_id; });
        if (binding == bindings.end()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto pin =
            circuit().pin_by_definition(component_placement.component(), binding->pin());
        if (!pin.has_value()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto net = circuit().net_of(pin.value());
        const auto status =
            net.has_value() ? PadResolutionStatus::Connected : PadResolutionStatus::Unconnected;
        resolutions.emplace_back(placement_id, component_placement.component(), pad_id, pad.label(),
                                 position, pin, net, status);
    }
}
[[nodiscard]] DiagnosticReport validate_board(const Board &board,
                                              const FootprintLibrary &footprints) {
    auto report = DiagnosticReport{};
    const auto resolution_footprints = detail::board_resolution_footprints(board, footprints);
    const auto pad_resolutions = board.resolve_pads(resolution_footprints);

    if (!board.outline().has_value()) {
        report.add(detail::board_diagnostic(DiagnosticCode{"PCB_BOARD_OUTLINE_MISSING"},
                                            "Board has no outline"));
    }

    for (std::size_t index = 0; index < board.circuit().component_count(); ++index) {
        const auto component = ComponentId{index};
        if (!board.placement_for_component(component).has_value()) {
            report.add(
                detail::board_component_diagnostic(DiagnosticCode{"PCB_COMPONENT_NOT_PLACED"},
                                                   "Component has no board placement", component));
        }

        if (!board.circuit().selected_physical_part(component).has_value()) {
            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_COMPONENT_MISSING_SELECTED_PART"},
                "Component requires a selected physical part for board placement", component));
        }
    }

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        for (const auto &diagnostic : footprint_resolution.diagnostics().diagnostics()) {
            report.add(Diagnostic{diagnostic.severity(), diagnostic.code(), diagnostic.message(),
                                  std::vector{EntityRef::component(placement.component()),
                                              EntityRef::component_placement(placement_id)}});
        }

        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (const auto &binding : footprint_resolution.pad_bindings()) {
            if (board.circuit()
                    .pin_by_definition(placement.component(), binding.pin())
                    .has_value()) {
                continue;
            }

            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_PIN_PAD_MISMATCH"},
                "Selected part pin-pad mapping does not resolve to a concrete component pin",
                placement.component()));
        }

        if (!board.outline().has_value()) {
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto &pad = definition->pad(FootprintPadId{pad_index});
            const auto pad_corners = detail::transformed_pad_body_corners(placement, pad);
            if (detail::pad_body_exits_outline(board.outline().value(), pad_corners)) {
                report.add(detail::board_placement_diagnostic(
                    DiagnosticCode{"PCB_PLACEMENT_OUTSIDE_OUTLINE"},
                    "Placement pad '" + pad.label() + "' is outside the board outline",
                    placement.component(), placement_id));
            }
        }
    }

    for (std::size_t index = 0; index < board.circuit().net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = board.circuit().net(net_id);
        if (net.pins().size() < 2U) {
            continue;
        }

        const auto placed_pad_count =
            std::count_if(pad_resolutions.begin(), pad_resolutions.end(),
                          [net_id](const PadResolution &resolution) {
                              return resolution.status() == PadResolutionStatus::Connected &&
                                     resolution.net().has_value() &&
                                     resolution.net().value() == net_id;
                          });
        if (placed_pad_count != 0) {
            continue;
        }

        report.add(
            detail::board_warning(DiagnosticCode{"PCB_NET_WITHOUT_PLACED_PADS"},
                                  "Net has logical connectivity but no placed pads on the board",
                                  std::vector{EntityRef::net(net_id)}));
    }

    detail::validate_board_drc(board, resolution_footprints, pad_resolutions, report);

    return report;
}

} // namespace volt

namespace volt::detail {

[[nodiscard]] bool ratsnest_endpoint_less(const RatsnestEndpoint &lhs,
                                          const RatsnestEndpoint &rhs) noexcept {
    if (lhs.placement().index() != rhs.placement().index()) {
        return lhs.placement().index() < rhs.placement().index();
    }
    return lhs.pad().index() < rhs.pad().index();
}
[[nodiscard]] double ratsnest_distance_squared(const RatsnestEndpoint &lhs,
                                               const RatsnestEndpoint &rhs) noexcept {
    const auto dx = lhs.position().x_mm() - rhs.position().x_mm();
    const auto dy = lhs.position().y_mm() - rhs.position().y_mm();
    return (dx * dx) + (dy * dy);
}
[[nodiscard]] RatsnestEdge make_ratsnest_edge(NetId net, RatsnestEndpoint lhs,
                                              RatsnestEndpoint rhs) {
    if (ratsnest_endpoint_less(rhs, lhs)) {
        return RatsnestEdge{net, rhs, lhs};
    }
    return RatsnestEdge{net, lhs, rhs};
}
[[nodiscard]] std::size_t ratsnest_root(std::vector<std::size_t> &parents, std::size_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}
[[nodiscard]] bool same_ratsnest_endpoint(const RatsnestEndpoint &lhs,
                                          const RatsnestEndpoint &rhs) noexcept {
    return lhs.placement() == rhs.placement() && lhs.pad() == rhs.pad();
}
[[nodiscard]] BoardPoint transform_footprint_point(const ComponentPlacement &placement,
                                                   FootprintPoint point) {
    constexpr double pi = 3.14159265358979323846264338327950288;
    const auto radians = placement.rotation().degrees() * pi / 180.0;
    auto local_x = point.x_mm();
    const auto local_y = point.y_mm();
    if (placement.side() == BoardSide::Bottom) {
        local_x = -local_x;
    }

    const auto rotated_x = (std::cos(radians) * local_x) - (std::sin(radians) * local_y);
    const auto rotated_y = (std::sin(radians) * local_x) + (std::cos(radians) * local_y);
    return BoardPoint{placement.position().x_mm() + rotated_x,
                      placement.position().y_mm() + rotated_y};
}
[[nodiscard]] std::vector<BoardPoint>
transformed_pad_body_corners(const ComponentPlacement &placement, const FootprintPad &pad) {
    const auto half_width = pad.size().width_mm() / 2.0;
    const auto half_height = pad.size().height_mm() / 2.0;
    const auto center = pad.position();
    return std::vector{
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() - half_width, center.y_mm() - half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() + half_width, center.y_mm() - half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() + half_width, center.y_mm() + half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() - half_width, center.y_mm() + half_height}),
    };
}
[[nodiscard]] double board_orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept {
    return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
           ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
}
[[nodiscard]] bool segments_cross_properly(BoardPoint a, BoardPoint b, BoardPoint c,
                                           BoardPoint d) noexcept {
    constexpr double geometry_epsilon = 1.0e-9;
    const auto ab_c = board_orientation(a, b, c);
    const auto ab_d = board_orientation(a, b, d);
    const auto cd_a = board_orientation(c, d, a);
    const auto cd_b = board_orientation(c, d, b);

    if (std::abs(ab_c) <= geometry_epsilon || std::abs(ab_d) <= geometry_epsilon ||
        std::abs(cd_a) <= geometry_epsilon || std::abs(cd_b) <= geometry_epsilon) {
        return false;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}
[[nodiscard]] BoardPoint segment_midpoint(BoardPoint a, BoardPoint b) {
    return BoardPoint{(a.x_mm() + b.x_mm()) / 2.0, (a.y_mm() + b.y_mm()) / 2.0};
}
[[nodiscard]] bool pad_body_exits_outline(const BoardOutline &outline,
                                          const std::vector<BoardPoint> &pad_corners) {
    for (const auto point : pad_corners) {
        if (!outline.contains(point)) {
            return true;
        }
    }

    const auto &outline_vertices = outline.vertices();
    for (std::size_t pad_index = 0; pad_index < pad_corners.size(); ++pad_index) {
        const auto pad_next = (pad_index + 1U) % pad_corners.size();
        if (!outline.contains(segment_midpoint(pad_corners[pad_index], pad_corners[pad_next]))) {
            return true;
        }

        for (std::size_t outline_index = 0; outline_index < outline_vertices.size();
             ++outline_index) {
            const auto outline_next = (outline_index + 1U) % outline_vertices.size();
            if (segments_cross_properly(pad_corners[pad_index], pad_corners[pad_next],
                                        outline_vertices[outline_index],
                                        outline_vertices[outline_next])) {
                return true;
            }
        }
    }

    return false;
}
[[nodiscard]] FootprintLibrary board_resolution_footprints(const Board &board,
                                                           const FootprintLibrary &footprints) {
    auto library = FootprintLibrary{};
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        library.add(board.footprint_definition(FootprintDefId{index}));
    }
    for (const auto &definition : footprints.definitions()) {
        if (library.find(definition.ref()) == nullptr) {
            library.add(definition);
        }
    }
    return library;
}
[[nodiscard]] Diagnostic board_diagnostic(DiagnosticCode code, std::string message,
                                          std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Error, std::move(code), std::move(message), std::move(entities)};
}
[[nodiscard]] Diagnostic board_warning(DiagnosticCode code, std::string message,
                                       std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Warning, std::move(code), std::move(message), std::move(entities)};
}
[[nodiscard]] Diagnostic board_component_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component) {
    return board_diagnostic(std::move(code), std::move(message),
                            std::vector{EntityRef::component(component)});
}
[[nodiscard]] Diagnostic board_placement_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component,
                                                    ComponentPlacementId placement) {
    return board_diagnostic(
        std::move(code), std::move(message),
        std::vector{EntityRef::component(component), EntityRef::component_placement(placement)});
}
[[nodiscard]] double board_distance(BoardPoint lhs, BoardPoint rhs) noexcept {
    return std::sqrt(square(lhs.x_mm() - rhs.x_mm()) + square(lhs.y_mm() - rhs.y_mm()));
}
[[nodiscard]] double point_segment_distance(BoardPoint point, BoardPoint a, BoardPoint b) noexcept {
    const auto dx = b.x_mm() - a.x_mm();
    const auto dy = b.y_mm() - a.y_mm();
    const auto length_squared = (dx * dx) + (dy * dy);
    if (length_squared <= board_drc_epsilon) {
        return board_distance(point, a);
    }

    const auto projection =
        (((point.x_mm() - a.x_mm()) * dx) + ((point.y_mm() - a.y_mm()) * dy)) / length_squared;
    const auto clamped = std::clamp(projection, 0.0, 1.0);
    return board_distance(point, BoardPoint{a.x_mm() + (clamped * dx), a.y_mm() + (clamped * dy)});
}
[[nodiscard]] bool drc_point_on_segment(BoardPoint point, BoardPoint a, BoardPoint b) noexcept {
    return std::abs(board_orientation(a, b, point)) <= board_drc_epsilon &&
           point_segment_distance(point, a, b) <= board_drc_epsilon;
}
[[nodiscard]] bool drc_segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                          BoardPoint d) noexcept {
    const auto ab_c = board_orientation(a, b, c);
    const auto ab_d = board_orientation(a, b, d);
    const auto cd_a = board_orientation(c, d, a);
    const auto cd_b = board_orientation(c, d, b);

    if (std::abs(ab_c) <= board_drc_epsilon && drc_point_on_segment(c, a, b)) {
        return true;
    }
    if (std::abs(ab_d) <= board_drc_epsilon && drc_point_on_segment(d, a, b)) {
        return true;
    }
    if (std::abs(cd_a) <= board_drc_epsilon && drc_point_on_segment(a, c, d)) {
        return true;
    }
    if (std::abs(cd_b) <= board_drc_epsilon && drc_point_on_segment(b, c, d)) {
        return true;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}
[[nodiscard]] double segment_segment_distance(BoardPoint a, BoardPoint b, BoardPoint c,
                                              BoardPoint d) noexcept {
    if (drc_segments_intersect(a, b, c, d)) {
        return 0.0;
    }
    auto result = point_segment_distance(a, c, d);
    result = std::min(result, point_segment_distance(b, c, d));
    result = std::min(result, point_segment_distance(c, a, b));
    result = std::min(result, point_segment_distance(d, a, b));
    return result;
}
[[nodiscard]] bool polygon_contains_point(const std::vector<BoardPoint> &polygon,
                                          BoardPoint point) {
    bool inside = false;
    std::size_t previous = polygon.size() - 1U;
    for (std::size_t current = 0; current < polygon.size(); ++current) {
        const auto &a = polygon[previous];
        const auto &b = polygon[current];
        if (drc_point_on_segment(point, a, b)) {
            return true;
        }

        const auto crosses_y = (a.y_mm() > point.y_mm()) != (b.y_mm() > point.y_mm());
        if (crosses_y) {
            const auto intersection_x =
                ((b.x_mm() - a.x_mm()) * (point.y_mm() - a.y_mm()) / (b.y_mm() - a.y_mm())) +
                a.x_mm();
            if (point.x_mm() < intersection_x) {
                inside = !inside;
            }
        }
        previous = current;
    }
    return inside;
}
[[nodiscard]] double point_polygon_distance(BoardPoint point,
                                            const std::vector<BoardPoint> &polygon) {
    if (polygon_contains_point(polygon, point)) {
        return 0.0;
    }

    auto result = std::numeric_limits<double>::infinity();
    std::size_t previous = polygon.size() - 1U;
    for (std::size_t current = 0; current < polygon.size(); ++current) {
        result =
            std::min(result, point_segment_distance(point, polygon[previous], polygon[current]));
        previous = current;
    }
    return result;
}
[[nodiscard]] double segment_polygon_distance(BoardPoint a, BoardPoint b,
                                              const std::vector<BoardPoint> &polygon) {
    if (polygon_contains_point(polygon, a) || polygon_contains_point(polygon, b)) {
        return 0.0;
    }

    auto result = std::numeric_limits<double>::infinity();
    std::size_t previous = polygon.size() - 1U;
    for (std::size_t current = 0; current < polygon.size(); ++current) {
        result =
            std::min(result, segment_segment_distance(a, b, polygon[previous], polygon[current]));
        previous = current;
    }
    return result;
}
[[nodiscard]] double polygon_polygon_distance(const std::vector<BoardPoint> &lhs,
                                              const std::vector<BoardPoint> &rhs) {
    for (const auto point : lhs) {
        if (polygon_contains_point(rhs, point)) {
            return 0.0;
        }
    }
    for (const auto point : rhs) {
        if (polygon_contains_point(lhs, point)) {
            return 0.0;
        }
    }

    auto result = std::numeric_limits<double>::infinity();
    for (std::size_t lhs_index = 0; lhs_index < lhs.size(); ++lhs_index) {
        const auto lhs_next = (lhs_index + 1U) % lhs.size();
        for (std::size_t rhs_index = 0; rhs_index < rhs.size(); ++rhs_index) {
            const auto rhs_next = (rhs_index + 1U) % rhs.size();
            result = std::min(result, segment_segment_distance(lhs[lhs_index], lhs[lhs_next],
                                                               rhs[rhs_index], rhs[rhs_next]));
        }
    }
    return result;
}
[[nodiscard]] double outline_boundary_distance(const BoardOutline &outline, BoardPoint point) {
    const auto &vertices = outline.vertices();
    auto result = std::numeric_limits<double>::infinity();
    std::size_t previous = vertices.size() - 1U;
    for (std::size_t current = 0; current < vertices.size(); ++current) {
        result =
            std::min(result, point_segment_distance(point, vertices[previous], vertices[current]));
        previous = current;
    }
    return result;
}
[[nodiscard]] double segment_outline_boundary_distance(const BoardOutline &outline, BoardPoint a,
                                                       BoardPoint b) {
    const auto &vertices = outline.vertices();
    auto result = std::numeric_limits<double>::infinity();
    std::size_t previous = vertices.size() - 1U;
    for (std::size_t current = 0; current < vertices.size(); ++current) {
        result =
            std::min(result, segment_segment_distance(a, b, vertices[previous], vertices[current]));
        previous = current;
    }
    return result;
}
[[nodiscard]] double polygon_outline_boundary_distance(const BoardOutline &outline,
                                                       const std::vector<BoardPoint> &polygon) {
    const auto &vertices = outline.vertices();
    auto result = std::numeric_limits<double>::infinity();
    for (std::size_t polygon_index = 0; polygon_index < polygon.size(); ++polygon_index) {
        const auto polygon_next = (polygon_index + 1U) % polygon.size();
        std::size_t previous = vertices.size() - 1U;
        for (std::size_t current = 0; current < vertices.size(); ++current) {
            result = std::min(
                result, segment_segment_distance(polygon[polygon_index], polygon[polygon_next],
                                                 vertices[previous], vertices[current]));
            previous = current;
        }
    }
    return result;
}
[[nodiscard]] double shape_distance(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Disc) {
        return board_distance(lhs.points[0], rhs.points[0]);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_segment_distance(lhs.points[0], lhs.points[1], rhs.points[0], rhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Polygon) {
        return polygon_polygon_distance(lhs.points, rhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_segment_distance(rhs.points[0], lhs.points[0], lhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Segment) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(rhs.points[0], lhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Polygon) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(rhs.points[0], rhs.points[1], lhs.points);
    }
    return shape_distance(rhs, lhs);
}
[[nodiscard]] std::optional<BoardLayerId> first_common_layer(const BoardCopperShape &lhs,
                                                             const BoardCopperShape &rhs) {
    for (const auto lhs_layer : lhs.layers) {
        if (std::find(rhs.layers.begin(), rhs.layers.end(), lhs_layer) != rhs.layers.end()) {
            return lhs_layer;
        }
    }
    return std::nullopt;
}
[[nodiscard]] bool layers_overlap(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    return first_common_layer(lhs, rhs).has_value();
}
void append_unique_layer(std::vector<BoardLayerId> &layers, BoardLayerId layer) {
    if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
    }
}
[[nodiscard]] std::vector<BoardLayerId> via_copper_layers(const Board &board, const BoardVia &via) {
    auto result = std::vector<BoardLayerId>{};
    if (board.layer_stack().has_value()) {
        auto start = std::optional<std::size_t>{};
        auto end = std::optional<std::size_t>{};
        const auto &layers = board.layer_stack()->layers();
        for (std::size_t index = 0; index < layers.size(); ++index) {
            if (layers[index] == via.start_layer()) {
                start = index;
            }
            if (layers[index] == via.end_layer()) {
                end = index;
            }
        }
        if (start.has_value() && end.has_value()) {
            const auto first = std::min(start.value(), end.value());
            const auto last = std::max(start.value(), end.value());
            for (std::size_t index = first; index <= last; ++index) {
                if (board.layer(layers[index]).role() == BoardLayerRole::Copper) {
                    append_unique_layer(result, layers[index]);
                }
            }
            return result;
        }
    }

    append_unique_layer(result, via.start_layer());
    append_unique_layer(result, via.end_layer());
    return result;
}
[[nodiscard]] std::vector<BoardLayerId>
pad_copper_layers(const Board &board, const FootprintPad &pad, BoardSide placement_side) {
    auto result = std::vector<BoardLayerId>{};
    if (pad.layers().is_through_hole()) {
        for (std::size_t index = 0; index < board.layer_count(); ++index) {
            const auto layer_id = BoardLayerId{index};
            if (board.layer(layer_id).role() == BoardLayerRole::Copper) {
                result.push_back(layer_id);
            }
        }
        return result;
    }

    const auto front_copper = pad.layers().contains(FootprintLayer::FrontCopper);
    const auto back_copper = pad.layers().contains(FootprintLayer::BackCopper);
    const auto maps_to_top = placement_side == BoardSide::Top ? front_copper : back_copper;
    const auto maps_to_bottom = placement_side == BoardSide::Top ? back_copper : front_copper;
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (layer.role() != BoardLayerRole::Copper) {
            continue;
        }
        if ((layer.side() == BoardLayerSide::Top && maps_to_top) ||
            (layer.side() == BoardLayerSide::Bottom && maps_to_bottom)) {
            result.push_back(layer_id);
        }
    }
    return result;
}
[[nodiscard]] const PadResolution *
find_board_pad_resolution(const std::vector<PadResolution> &resolutions,
                          ComponentPlacementId placement, FootprintPadId pad) {
    const auto match = std::find_if(
        resolutions.begin(), resolutions.end(), [placement, pad](const PadResolution &candidate) {
            return candidate.placement() == placement && candidate.pad() == pad;
        });
    if (match == resolutions.end()) {
        return nullptr;
    }
    return &*match;
}
void append_track_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t track_index = 0; track_index < board.track_count(); ++track_index) {
        const auto track_id = BoardTrackId{track_index};
        const auto &track = board.track(track_id);
        for (std::size_t point_index = 1; point_index < track.points().size(); ++point_index) {
            shapes.push_back(BoardCopperShape{
                BoardCopperShapeKind::Segment,
                track.net(),
                std::vector{track.layer()},
                std::vector{EntityRef::board_track(track_id)},
                std::vector{track.points()[point_index - 1U], track.points()[point_index]},
                track.width_mm() / 2.0,
                std::nullopt,
            });
        }
    }
}
void append_via_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t via_index = 0; via_index < board.via_count(); ++via_index) {
        const auto via_id = BoardViaId{via_index};
        const auto &via = board.via(via_id);
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Disc,
            via.net(),
            via_copper_layers(board, via),
            std::vector{EntityRef::board_via(via_id)},
            std::vector{via.position()},
            via.annular_diameter_mm() / 2.0,
            std::nullopt,
        });
    }
}
void append_zone_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.zone(zone_id);
        if (!zone.net().has_value()) {
            continue;
        }
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Polygon,
            zone.net().value(),
            zone.layers(),
            std::vector{EntityRef::board_zone(zone_id)},
            zone.outline(),
            0.0,
            std::nullopt,
        });
    }
}
void append_pad_shapes(const Board &board, const FootprintLibrary &footprints,
                       const std::vector<PadResolution> &resolutions,
                       std::vector<BoardCopperShape> &shapes) {
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            continue;
        }
        const auto footprint_resolution = resolve_footprint(selected_part.value(), footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto *resolution = find_board_pad_resolution(resolutions, placement_id, pad_id);
            if (resolution == nullptr || resolution->status() != PadResolutionStatus::Connected ||
                !resolution->net().has_value()) {
                continue;
            }

            const auto &pad = definition->pad(pad_id);
            auto layers = pad_copper_layers(board, pad, placement.side());
            if (layers.empty()) {
                continue;
            }

            auto shape_points = std::vector<BoardPoint>{};
            auto radius = 0.0;
            auto kind = BoardCopperShapeKind::Polygon;
            if (pad.shape() == FootprintPadShape::Circle ||
                pad.shape() == FootprintPadShape::Oval) {
                kind = BoardCopperShapeKind::Disc;
                shape_points.push_back(resolution->position());
                radius = std::max(pad.size().width_mm(), pad.size().height_mm()) / 2.0;
            } else {
                shape_points = transformed_pad_body_corners(placement, pad);
            }

            shapes.push_back(BoardCopperShape{
                kind,
                resolution->net().value(),
                std::move(layers),
                std::vector{EntityRef::component_placement(placement_id),
                            EntityRef::footprint_pad(pad_id)},
                std::move(shape_points),
                radius,
                BoardPadShapeKey{placement_id, pad_id},
            });
        }
    }
}
[[nodiscard]] std::vector<BoardCopperShape>
collect_copper_shapes(const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions) {
    auto shapes = std::vector<BoardCopperShape>{};
    append_track_shapes(board, shapes);
    append_via_shapes(board, shapes);
    append_zone_shapes(board, shapes);
    append_pad_shapes(board, footprints, resolutions, shapes);
    return shapes;
}
[[nodiscard]] bool polygon_satisfies_outline(const std::vector<BoardPoint> &polygon,
                                             const BoardOutline &outline, double clearance_mm) {
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto next = (index + 1U) % polygon.size();
        if (!outline.contains(polygon[index]) ||
            !outline.contains(segment_midpoint(polygon[index], polygon[next]))) {
            return false;
        }
    }
    return polygon_outline_boundary_distance(outline, polygon) + board_drc_epsilon >= clearance_mm;
}
[[nodiscard]] bool shape_satisfies_outline(const BoardCopperShape &shape,
                                           const BoardOutline &outline, double clearance_mm) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return outline.contains(shape.points[0]) &&
               outline_boundary_distance(outline, shape.points[0]) + board_drc_epsilon >=
                   shape.radius_mm + clearance_mm;
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return outline.contains(shape.points[0]) && outline.contains(shape.points[1]) &&
               outline.contains(segment_midpoint(shape.points[0], shape.points[1])) &&
               segment_outline_boundary_distance(outline, shape.points[0], shape.points[1]) +
                       board_drc_epsilon >=
                   shape.radius_mm + clearance_mm;
    }

    return polygon_satisfies_outline(shape.points, outline, clearance_mm);
}
[[nodiscard]] std::vector<EntityRef> copper_shape_entities(const BoardCopperShape &shape, NetId net,
                                                           BoardLayerId layer) {
    auto entities = shape.primary_entities;
    entities.push_back(EntityRef::net(net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}
void validate_track_widths(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.track(track_id);
        if (track.width_mm() + board_drc_epsilon >= rules.minimum_track_width_mm()) {
            continue;
        }
        report.add(board_diagnostic(DiagnosticCode{"PCB_TRACK_WIDTH_BELOW_MINIMUM"},
                                    "Track width is below the board minimum",
                                    std::vector{EntityRef::board_track(track_id),
                                                EntityRef::net(track.net()),
                                                EntityRef::board_layer(track.layer())}));
    }
}
void validate_via_rules(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto via_id = BoardViaId{index};
        const auto &via = board.via(via_id);
        if (via.drill_diameter_mm() + board_drc_epsilon < rules.minimum_via_drill_diameter_mm()) {
            report.add(board_diagnostic(
                DiagnosticCode{"PCB_VIA_DRILL_BELOW_MINIMUM"},
                "Via drill diameter is below the board minimum",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
        }
        if (via.annular_diameter_mm() + board_drc_epsilon <
            rules.minimum_via_annular_diameter_mm()) {
            report.add(board_diagnostic(
                DiagnosticCode{"PCB_VIA_ANNULAR_BELOW_MINIMUM"},
                "Via annular copper diameter is below the board minimum",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
        }
    }
}
void validate_outline_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                DiagnosticReport &report) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &outline = board.outline().value();
    const auto outline_clearance = board.design_rules().board_outline_clearance_mm();
    for (const auto &shape : shapes) {
        if (shape_satisfies_outline(shape, outline, outline_clearance)) {
            continue;
        }
        auto layer = shape.layers.empty() ? std::optional<BoardLayerId>{} : shape.layers.front();
        if (!layer.has_value()) {
            continue;
        }
        report.add(board_diagnostic(DiagnosticCode{"PCB_COPPER_OUTSIDE_OUTLINE"},
                                    "Copper does not satisfy the board outline clearance",
                                    copper_shape_entities(shape, shape.net, layer.value())));
    }
}
void validate_netless_zone_outline_clearance(const Board &board, DiagnosticReport &report) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &outline = board.outline().value();
    const auto outline_clearance = board.design_rules().board_outline_clearance_mm();
    for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.zone(zone_id);
        if (zone.net().has_value() ||
            polygon_satisfies_outline(zone.outline(), outline, outline_clearance)) {
            continue;
        }
        report.add(board_diagnostic(DiagnosticCode{"PCB_COPPER_OUTSIDE_OUTLINE"},
                                    "Copper does not satisfy the board outline clearance",
                                    std::vector{EntityRef::board_zone(zone_id),
                                                EntityRef::board_layer(zone.layers().front())}));
    }
}
void validate_copper_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                               DiagnosticReport &report) {
    const auto required = board.design_rules().copper_clearance_mm();
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto &lhs = shapes[lhs_index];
            const auto &rhs = shapes[rhs_index];
            if (lhs.net == rhs.net) {
                continue;
            }
            const auto layer = first_common_layer(lhs, rhs);
            if (!layer.has_value()) {
                continue;
            }
            const auto clearance = shape_distance(lhs, rhs) - lhs.radius_mm - rhs.radius_mm;
            if (clearance + board_drc_epsilon >= required) {
                continue;
            }

            auto entities = lhs.primary_entities;
            entities.insert(entities.end(), rhs.primary_entities.begin(),
                            rhs.primary_entities.end());
            entities.push_back(EntityRef::net(lhs.net));
            entities.push_back(EntityRef::net(rhs.net));
            entities.push_back(EntityRef::board_layer(layer.value()));
            report.add(board_diagnostic(DiagnosticCode{"PCB_COPPER_CLEARANCE_VIOLATION"},
                                        "Copper on different nets violates board clearance",
                                        std::move(entities)));
        }
    }
}
[[nodiscard]] bool keepout_restricts(const BoardKeepout &keepout,
                                     BoardKeepoutRestriction restriction) {
    return std::find(keepout.restrictions().begin(), keepout.restrictions().end(),
                     BoardKeepoutRestriction::All) != keepout.restrictions().end() ||
           std::find(keepout.restrictions().begin(), keepout.restrictions().end(), restriction) !=
               keepout.restrictions().end();
}
[[nodiscard]] bool shape_has_entity_kind(const BoardCopperShape &shape, EntityKind kind) {
    return std::any_of(shape.primary_entities.begin(), shape.primary_entities.end(),
                       [kind](EntityRef entity) { return entity.kind() == kind; });
}
[[nodiscard]] std::optional<BoardLayerId>
first_common_keepout_layer(const BoardKeepout &keepout, const std::vector<BoardLayerId> &layers) {
    for (const auto keepout_layer : keepout.layers()) {
        if (std::find(layers.begin(), layers.end(), keepout_layer) != layers.end()) {
            return keepout_layer;
        }
    }
    return std::nullopt;
}
[[nodiscard]] bool shape_violates_keepout(const BoardCopperShape &shape,
                                          const BoardKeepout &keepout) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(shape.points[0], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(shape.points[0], shape.points[1], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    return polygon_polygon_distance(shape.points, keepout.outline()) <= board_drc_epsilon;
}
[[nodiscard]] std::vector<EntityRef>
keepout_copper_entities(BoardKeepoutId keepout, const BoardCopperShape &shape, BoardLayerId layer) {
    auto entities = std::vector{EntityRef::board_keepout(keepout)};
    entities.insert(entities.end(), shape.primary_entities.begin(), shape.primary_entities.end());
    entities.push_back(EntityRef::net(shape.net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}
void validate_keepout_copper_shapes(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                    DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
            continue;
        }
        for (const auto &shape : shapes) {
            if (shape_has_entity_kind(shape, EntityKind::BoardVia) ||
                shape_has_entity_kind(shape, EntityKind::BoardZone)) {
                continue;
            }
            const auto layer = first_common_keepout_layer(keepout, shape.layers);
            if (!layer.has_value() || !shape_violates_keepout(shape, keepout)) {
                continue;
            }
            report.add(board_diagnostic(DiagnosticCode{"PCB_KEEPOUT_COPPER_VIOLATION"},
                                        "Copper violates a board keepout",
                                        keepout_copper_entities(keepout_id, shape, layer.value())));
        }
    }
}
void validate_keepout_zones(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
            continue;
        }
        for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
            const auto zone_id = BoardZoneId{zone_index};
            const auto &zone = board.zone(zone_id);
            const auto layer = first_common_keepout_layer(keepout, zone.layers());
            if (!layer.has_value() ||
                polygon_polygon_distance(zone.outline(), keepout.outline()) > board_drc_epsilon) {
                continue;
            }
            auto entities =
                std::vector{EntityRef::board_keepout(keepout_id), EntityRef::board_zone(zone_id)};
            if (zone.net().has_value()) {
                entities.push_back(EntityRef::net(zone.net().value()));
            }
            entities.push_back(EntityRef::board_layer(layer.value()));
            report.add(board_diagnostic(DiagnosticCode{"PCB_KEEPOUT_COPPER_VIOLATION"},
                                        "Copper zone violates a board keepout",
                                        std::move(entities)));
        }
    }
}
void validate_keepout_vias(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Via)) {
            continue;
        }
        for (std::size_t via_index = 0; via_index < board.via_count(); ++via_index) {
            const auto via_id = BoardViaId{via_index};
            const auto &via = board.via(via_id);
            const auto layers = via_copper_layers(board, via);
            const auto layer = first_common_keepout_layer(keepout, layers);
            if (!layer.has_value() || point_polygon_distance(via.position(), keepout.outline()) >
                                          (via.annular_diameter_mm() / 2.0) + board_drc_epsilon) {
                continue;
            }
            report.add(board_diagnostic(
                DiagnosticCode{"PCB_KEEPOUT_VIA_VIOLATION"}, "Via violates a board keepout",
                std::vector{EntityRef::board_keepout(keepout_id), EntityRef::board_via(via_id),
                            EntityRef::net(via.net()), EntityRef::board_layer(layer.value())}));
        }
    }
}
void validate_keepout_placements(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Placement)) {
            continue;
        }
        for (std::size_t placement_index = 0; placement_index < board.placement_count();
             ++placement_index) {
            const auto placement_id = ComponentPlacementId{placement_index};
            const auto &placement = board.placement(placement_id);
            if (point_polygon_distance(placement.position(), keepout.outline()) >
                board_drc_epsilon) {
                continue;
            }
            report.add(board_diagnostic(DiagnosticCode{"PCB_KEEPOUT_PLACEMENT_VIOLATION"},
                                        "Component placement violates a board keepout",
                                        std::vector{EntityRef::board_keepout(keepout_id),
                                                    EntityRef::component_placement(placement_id),
                                                    EntityRef::component(placement.component())}));
        }
    }
}
[[nodiscard]] std::size_t connectivity_root(std::vector<std::size_t> &parents, std::size_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}
[[nodiscard]] std::optional<std::size_t>
shape_index_for_pad(const std::vector<BoardCopperShape> &shapes, ComponentPlacementId placement,
                    FootprintPadId pad) {
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        if (!shapes[index].pad.has_value()) {
            continue;
        }
        if (shapes[index].pad->placement == placement && shapes[index].pad->pad == pad) {
            return index;
        }
    }
    return std::nullopt;
}
void validate_unrouted_nets(const std::vector<PadResolution> &resolutions,
                            const std::vector<BoardCopperShape> &shapes, DiagnosticReport &report) {
    if (shapes.empty()) {
        return;
    }

    auto parents = std::vector<std::size_t>(shapes.size());
    std::iota(parents.begin(), parents.end(), 0U);
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto &lhs = shapes[lhs_index];
            const auto &rhs = shapes[rhs_index];
            if (lhs.net != rhs.net || !layers_overlap(lhs, rhs)) {
                continue;
            }
            if (shape_distance(lhs, rhs) > lhs.radius_mm + rhs.radius_mm + board_drc_epsilon) {
                continue;
            }
            const auto lhs_root = connectivity_root(parents, lhs_index);
            const auto rhs_root = connectivity_root(parents, rhs_index);
            if (lhs_root != rhs_root) {
                parents[rhs_root] = lhs_root;
            }
        }
    }

    for (const auto &edge : derive_ratsnest_edges(resolutions)) {
        const auto from_index =
            shape_index_for_pad(shapes, edge.from().placement(), edge.from().pad());
        const auto to_index = shape_index_for_pad(shapes, edge.to().placement(), edge.to().pad());
        if (!from_index.has_value() || !to_index.has_value()) {
            continue;
        }
        if (connectivity_root(parents, from_index.value()) ==
            connectivity_root(parents, to_index.value())) {
            continue;
        }

        report.add(board_warning(
            DiagnosticCode{"PCB_NET_UNROUTED"}, "Logical net still has unrouted placed pads",
            std::vector{EntityRef::net(edge.net()),
                        EntityRef::component_placement(edge.from().placement()),
                        EntityRef::footprint_pad(edge.from().pad()),
                        EntityRef::component_placement(edge.to().placement()),
                        EntityRef::footprint_pad(edge.to().pad())}));
    }
}
void validate_board_drc(const Board &board, const FootprintLibrary &footprints,
                        const std::vector<PadResolution> &pad_resolutions,
                        DiagnosticReport &report) {
    validate_track_widths(board, report);
    validate_via_rules(board, report);
    const auto shapes = collect_copper_shapes(board, footprints, pad_resolutions);
    validate_outline_clearance(board, shapes, report);
    validate_netless_zone_outline_clearance(board, report);
    validate_copper_clearance(board, shapes, report);
    validate_keepout_copper_shapes(board, shapes, report);
    validate_keepout_zones(board, report);
    validate_keepout_vias(board, report);
    validate_keepout_placements(board, report);
    validate_unrouted_nets(pad_resolutions, shapes, report);
}

} // namespace volt::detail
