#include <volt/pcb/board_outline.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {

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

} // namespace volt
