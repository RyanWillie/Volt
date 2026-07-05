#pragma once

#include <volt/core/errors.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace volt::detail {

inline constexpr double kFootprintPolygonEpsilon = 1.0e-9;

template <typename Point>
[[nodiscard]] double footprint_polygon_area_twice(const std::vector<Point> &vertices) {
    double area = 0.0;
    std::size_t previous = vertices.size() - 1U;
    for (std::size_t current = 0; current < vertices.size(); ++current) {
        area += vertices[previous].x_mm() * vertices[current].y_mm();
        area -= vertices[current].x_mm() * vertices[previous].y_mm();
        previous = current;
    }
    return area;
}

template <typename Point>
[[nodiscard]] double footprint_polygon_orientation(Point a, Point b, Point c) noexcept {
    return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
           ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
}

template <typename Point>
[[nodiscard]] bool footprint_polygon_point_on_segment(Point point, Point a, Point b) noexcept {
    const auto cross = ((point.y_mm() - a.y_mm()) * (b.x_mm() - a.x_mm())) -
                       ((point.x_mm() - a.x_mm()) * (b.y_mm() - a.y_mm()));
    if (std::abs(cross) > kFootprintPolygonEpsilon) {
        return false;
    }

    const auto dot = ((point.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                     ((point.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    if (dot < -kFootprintPolygonEpsilon) {
        return false;
    }

    const auto length_squared = ((b.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                                ((b.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
    return dot <= length_squared + kFootprintPolygonEpsilon;
}

template <typename Point>
[[nodiscard]] bool footprint_polygon_segments_intersect(Point a, Point b, Point c,
                                                        Point d) noexcept {
    const auto ab_c = footprint_polygon_orientation(a, b, c);
    const auto ab_d = footprint_polygon_orientation(a, b, d);
    const auto cd_a = footprint_polygon_orientation(c, d, a);
    const auto cd_b = footprint_polygon_orientation(c, d, b);

    if (std::abs(ab_c) <= kFootprintPolygonEpsilon && footprint_polygon_point_on_segment(c, a, b)) {
        return true;
    }
    if (std::abs(ab_d) <= kFootprintPolygonEpsilon && footprint_polygon_point_on_segment(d, a, b)) {
        return true;
    }
    if (std::abs(cd_a) <= kFootprintPolygonEpsilon && footprint_polygon_point_on_segment(a, c, d)) {
        return true;
    }
    if (std::abs(cd_b) <= kFootprintPolygonEpsilon && footprint_polygon_point_on_segment(b, c, d)) {
        return true;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}

template <typename Point>
[[nodiscard]] bool footprint_polygon_has_duplicate_vertices(const std::vector<Point> &vertices) {
    for (std::size_t first = 0; first < vertices.size(); ++first) {
        for (std::size_t second = first + 1U; second < vertices.size(); ++second) {
            if (vertices[first] == vertices[second]) {
                return true;
            }
        }
    }
    return false;
}

template <typename Point>
[[nodiscard]] bool footprint_polygon_has_self_intersection(const std::vector<Point> &vertices) {
    for (std::size_t first = 0; first < vertices.size(); ++first) {
        const auto first_next = (first + 1U) % vertices.size();
        for (std::size_t second = first + 1U; second < vertices.size(); ++second) {
            const auto second_next = (second + 1U) % vertices.size();
            const auto adjacent = first == second || first_next == second || second_next == first ||
                                  (first == 0U && second_next == 0U);
            if (adjacent) {
                continue;
            }

            if (footprint_polygon_segments_intersect(vertices[first], vertices[first_next],
                                                     vertices[second], vertices[second_next])) {
                return true;
            }
        }
    }

    return false;
}

template <typename Point>
void validate_footprint_polygon_vertices(const std::vector<Point> &vertices,
                                         std::string_view label) {
    if (vertices.size() < 3U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  std::string{label} + " must contain at least three vertices"};
    }
    if (footprint_polygon_has_duplicate_vertices(vertices)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  std::string{label} + " must not repeat vertices"};
    }
    if (std::abs(footprint_polygon_area_twice(vertices)) <= kFootprintPolygonEpsilon) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  std::string{label} + " area must be positive"};
    }
    if (footprint_polygon_has_self_intersection(vertices)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  std::string{label} + " must not self-intersect"};
    }
}

} // namespace volt::detail
