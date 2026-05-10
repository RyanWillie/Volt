#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/schematic/geometry.hpp>

namespace volt {

/** A straight line in a structured schematic symbol definition. */
class SymbolLine {
  public:
    /** Construct a symbol line from start and end points. */
    SymbolLine(Point start, Point end) : start_{start}, end_{end} {}

    /** Return the start point. */
    [[nodiscard]] Point start() const noexcept { return start_; }

    /** Return the end point. */
    [[nodiscard]] Point end() const noexcept { return end_; }

    /** Return whether two symbol lines have the same geometry. */
    [[nodiscard]] friend bool operator==(SymbolLine lhs, SymbolLine rhs) noexcept = default;

  private:
    Point start_;
    Point end_;
};

/** A rectangle in a structured schematic symbol definition. */
class SymbolRectangle {
  public:
    /** Construct a symbol rectangle from two opposing corners. */
    SymbolRectangle(Point first_corner, Point second_corner)
        : first_corner_{first_corner}, second_corner_{second_corner} {}

    /** Return the first corner. */
    [[nodiscard]] Point first_corner() const noexcept { return first_corner_; }

    /** Return the opposing corner. */
    [[nodiscard]] Point second_corner() const noexcept { return second_corner_; }

    /** Return whether two symbol rectangles have the same geometry. */
    [[nodiscard]] friend bool operator==(SymbolRectangle lhs,
                                         SymbolRectangle rhs) noexcept = default;

  private:
    Point first_corner_;
    Point second_corner_;
};

/** A circle in a structured schematic symbol definition. */
class SymbolCircle {
  public:
    /** Construct a symbol circle from center point and positive radius. */
    SymbolCircle(Point center, double radius) : center_{center}, radius_{radius} {
        if (!std::isfinite(radius_) || radius_ <= 0.0) {
            throw std::invalid_argument{"Symbol circle radius must be finite and positive"};
        }
    }

    /** Return the circle center. */
    [[nodiscard]] Point center() const noexcept { return center_; }

    /** Return the circle radius. */
    [[nodiscard]] double radius() const noexcept { return radius_; }

    /** Return whether two symbol circles have the same geometry. */
    [[nodiscard]] friend bool operator==(SymbolCircle lhs, SymbolCircle rhs) noexcept = default;

  private:
    Point center_;
    double radius_;
};

/** A circular arc in a structured schematic symbol definition. */
class SymbolArc {
  public:
    /** Construct a symbol arc from center, radius, start angle, and sweep angle. */
    SymbolArc(Point center, double radius, double start_degrees, double sweep_degrees)
        : center_{center}, radius_{radius}, start_degrees_{start_degrees},
          sweep_degrees_{sweep_degrees} {
        if (!std::isfinite(radius_) || radius_ <= 0.0) {
            throw std::invalid_argument{"Symbol arc radius must be finite and positive"};
        }
        if (!std::isfinite(start_degrees_) || !std::isfinite(sweep_degrees_)) {
            throw std::invalid_argument{"Symbol arc angles must be finite"};
        }
    }

    /** Return the arc center. */
    [[nodiscard]] Point center() const noexcept { return center_; }

    /** Return the arc radius. */
    [[nodiscard]] double radius() const noexcept { return radius_; }

    /** Return the start angle in degrees. */
    [[nodiscard]] double start_degrees() const noexcept { return start_degrees_; }

    /** Return the sweep angle in degrees. */
    [[nodiscard]] double sweep_degrees() const noexcept { return sweep_degrees_; }

    /** Return whether two symbol arcs have the same geometry. */
    [[nodiscard]] friend bool operator==(SymbolArc lhs, SymbolArc rhs) noexcept = default;

  private:
    Point center_;
    double radius_;
    double start_degrees_;
    double sweep_degrees_;
};

/** Text in a structured schematic symbol definition. */
class SymbolText {
  public:
    /** Construct symbol text with content, anchor, and orientation. */
    SymbolText(std::string text, Point anchor,
               SchematicOrientation orientation = SchematicOrientation::Right)
        : text_{std::move(text)}, anchor_{anchor}, orientation_{orientation} {
        if (text_.empty()) {
            throw std::invalid_argument{"Symbol text must not be empty"};
        }
    }

    /** Return the displayed text. */
    [[nodiscard]] const std::string &text() const noexcept { return text_; }

    /** Return the symbol-local text anchor. */
    [[nodiscard]] Point anchor() const noexcept { return anchor_; }

    /** Return the text orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    std::string text_;
    Point anchor_;
    SchematicOrientation orientation_;
};

/** Canonical structured drawing primitive for schematic symbols. */
using SymbolPrimitive =
    std::variant<SymbolLine, SymbolRectangle, SymbolCircle, SymbolArc, SymbolText>;

/** Visual pin anchor belonging to a schematic symbol definition. */
class SymbolPin {
  public:
    /** Construct a symbol pin with display name, pin number, anchor, and orientation. */
    SymbolPin(std::string name, std::string number, Point anchor, SchematicOrientation orientation)
        : name_{std::move(name)}, number_{std::move(number)}, anchor_{anchor},
          orientation_{orientation} {
        if (name_.empty()) {
            throw std::invalid_argument{"Symbol pin name must not be empty"};
        }
        if (number_.empty()) {
            throw std::invalid_argument{"Symbol pin number must not be empty"};
        }
    }

    /** Return the displayed pin name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the displayed pin number. */
    [[nodiscard]] const std::string &number() const noexcept { return number_; }

    /** Return the symbol-local pin anchor. */
    [[nodiscard]] Point anchor() const noexcept { return anchor_; }

    /** Return the pin orientation at the anchor. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    std::string name_;
    std::string number_;
    Point anchor_;
    SchematicOrientation orientation_;
};

/** Reusable schematic symbol definition made from structured geometry and pin anchors. */
class SymbolDefinition {
  public:
    /** Construct a named symbol definition. */
    explicit SymbolDefinition(std::string name) : name_{std::move(name)} {
        if (name_.empty()) {
            throw std::invalid_argument{"Symbol definition name must not be empty"};
        }
    }

    /** Add a visual pin anchor to the symbol definition. */
    void add_pin(SymbolPin pin) {
        const auto duplicate = std::any_of(pins_.begin(), pins_.end(), [&pin](const auto &other) {
            return other.number() == pin.number();
        });
        if (duplicate) {
            throw std::logic_error{"Symbol pin number already exists"};
        }

        pins_.push_back(std::move(pin));
    }

    /** Add a structured drawing primitive to the symbol definition. */
    void add_primitive(SymbolPrimitive primitive) { primitives_.push_back(std::move(primitive)); }

    /** Return the symbol definition name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return visual pin anchors in insertion order. */
    [[nodiscard]] const std::vector<SymbolPin> &pins() const noexcept { return pins_; }

    /** Return structured drawing primitives in insertion order. */
    [[nodiscard]] const std::vector<SymbolPrimitive> &primitives() const noexcept {
        return primitives_;
    }

  private:
    std::string name_;
    std::vector<SymbolPin> pins_;
    std::vector<SymbolPrimitive> primitives_;
};

} // namespace volt
