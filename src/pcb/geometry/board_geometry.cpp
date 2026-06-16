#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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

} // namespace volt

namespace volt::detail {

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

namespace {

[[nodiscard]] BoardPoint outline_segment_midpoint(BoardPoint lhs, BoardPoint rhs) {
    return BoardPoint{(lhs.x_mm() + rhs.x_mm()) / 2.0, (lhs.y_mm() + rhs.y_mm()) / 2.0};
}

} // namespace

[[nodiscard]] bool outline_contains_disc(const BoardOutline &outline, BoardPoint center,
                                         double radius_mm, double clearance_mm) {
    if (!outline.contains(center)) {
        return false;
    }
    return outline_boundary_distance(outline, center) + board_drc_epsilon >=
           radius_mm + clearance_mm;
}

[[nodiscard]] bool outline_contains_segment(const BoardOutline &outline, BoardPoint start,
                                            BoardPoint end, double radius_mm, double clearance_mm) {
    if (!outline.contains(start) || !outline.contains(end) ||
        !outline.contains(outline_segment_midpoint(start, end))) {
        return false;
    }
    return segment_outline_boundary_distance(outline, start, end) + board_drc_epsilon >=
           radius_mm + clearance_mm;
}

[[nodiscard]] bool outline_contains_polygon(const BoardOutline &outline,
                                            const std::vector<BoardPoint> &polygon,
                                            double clearance_mm) {
    const auto &outline_vertices = outline.vertices();
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto next = (index + 1U) % polygon.size();
        if (!outline.contains(polygon[index]) ||
            !outline.contains(outline_segment_midpoint(polygon[index], polygon[next]))) {
            return false;
        }
        for (std::size_t outline_index = 0; outline_index < outline_vertices.size();
             ++outline_index) {
            const auto outline_next = (outline_index + 1U) % outline_vertices.size();
            if (segments_cross_properly(polygon[index], polygon[next],
                                        outline_vertices[outline_index],
                                        outline_vertices[outline_next])) {
                return false;
            }
        }
    }
    return polygon_outline_boundary_distance(outline, polygon) + board_drc_epsilon >= clearance_mm;
}

} // namespace volt::detail
