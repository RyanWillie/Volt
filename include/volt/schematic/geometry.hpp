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
    Point(double x, double y);

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
    SchematicSegment(Point start, Point end);

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

[[nodiscard]] bool nearly_equal(double lhs, double rhs) noexcept;

[[nodiscard]] double cross(Point origin, Point a, Point b) noexcept;

[[nodiscard]] bool between(double value, double first, double second) noexcept;

[[nodiscard]] double projected_coordinate(Point point, bool use_x) noexcept;

} // namespace detail

/** Return whether two schematic points are the same within the geometry tolerance. */
[[nodiscard]] bool same_schematic_point(Point lhs, Point rhs) noexcept;

/** Transform a symbol-local point into sheet coordinates. */
[[nodiscard]] Point transform_schematic_point(Point local, Point origin,
                                              SchematicOrientation orientation);

/** Return whether a point lies on a schematic segment. */
[[nodiscard]] bool point_on_schematic_segment(Point point, SchematicSegment segment) noexcept;

/** Classify how two schematic segments relate geometrically. */
[[nodiscard]] SchematicSegmentRelationship
classify_segment_relationship(SchematicSegment first, SchematicSegment second) noexcept;

/** Return whether same-net segments are visually joined by this relationship. */
[[nodiscard]] bool same_net_segments_join(SchematicSegmentRelationship relationship,
                                          SchematicJunction junction) noexcept;

/** Return whether different-net segments conflict under the schematic geometry rules. */
[[nodiscard]] bool different_net_segments_collide(SchematicSegmentRelationship relationship,
                                                  SchematicJunction junction) noexcept;

} // namespace volt
