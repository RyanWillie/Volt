#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_outline.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** Physical board feature kind for placement-only PCB models. */
enum class BoardFeatureKind {
    MountingHole,
    Slot,
    Cutout,
    Fiducial,
    ToolingHole,
    Text,
    MechanicalKeepout,
};

/** Board-owned physical feature that does not define electrical connectivity. */
class BoardFeature {
  public:
    /** Construct a circular mounting hole feature. */
    [[nodiscard]] static BoardFeature mounting_hole(std::string label, BoardPoint center,
                                                    double drill_diameter_mm);

    /** Return the feature kind. */
    [[nodiscard]] BoardFeatureKind kind() const noexcept { return kind_; }

    /** Return the optional human-facing feature label. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the feature center or anchor point. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the feature primary diameter in millimeters. */
    [[nodiscard]] double diameter_mm() const noexcept { return diameter_mm_; }

  private:
    BoardFeature(BoardFeatureKind kind, std::string label, BoardPoint position, double diameter_mm);

    BoardFeatureKind kind_;
    std::string label_;
    BoardPoint position_;
    double diameter_mm_;
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
