#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_outline.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** Physical board feature kind for placement-only PCB models. */
enum class BoardFeatureKind {
    Hole,
    MountingHole,
    Slot,
    Cutout,
    Fiducial,
    ToolingHole,
    Text,
    MechanicalKeepout,
};

/** Object kinds restricted by a board keepout. */
enum class BoardKeepoutRestriction {
    Copper,
    Via,
    Placement,
    All,
};

/** Generic drilled board hole primitive. */
class BoardHole {
  public:
    /** Construct a board hole with drill metadata. */
    BoardHole(BoardPoint center, double drill_diameter_mm, bool plated,
              std::optional<double> finished_diameter_mm = std::nullopt);

    /** Return the hole center. */
    [[nodiscard]] BoardPoint center() const noexcept { return center_; }

    /** Return drill diameter in millimeters. */
    [[nodiscard]] double drill_diameter_mm() const noexcept { return drill_diameter_mm_; }

    /** Return finished diameter in millimeters, when distinct from drill diameter. */
    [[nodiscard]] std::optional<double> finished_diameter_mm() const noexcept {
        return finished_diameter_mm_;
    }

    /** Return whether the hole is plated. */
    [[nodiscard]] bool plated() const noexcept { return plated_; }

  private:
    BoardPoint center_;
    double drill_diameter_mm_;
    bool plated_;
    std::optional<double> finished_diameter_mm_;
};

/** Generic slotted board hole primitive. */
class BoardSlot {
  public:
    /** Construct a slot from a centerline and width. */
    BoardSlot(BoardPoint start, BoardPoint end, double width_mm, bool plated);

    /** Return the slot centerline start. */
    [[nodiscard]] BoardPoint start() const noexcept { return start_; }

    /** Return the slot centerline end. */
    [[nodiscard]] BoardPoint end() const noexcept { return end_; }

    /** Return the slot width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

    /** Return whether the slot is plated. */
    [[nodiscard]] bool plated() const noexcept { return plated_; }

  private:
    BoardPoint start_;
    BoardPoint end_;
    double width_mm_;
    bool plated_;
};

/** Generic board cutout primitive. */
class BoardCutout {
  public:
    /** Construct a cutout from a closed polygon. */
    explicit BoardCutout(std::vector<BoardPoint> outline);

    /** Return cutout polygon vertices. */
    [[nodiscard]] const std::vector<BoardPoint> &outline() const noexcept;

  private:
    BoardPolygon outline_;
};

/** Generic board fiducial primitive. */
class BoardFiducial {
  public:
    /** Construct a circular fiducial marker. */
    BoardFiducial(BoardPoint center, double diameter_mm, BoardSide side = BoardSide::Top);

    /** Return the fiducial center. */
    [[nodiscard]] BoardPoint center() const noexcept { return center_; }

    /** Return fiducial diameter in millimeters. */
    [[nodiscard]] double diameter_mm() const noexcept { return diameter_mm_; }

    /** Return the board side carrying the fiducial. */
    [[nodiscard]] BoardSide side() const noexcept { return side_; }

  private:
    BoardPoint center_;
    double diameter_mm_;
    BoardSide side_;
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

/** Board-owned physical feature that does not define electrical connectivity. */
class BoardFeature {
  public:
    /** Payload held by one board feature. */
    using Payload =
        std::variant<BoardHole, BoardSlot, BoardCutout, BoardFiducial, BoardText, BoardKeepout>;

    /** Construct a circular board hole feature. */
    [[nodiscard]] static BoardFeature
    hole(std::string label, BoardPoint center, double drill_diameter_mm, bool plated = false,
         std::string role = {}, std::optional<double> finished_diameter_mm = std::nullopt);

    /** Construct a circular mounting hole feature. */
    [[nodiscard]] static BoardFeature mounting_hole(std::string label, BoardPoint center,
                                                    double drill_diameter_mm);

    /** Construct a circular tooling hole feature. */
    [[nodiscard]] static BoardFeature
    tooling_hole(std::string label, BoardPoint center, double drill_diameter_mm,
                 std::optional<double> finished_diameter_mm = std::nullopt);

    /** Construct a slotted board feature. */
    [[nodiscard]] static BoardFeature slot(std::string label, BoardPoint start, BoardPoint end,
                                           double width_mm, bool plated = false,
                                           std::string role = {});

    /** Construct a polygonal board cutout feature. */
    [[nodiscard]] static BoardFeature cutout(std::string label, std::vector<BoardPoint> outline,
                                             std::string role = {});

    /** Construct a fiducial feature. */
    [[nodiscard]] static BoardFeature fiducial(std::string label, BoardPoint center,
                                               double diameter_mm, BoardSide side = BoardSide::Top);

    /** Construct a board text feature. */
    [[nodiscard]] static BoardFeature text(BoardText text);

    /** Construct a mechanical keepout feature. */
    [[nodiscard]] static BoardFeature mechanical_keepout(BoardKeepout keepout);

    /** Return the feature kind. */
    [[nodiscard]] BoardFeatureKind kind() const noexcept { return kind_; }

    /** Return the optional human-facing feature label. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the optional mechanical/electrical role metadata. */
    [[nodiscard]] const std::string &role() const noexcept { return role_; }

    /** Return the feature center or anchor point. */
    [[nodiscard]] BoardPoint position() const;

    /** Return the feature primary diameter in millimeters. */
    [[nodiscard]] double diameter_mm() const;

    /** Return hole payload for hole-like features. */
    [[nodiscard]] const BoardHole &hole() const;

    /** Return slot payload. */
    [[nodiscard]] const BoardSlot &slot() const;

    /** Return cutout payload. */
    [[nodiscard]] const BoardCutout &cutout() const;

    /** Return fiducial payload. */
    [[nodiscard]] const BoardFiducial &fiducial() const;

    /** Return text payload. */
    [[nodiscard]] const BoardText &text() const;

    /** Return keepout payload. */
    [[nodiscard]] const BoardKeepout &keepout() const;

    /** Return the underlying typed payload. */
    [[nodiscard]] const Payload &payload() const noexcept { return payload_; }

  private:
    BoardFeature(BoardFeatureKind kind, std::string label, std::string role, Payload payload);

    BoardFeatureKind kind_;
    std::string label_;
    std::string role_;
    Payload payload_;
};

/** Stored placement of an existing logical component on a board. */
class ComponentPlacement {
  public:
    /** Construct deterministic placement data for an existing component. */
    ComponentPlacement(ComponentId component, BoardPoint position, BoardRotation rotation,
                       BoardSide side = BoardSide::Top, bool locked = false);

    /** Return the placed logical component ID. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the board-space placement origin in millimeters. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the placement rotation. */
    [[nodiscard]] BoardRotation rotation() const noexcept { return rotation_; }

    /** Return the board side. */
    [[nodiscard]] BoardSide side() const noexcept { return side_; }

    /** Return whether placement is locked against automatic movement. */
    [[nodiscard]] bool locked() const noexcept { return locked_; }

  private:
    ComponentId component_;
    BoardPoint position_;
    BoardRotation rotation_;
    BoardSide side_;
    bool locked_;
};

/** Status for a derived placement pad resolution. */
enum class PadResolutionStatus {
    Connected,
    Unconnected,
    NonElectrical,
    Invalid,
};

/** Derived view linking a placed footprint pad to logical pin and net identity. */
class PadResolution {
  public:
    /** Construct a pad resolution value. */
    PadResolution(ComponentPlacementId placement, ComponentId component, FootprintPadId pad,
                  std::string pad_label, BoardPoint position, std::optional<PinId> pin,
                  std::optional<NetId> net, PadResolutionStatus status);

    /** Return the placement that owns this pad. */
    [[nodiscard]] ComponentPlacementId placement() const noexcept { return placement_; }

    /** Return the logical component for this placement. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the footprint-local pad ID. */
    [[nodiscard]] FootprintPadId pad() const noexcept { return pad_; }

    /** Return the footprint-local pad label. */
    [[nodiscard]] const std::string &pad_label() const noexcept { return pad_label_; }

    /** Return the resolved board-space pad center. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the concrete logical pin, if this pad resolves to one. */
    [[nodiscard]] std::optional<PinId> pin() const noexcept { return pin_; }

    /** Return the existing logical net, if the resolved pin is connected. */
    [[nodiscard]] std::optional<NetId> net() const noexcept { return net_; }

    /** Return resolution status. */
    [[nodiscard]] PadResolutionStatus status() const noexcept { return status_; }

  private:
    ComponentPlacementId placement_;
    ComponentId component_;
    FootprintPadId pad_;
    std::string pad_label_;
    BoardPoint position_;
    std::optional<PinId> pin_;
    std::optional<NetId> net_;
    PadResolutionStatus status_;
};

/** Stable endpoint of a derived ratsnest edge. */
class RatsnestEndpoint {
  public:
    /** Construct an endpoint over one resolved board pad. */
    RatsnestEndpoint(ComponentPlacementId placement, ComponentId component, FootprintPadId pad,
                     BoardPoint position);

    /** Return the placement containing this pad. */
    [[nodiscard]] ComponentPlacementId placement() const noexcept { return placement_; }

    /** Return the logical component containing this pad. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the footprint-local pad ID. */
    [[nodiscard]] FootprintPadId pad() const noexcept { return pad_; }

    /** Return the board-space pad center. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

  private:
    ComponentPlacementId placement_;
    ComponentId component_;
    FootprintPadId pad_;
    BoardPoint position_;
};

/** Derived unrouted connection preview between two placed pads on one logical net. */
class RatsnestEdge {
  public:
    /** Construct a derived ratsnest edge. */
    RatsnestEdge(NetId net, RatsnestEndpoint from, RatsnestEndpoint to);

    /** Return the logical net this preview edge belongs to. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the stable lower endpoint for this edge. */
    [[nodiscard]] const RatsnestEndpoint &from() const noexcept { return from_; }

    /** Return the stable upper endpoint for this edge. */
    [[nodiscard]] const RatsnestEndpoint &to() const noexcept { return to_; }

  private:
    NetId net_;
    RatsnestEndpoint from_;
    RatsnestEndpoint to_;
};

class Board;

namespace detail {

[[nodiscard]] inline FootprintLibrary
board_resolution_footprints(const Board &board, const FootprintLibrary &footprints);

[[nodiscard]] bool ratsnest_endpoint_less(const RatsnestEndpoint &lhs,
                                          const RatsnestEndpoint &rhs) noexcept;

[[nodiscard]] double ratsnest_distance_squared(const RatsnestEndpoint &lhs,
                                               const RatsnestEndpoint &rhs) noexcept;

[[nodiscard]] RatsnestEdge make_ratsnest_edge(NetId net, RatsnestEndpoint lhs,
                                              RatsnestEndpoint rhs);

[[nodiscard]] std::size_t ratsnest_root(std::vector<std::size_t> &parents, std::size_t index);

[[nodiscard]] bool same_ratsnest_endpoint(const RatsnestEndpoint &lhs,
                                          const RatsnestEndpoint &rhs) noexcept;

} // namespace detail

/** Derive deterministic unrouted ratsnest edges from resolved placed pads. */
[[nodiscard]] std::vector<RatsnestEdge>
derive_ratsnest_edges(const std::vector<PadResolution> &resolutions);

namespace detail {

[[nodiscard]] BoardPoint transform_footprint_point(const ComponentPlacement &placement,
                                                   FootprintPoint point);

[[nodiscard]] std::vector<BoardPoint>
transformed_pad_body_corners(const ComponentPlacement &placement, const FootprintPad &pad);

[[nodiscard]] double board_orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept;

[[nodiscard]] bool segments_cross_properly(BoardPoint a, BoardPoint b, BoardPoint c,
                                           BoardPoint d) noexcept;

[[nodiscard]] BoardPoint segment_midpoint(BoardPoint a, BoardPoint b);

[[nodiscard]] bool pad_body_exits_outline(const BoardOutline &outline,
                                          const std::vector<BoardPoint> &pad_corners);

} // namespace detail

} // namespace volt
