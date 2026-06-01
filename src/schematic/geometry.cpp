#include <volt/schematic/geometry.hpp>

#include <cmath>
#include <stdexcept>

namespace volt {

Point::Point(double x, double y) : x_{x}, y_{y} {
    if (!std::isfinite(x_) || !std::isfinite(y_)) {
        throw std::invalid_argument{"Schematic point coordinates must be finite"};
    }
}
SchematicSegment::SchematicSegment(Point start, Point end) : start_{start}, end_{end} {}
[[nodiscard]] bool same_schematic_point(Point lhs, Point rhs) noexcept {
    return detail::nearly_equal(lhs.x(), rhs.x()) && detail::nearly_equal(lhs.y(), rhs.y());
}
[[nodiscard]] Point transform_schematic_point(Point local, Point origin,
                                              SchematicOrientation orientation) {
    auto rotated = local;
    switch (orientation) {
    case SchematicOrientation::Right:
        rotated = local;
        break;
    case SchematicOrientation::Down:
        rotated = Point{-local.y(), local.x()};
        break;
    case SchematicOrientation::Left:
        rotated = Point{-local.x(), -local.y()};
        break;
    case SchematicOrientation::Up:
        rotated = Point{local.y(), -local.x()};
        break;
    }

    return Point{origin.x() + rotated.x(), origin.y() + rotated.y()};
}
[[nodiscard]] bool point_on_schematic_segment(Point point, SchematicSegment segment) noexcept {
    return detail::nearly_equal(detail::cross(segment.start(), segment.end(), point), 0.0) &&
           detail::between(point.x(), segment.start().x(), segment.end().x()) &&
           detail::between(point.y(), segment.start().y(), segment.end().y());
}
[[nodiscard]] SchematicSegmentRelationship
classify_segment_relationship(SchematicSegment first, SchematicSegment second) noexcept {
    const auto first_is_point = same_schematic_point(first.start(), first.end());
    const auto second_is_point = same_schematic_point(second.start(), second.end());
    if (first_is_point && second_is_point) {
        return same_schematic_point(first.start(), second.start())
                   ? SchematicSegmentRelationship::EndpointTouch
                   : SchematicSegmentRelationship::Disjoint;
    }
    if (first_is_point) {
        return point_on_schematic_segment(first.start(), second)
                   ? SchematicSegmentRelationship::EndpointTouch
                   : SchematicSegmentRelationship::Disjoint;
    }
    if (second_is_point) {
        return point_on_schematic_segment(second.start(), first)
                   ? SchematicSegmentRelationship::EndpointTouch
                   : SchematicSegmentRelationship::Disjoint;
    }

    const auto first_to_second_start = detail::cross(first.start(), first.end(), second.start());
    const auto first_to_second_end = detail::cross(first.start(), first.end(), second.end());
    const auto second_to_first_start = detail::cross(second.start(), second.end(), first.start());
    const auto second_to_first_end = detail::cross(second.start(), second.end(), first.end());

    const auto collinear = detail::nearly_equal(first_to_second_start, 0.0) &&
                           detail::nearly_equal(first_to_second_end, 0.0);
    if (collinear) {
        const auto use_x = std::abs(first.end().x() - first.start().x()) >=
                           std::abs(first.end().y() - first.start().y());
        const auto first_start = detail::projected_coordinate(first.start(), use_x);
        const auto first_end = detail::projected_coordinate(first.end(), use_x);
        const auto second_start = detail::projected_coordinate(second.start(), use_x);
        const auto second_end = detail::projected_coordinate(second.end(), use_x);
        const auto overlap_start =
            std::max(std::min(first_start, first_end), std::min(second_start, second_end));
        const auto overlap_end =
            std::min(std::max(first_start, first_end), std::max(second_start, second_end));

        if (overlap_end < overlap_start - detail::schematic_geometry_tolerance) {
            return SchematicSegmentRelationship::Disjoint;
        }
        if (detail::nearly_equal(overlap_start, overlap_end)) {
            return SchematicSegmentRelationship::EndpointTouch;
        }
        return SchematicSegmentRelationship::Overlap;
    }

    if (point_on_schematic_segment(first.start(), second) ||
        point_on_schematic_segment(first.end(), second) ||
        point_on_schematic_segment(second.start(), first) ||
        point_on_schematic_segment(second.end(), first)) {
        return SchematicSegmentRelationship::EndpointTouch;
    }

    const auto first_straddles = (first_to_second_start < -detail::schematic_geometry_tolerance &&
                                  first_to_second_end > detail::schematic_geometry_tolerance) ||
                                 (first_to_second_start > detail::schematic_geometry_tolerance &&
                                  first_to_second_end < -detail::schematic_geometry_tolerance);
    const auto second_straddles = (second_to_first_start < -detail::schematic_geometry_tolerance &&
                                   second_to_first_end > detail::schematic_geometry_tolerance) ||
                                  (second_to_first_start > detail::schematic_geometry_tolerance &&
                                   second_to_first_end < -detail::schematic_geometry_tolerance);
    if (first_straddles && second_straddles) {
        return SchematicSegmentRelationship::Crossing;
    }

    return SchematicSegmentRelationship::Disjoint;
}
[[nodiscard]] bool same_net_segments_join(SchematicSegmentRelationship relationship,
                                          SchematicJunction junction) noexcept {
    switch (relationship) {
    case SchematicSegmentRelationship::Disjoint:
        return false;
    case SchematicSegmentRelationship::EndpointTouch:
    case SchematicSegmentRelationship::Overlap:
        return true;
    case SchematicSegmentRelationship::Crossing:
        return junction == SchematicJunction::Present;
    }

    return false;
}
[[nodiscard]] bool different_net_segments_collide(SchematicSegmentRelationship relationship,
                                                  SchematicJunction junction) noexcept {
    switch (relationship) {
    case SchematicSegmentRelationship::Disjoint:
        return false;
    case SchematicSegmentRelationship::EndpointTouch:
    case SchematicSegmentRelationship::Overlap:
        return true;
    case SchematicSegmentRelationship::Crossing:
        return junction == SchematicJunction::Present;
    }

    return false;
}

} // namespace volt

namespace volt::detail {

[[nodiscard]] bool nearly_equal(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) <= schematic_geometry_tolerance;
}
[[nodiscard]] double cross(Point origin, Point a, Point b) noexcept {
    return ((a.x() - origin.x()) * (b.y() - origin.y())) -
           ((a.y() - origin.y()) * (b.x() - origin.x()));
}
[[nodiscard]] bool between(double value, double first, double second) noexcept {
    const auto minimum = std::min(first, second) - schematic_geometry_tolerance;
    const auto maximum = std::max(first, second) + schematic_geometry_tolerance;
    return minimum <= value && value <= maximum;
}
[[nodiscard]] double projected_coordinate(Point point, bool use_x) noexcept {
    return use_x ? point.x() : point.y();
}

} // namespace volt::detail
