#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_layers.hpp>
#include <volt/pcb/board_outline.hpp>

namespace volt {

/** Fill intent for a stored copper zone. */
enum class BoardZoneFill {
    Solid,
};

/** Kernel-owned copper area intent on one or more board copper layers. */
class BoardZone {
  public:
    /** Construct a copper zone with a polygon outline and optional existing logical net. */
    BoardZone(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
              std::optional<NetId> net = std::nullopt, BoardZoneFill fill = BoardZoneFill::Solid,
              int priority = 0);

    /** Return the polygon outline. */
    [[nodiscard]] const std::vector<BoardPoint> &outline() const noexcept;

    /** Return board layers on which this zone exists. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return the existing logical net this zone is tied to, if any. */
    [[nodiscard]] std::optional<NetId> net() const noexcept { return net_; }

    /** Return zone fill intent. */
    [[nodiscard]] BoardZoneFill fill() const noexcept { return fill_; }

    /** Return deterministic priority/order metadata. */
    [[nodiscard]] int priority() const noexcept { return priority_; }

  private:
    void validate_layers() const;

    BoardPolygon outline_;
    std::vector<BoardLayerId> layers_;
    std::optional<NetId> net_;
    BoardZoneFill fill_;
    int priority_;
};

/** Object kinds restricted by a board keepout. */
enum class BoardKeepoutRestriction {
    Copper,
    Via,
    Placement,
    All,
};

/** Kernel-owned board keepout constraint over a polygonal scope. */
class BoardKeepout {
  public:
    /** Construct a keepout over board layers and restricted object kinds. */
    BoardKeepout(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                 std::vector<BoardKeepoutRestriction> restrictions);

    /** Return keepout polygon vertices. */
    [[nodiscard]] const std::vector<BoardPoint> &outline() const noexcept;

    /** Return board layers this keepout applies to. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return restricted object kinds. */
    [[nodiscard]] const std::vector<BoardKeepoutRestriction> &restrictions() const noexcept;

  private:
    void validate_layers() const;

    void validate_restrictions() const;

    BoardPolygon outline_;
    std::vector<BoardLayerId> layers_;
    std::vector<BoardKeepoutRestriction> restrictions_;
};

/** Kernel-owned board text/annotation primitive. */
class BoardText {
  public:
    /** Construct board text on an existing board layer. */
    BoardText(std::string text, BoardPoint position, BoardRotation rotation, BoardLayerId layer,
              double size_mm, bool locked = false);

    /** Return text content. */
    [[nodiscard]] const std::string &text() const noexcept { return text_; }

    /** Return text anchor position. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return text rotation. */
    [[nodiscard]] BoardRotation rotation() const noexcept { return rotation_; }

    /** Return board layer. */
    [[nodiscard]] BoardLayerId layer() const noexcept { return layer_; }

    /** Return text size in millimeters. */
    [[nodiscard]] double size_mm() const noexcept { return size_mm_; }

    /** Return whether the text is locked against movement. */
    [[nodiscard]] bool locked() const noexcept { return locked_; }

  private:
    std::string text_;
    BoardPoint position_;
    BoardRotation rotation_;
    BoardLayerId layer_;
    double size_mm_;
    bool locked_;
};

/** Routed copper track that physically implements an existing logical net. */
class BoardTrack {
  public:
    /** Construct a routed track on one board copper layer. */
    BoardTrack(NetId net, BoardLayerId layer, std::vector<BoardPoint> points, double width_mm);

    /** Return the existing logical net this track implements. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the board-owned copper layer this track is on. */
    [[nodiscard]] BoardLayerId layer() const noexcept { return layer_; }

    /** Return ordered board-space track points. */
    [[nodiscard]] const std::vector<BoardPoint> &points() const noexcept { return points_; }

    /** Return the track width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

  private:
    NetId net_;
    BoardLayerId layer_;
    std::vector<BoardPoint> points_;
    double width_mm_;
};

/** Routed copper via that physically implements an existing logical net across layers. */
class BoardVia {
  public:
    /** Construct a routed via between two distinct board copper layers. */
    BoardVia(NetId net, BoardPoint position, BoardLayerId start_layer, BoardLayerId end_layer,
             double drill_diameter_mm, double annular_diameter_mm);

    /** Return the existing logical net this via implements. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the via center in board coordinates. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the first board-owned copper layer in the via span. */
    [[nodiscard]] BoardLayerId start_layer() const noexcept { return start_layer_; }

    /** Return the second board-owned copper layer in the via span. */
    [[nodiscard]] BoardLayerId end_layer() const noexcept { return end_layer_; }

    /** Return drill diameter in millimeters. */
    [[nodiscard]] double drill_diameter_mm() const noexcept { return drill_diameter_mm_; }

    /** Return outer annular copper diameter in millimeters. */
    [[nodiscard]] double annular_diameter_mm() const noexcept { return annular_diameter_mm_; }

  private:
    NetId net_;
    BoardPoint position_;
    BoardLayerId start_layer_;
    BoardLayerId end_layer_;
    double drill_diameter_mm_;
    double annular_diameter_mm_;
};

/** Kernel-owned first PCB design-rule values, expressed in board millimeters. */
class BoardDesignRules {
  public:
    /** Construct first board-level DRC rules. */
    BoardDesignRules(double copper_clearance_mm = 0.15, double minimum_track_width_mm = 0.15,
                     double minimum_via_drill_diameter_mm = 0.20,
                     double minimum_via_annular_diameter_mm = 0.45,
                     double board_outline_clearance_mm = 0.0);

    /** Return required copper-to-copper clearance between different nets. */
    [[nodiscard]] double copper_clearance_mm() const noexcept { return copper_clearance_mm_; }

    /** Return minimum allowed routed track width. */
    [[nodiscard]] double minimum_track_width_mm() const noexcept { return minimum_track_width_mm_; }

    /** Return minimum allowed via drill diameter. */
    [[nodiscard]] double minimum_via_drill_diameter_mm() const noexcept;

    /** Return minimum allowed via outer annular copper diameter. */
    [[nodiscard]] double minimum_via_annular_diameter_mm() const noexcept;

    /** Return required copper/pad setback from the board outline. */
    [[nodiscard]] double board_outline_clearance_mm() const noexcept;

  private:
    double copper_clearance_mm_;
    double minimum_track_width_mm_;
    double minimum_via_drill_diameter_mm_;
    double minimum_via_annular_diameter_mm_;
    double board_outline_clearance_mm_;
};

} // namespace volt
