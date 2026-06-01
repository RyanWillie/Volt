#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace volt {

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
