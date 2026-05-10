#pragma once

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

} // namespace volt
