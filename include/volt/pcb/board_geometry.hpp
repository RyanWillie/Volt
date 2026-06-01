#pragma once

#include <string>

namespace volt {

/** Human-facing PCB board name. */
class BoardName {
  public:
    /** Construct a non-empty board name. */
    explicit BoardName(std::string value);

    /** Return the stored board name. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two board names carry the same value. */
    [[nodiscard]] friend bool operator==(const BoardName &lhs, const BoardName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Units used by first PCB board geometry values. */
enum class BoardUnits {
    Millimeters,
};

/** Board coordinate in millimeters. */
class BoardPoint {
  public:
    /** Construct a finite board point. */
    BoardPoint(double x_mm, double y_mm);

    /** Return the X coordinate in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the Y coordinate in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Return whether two points have the same coordinates. */
    [[nodiscard]] friend bool operator==(BoardPoint lhs, BoardPoint rhs) noexcept = default;

  private:
    double x_mm_;
    double y_mm_;
};

/** Two-dimensional board size in millimeters. */
class BoardSize {
  public:
    /** Construct a positive finite board size. */
    BoardSize(double width_mm, double height_mm);

    /** Return the width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

    /** Return the height in millimeters. */
    [[nodiscard]] double height_mm() const noexcept { return height_mm_; }

    /** Return whether two sizes have the same dimensions. */
    [[nodiscard]] friend bool operator==(BoardSize lhs, BoardSize rhs) noexcept = default;

  private:
    double width_mm_;
    double height_mm_;
};

/** Rotation in board coordinates, stored in degrees. */
class BoardRotation {
  public:
    /** Construct a finite rotation in degrees. */
    [[nodiscard]] static BoardRotation degrees(double value) { return BoardRotation{value}; }

    /** Return the stored rotation in degrees. */
    [[nodiscard]] double degrees() const noexcept { return degrees_; }

    /** Return whether two rotations have the same degrees. */
    [[nodiscard]] friend bool operator==(BoardRotation lhs, BoardRotation rhs) noexcept = default;

  private:
    explicit BoardRotation(double degrees);

    double degrees_;
};

/** Component placement side. */
enum class BoardSide {
    Top,
    Bottom,
};

} // namespace volt
