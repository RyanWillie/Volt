#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volt {

/** Direction used by schematic presentation objects. */
enum class SchematicOrientation {
    Right,
    Down,
    Left,
    Up,
};

/** Two-dimensional schematic coordinate in drawing units. */
class Point {
  public:
    /** Construct a finite point. */
    Point(double x, double y) : x_{x}, y_{y} {
        if (!std::isfinite(x_) || !std::isfinite(y_)) {
            throw std::invalid_argument{"Schematic point coordinates must be finite"};
        }
    }

    /** Return the horizontal coordinate. */
    [[nodiscard]] double x() const noexcept { return x_; }

    /** Return the vertical coordinate. */
    [[nodiscard]] double y() const noexcept { return y_; }

    /** Return whether two points have the same coordinates. */
    [[nodiscard]] friend bool operator==(Point lhs, Point rhs) noexcept = default;

  private:
    double x_;
    double y_;
};

/** One drawn schematic segment between two sheet-local points. */
class SchematicSegment {
  public:
    /** Construct a schematic segment. */
    SchematicSegment(Point start, Point end) : start_{start}, end_{end} {}

    /** Return the segment start point. */
    [[nodiscard]] Point start() const noexcept { return start_; }

    /** Return the segment end point. */
    [[nodiscard]] Point end() const noexcept { return end_; }

  private:
    Point start_;
    Point end_;
};

/** Geometric relationship between two schematic wire segments. */
enum class SchematicSegmentRelationship {
    Disjoint,
    EndpointTouch,
    Crossing,
    Overlap,
};

/** Whether an explicit schematic junction object is present at a segment intersection. */
enum class SchematicJunction {
    Absent,
    Present,
};

namespace detail {

inline constexpr double schematic_geometry_tolerance = 1e-9;

[[nodiscard]] inline bool nearly_equal(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) <= schematic_geometry_tolerance;
}

[[nodiscard]] inline double cross(Point origin, Point a, Point b) noexcept {
    return ((a.x() - origin.x()) * (b.y() - origin.y())) -
           ((a.y() - origin.y()) * (b.x() - origin.x()));
}

[[nodiscard]] inline bool between(double value, double first, double second) noexcept {
    const auto minimum = std::min(first, second) - schematic_geometry_tolerance;
    const auto maximum = std::max(first, second) + schematic_geometry_tolerance;
    return minimum <= value && value <= maximum;
}

[[nodiscard]] inline double projected_coordinate(Point point, bool use_x) noexcept {
    return use_x ? point.x() : point.y();
}

} // namespace detail

/** Return whether two schematic points are the same within the geometry tolerance. */
[[nodiscard]] inline bool same_schematic_point(Point lhs, Point rhs) noexcept {
    return detail::nearly_equal(lhs.x(), rhs.x()) && detail::nearly_equal(lhs.y(), rhs.y());
}

/** Transform a symbol-local point into sheet coordinates. */
[[nodiscard]] inline Point transform_schematic_point(Point local, Point origin,
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

/** Return whether a point lies on a schematic segment. */
[[nodiscard]] inline bool point_on_schematic_segment(Point point,
                                                     SchematicSegment segment) noexcept {
    return detail::nearly_equal(detail::cross(segment.start(), segment.end(), point), 0.0) &&
           detail::between(point.x(), segment.start().x(), segment.end().x()) &&
           detail::between(point.y(), segment.start().y(), segment.end().y());
}

/** Classify how two schematic segments relate geometrically. */
[[nodiscard]] inline SchematicSegmentRelationship
classify_segment_relationship(SchematicSegment first, SchematicSegment second) noexcept {
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

/** Return whether same-net segments are visually joined by this relationship. */
[[nodiscard]] inline bool same_net_segments_join(SchematicSegmentRelationship relationship,
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

/** Return whether different-net segments conflict under the schematic geometry rules. */
[[nodiscard]] inline bool different_net_segments_collide(SchematicSegmentRelationship relationship,
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
