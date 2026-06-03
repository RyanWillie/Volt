#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

BoardHole::BoardHole(BoardPoint center, double drill_diameter_mm, bool plated,
                     std::optional<double> finished_diameter_mm)
    : center_{center}, drill_diameter_mm_{drill_diameter_mm}, plated_{plated},
      finished_diameter_mm_{finished_diameter_mm} {
    if (!std::isfinite(drill_diameter_mm_)) {
        throw std::invalid_argument{"Board hole drill diameter must be finite"};
    }
    if (drill_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board hole drill diameter must be positive"};
    }
    if (finished_diameter_mm_.has_value() && !std::isfinite(finished_diameter_mm_.value())) {
        throw std::invalid_argument{"Board hole finished diameter must be finite"};
    }
    if (finished_diameter_mm_.has_value() && finished_diameter_mm_.value() <= 0.0) {
        throw std::invalid_argument{"Board hole finished diameter must be positive"};
    }
}
BoardSlot::BoardSlot(BoardPoint start, BoardPoint end, double width_mm, bool plated)
    : start_{start}, end_{end}, width_mm_{width_mm}, plated_{plated} {
    if (start_ == end_) {
        throw std::invalid_argument{"Board slot endpoints must be distinct"};
    }
    if (!std::isfinite(width_mm_)) {
        throw std::invalid_argument{"Board slot width must be finite"};
    }
    if (width_mm_ <= 0.0) {
        throw std::invalid_argument{"Board slot width must be positive"};
    }
}
BoardCutout::BoardCutout(std::vector<BoardPoint> outline) : outline_{std::move(outline)} {}
[[nodiscard]] const std::vector<BoardPoint> &BoardCutout::outline() const noexcept {
    return outline_.vertices();
}
BoardFiducial::BoardFiducial(BoardPoint center, double diameter_mm, BoardSide side)
    : center_{center}, diameter_mm_{diameter_mm}, side_{side} {
    if (!std::isfinite(diameter_mm_)) {
        throw std::invalid_argument{"Board fiducial diameter must be finite"};
    }
    if (diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board fiducial diameter must be positive"};
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

[[nodiscard]] BoardFeature BoardFeature::hole(std::string label, BoardPoint center,
                                              double drill_diameter_mm, bool plated,
                                              std::string role,
                                              std::optional<double> finished_diameter_mm) {
    return BoardFeature{BoardFeatureKind::Hole, std::move(label), std::move(role),
                        BoardHole{center, drill_diameter_mm, plated, finished_diameter_mm}};
}
[[nodiscard]] BoardFeature BoardFeature::tooling_hole(std::string label, BoardPoint center,
                                                      double drill_diameter_mm,
                                                      std::optional<double> finished_diameter_mm,
                                                      std::string role) {
    return BoardFeature{BoardFeatureKind::ToolingHole, std::move(label), std::move(role),
                        BoardHole{center, drill_diameter_mm, false, finished_diameter_mm}};
}
[[nodiscard]] BoardFeature BoardFeature::slot(std::string label, BoardPoint start, BoardPoint end,
                                              double width_mm, bool plated, std::string role) {
    return BoardFeature{BoardFeatureKind::Slot, std::move(label), std::move(role),
                        BoardSlot{start, end, width_mm, plated}};
}
[[nodiscard]] BoardFeature BoardFeature::cutout(std::string label, std::vector<BoardPoint> outline,
                                                std::string role) {
    return BoardFeature{BoardFeatureKind::Cutout, std::move(label), std::move(role),
                        BoardCutout{std::move(outline)}};
}
[[nodiscard]] BoardFeature BoardFeature::fiducial(std::string label, BoardPoint center,
                                                  double diameter_mm, BoardSide side) {
    return BoardFeature{BoardFeatureKind::Fiducial, std::move(label), "fiducial",
                        BoardFiducial{center, diameter_mm, side}};
}
[[nodiscard]] BoardFeature BoardFeature::text(BoardText text) {
    return BoardFeature{BoardFeatureKind::Text, {}, {}, std::move(text)};
}
[[nodiscard]] BoardFeature BoardFeature::mechanical_keepout(BoardKeepout keepout) {
    return BoardFeature{BoardFeatureKind::MechanicalKeepout, {}, "mechanical", std::move(keepout)};
}

[[nodiscard]] bool is_board_hole_feature(BoardFeatureKind kind) noexcept {
    switch (kind) {
    case BoardFeatureKind::Hole:
    case BoardFeatureKind::ToolingHole:
        return true;
    case BoardFeatureKind::Cutout:
    case BoardFeatureKind::Fiducial:
    case BoardFeatureKind::MechanicalKeepout:
    case BoardFeatureKind::Slot:
    case BoardFeatureKind::Text:
        return false;
    }
    return false;
}
[[nodiscard]] const BoardHole &BoardFeature::hole() const { return std::get<BoardHole>(payload_); }
[[nodiscard]] const BoardSlot &BoardFeature::slot() const { return std::get<BoardSlot>(payload_); }
[[nodiscard]] const BoardCutout &BoardFeature::cutout() const {
    return std::get<BoardCutout>(payload_);
}
[[nodiscard]] const BoardFiducial &BoardFeature::fiducial() const {
    return std::get<BoardFiducial>(payload_);
}
[[nodiscard]] const BoardText &BoardFeature::text() const { return std::get<BoardText>(payload_); }
[[nodiscard]] const BoardKeepout &BoardFeature::keepout() const {
    return std::get<BoardKeepout>(payload_);
}
BoardFeature::BoardFeature(BoardFeatureKind kind, std::string label, std::string role,
                           Payload payload)
    : kind_{kind}, label_{std::move(label)}, role_{std::move(role)}, payload_{std::move(payload)} {}
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
        const auto net = resolution.net().value();
        endpoints.push_back(EndpointWithNet{
            net,
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

} // namespace volt::detail
