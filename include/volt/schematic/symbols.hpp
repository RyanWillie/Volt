#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/schematic/geometry.hpp>

namespace volt {

/** Optional presentation role for a schematic symbol line primitive. */
enum class SymbolLineRole {
    Normal,
    TerminalLeadStart,
    TerminalLeadEnd,
};

/** Horizontal alignment for kernel-owned schematic text. */
enum class TextHorizontalAlignment {
    Start,
    Middle,
    End,
};

/** Vertical alignment/baseline for kernel-owned schematic text. */
enum class TextVerticalAlignment {
    Top,
    Middle,
    Bottom,
    Baseline,
};

/** Generic presentation metadata for schematic text. */
class SchematicTextStyle {
  public:
    /** Construct schematic text style metadata. */
    explicit SchematicTextStyle(
        TextHorizontalAlignment horizontal_alignment = TextHorizontalAlignment::Middle,
        TextVerticalAlignment vertical_alignment = TextVerticalAlignment::Baseline,
        std::optional<double> font_size = std::nullopt);

    /** Return horizontal text alignment. */
    [[nodiscard]] TextHorizontalAlignment horizontal_alignment() const noexcept;

    /** Return vertical text alignment/baseline. */
    [[nodiscard]] TextVerticalAlignment vertical_alignment() const noexcept;

    /** Return optional rendered font size override. */
    [[nodiscard]] const std::optional<double> &font_size() const noexcept { return font_size_; }

    /** Return whether two schematic text styles are identical. */
    [[nodiscard]] friend bool operator==(const SchematicTextStyle &lhs,
                                         const SchematicTextStyle &rhs) noexcept = default;

  private:
    TextHorizontalAlignment horizontal_alignment_;
    TextVerticalAlignment vertical_alignment_;
    std::optional<double> font_size_;
};

/** A straight line in a structured schematic symbol definition. */
class SymbolLine {
  public:
    /** Construct a symbol line from start and end points. */
    SymbolLine(Point start, Point end, SymbolLineRole role = SymbolLineRole::Normal)
        : start_{start}, end_{end}, role_{role} {}

    /** Return the start point. */
    [[nodiscard]] Point start() const noexcept { return start_; }

    /** Return the end point. */
    [[nodiscard]] Point end() const noexcept { return end_; }

    /** Return the schematic presentation role for this line. */
    [[nodiscard]] SymbolLineRole role() const noexcept { return role_; }

    /** Return whether two symbol lines have the same geometry. */
    [[nodiscard]] friend bool operator==(SymbolLine lhs, SymbolLine rhs) noexcept = default;

  private:
    Point start_;
    Point end_;
    SymbolLineRole role_;
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
    SymbolCircle(Point center, double radius);

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
    SymbolArc(Point center, double radius, double start_degrees, double sweep_degrees);

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
               SchematicOrientation orientation = SchematicOrientation::Right,
               SchematicTextStyle style = SchematicTextStyle{});

    /** Return the displayed text. */
    [[nodiscard]] const std::string &text() const noexcept { return text_; }

    /** Return the symbol-local text anchor. */
    [[nodiscard]] Point anchor() const noexcept { return anchor_; }

    /** Return the text orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return generic text presentation metadata. */
    [[nodiscard]] SchematicTextStyle style() const noexcept { return style_; }

    /** Return whether two symbol text primitives have the same content and geometry. */
    [[nodiscard]] friend bool operator==(const SymbolText &lhs,
                                         const SymbolText &rhs) noexcept = default;

  private:
    std::string text_;
    Point anchor_;
    SchematicOrientation orientation_;
    SchematicTextStyle style_;
};

/** Canonical structured drawing primitive for schematic symbols. */
using SymbolPrimitive =
    std::variant<SymbolLine, SymbolRectangle, SymbolCircle, SymbolArc, SymbolText>;

/** Visual pin anchor belonging to a schematic symbol definition. */
class SymbolPin {
  public:
    /** Construct a symbol pin with display name, pin number, anchor, and orientation. */
    SymbolPin(std::string name, std::string number, Point anchor, SchematicOrientation orientation);

    /** Return the displayed pin name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the displayed pin number. */
    [[nodiscard]] const std::string &number() const noexcept { return number_; }

    /** Return the symbol-local pin anchor. */
    [[nodiscard]] Point anchor() const noexcept { return anchor_; }

    /** Return the pin orientation at the anchor. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return whether two symbol pins have the same displayed endpoint. */
    [[nodiscard]] friend bool operator==(const SymbolPin &lhs,
                                         const SymbolPin &rhs) noexcept = default;

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
    explicit SymbolDefinition(std::string name);

    /** Add a visual pin anchor to the symbol definition. */
    void add_pin(SymbolPin pin);

    /** Add a structured drawing primitive to the symbol definition. */
    void add_primitive(SymbolPrimitive primitive);

    /** Return the symbol definition name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return visual pin anchors in insertion order. */
    [[nodiscard]] const std::vector<SymbolPin> &pins() const noexcept { return pins_; }

    /** Return structured drawing primitives in insertion order. */
    [[nodiscard]] const std::vector<SymbolPrimitive> &primitives() const noexcept;

    /** Return whether two symbol definitions have the same name and geometry. */
    [[nodiscard]] friend bool operator==(const SymbolDefinition &lhs,
                                         const SymbolDefinition &rhs) noexcept = default;

  private:
    std::string name_;
    std::vector<SymbolPin> pins_;
    std::vector<SymbolPrimitive> primitives_;
};

} // namespace volt
