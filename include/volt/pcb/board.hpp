#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** Human-facing PCB board name. */
class BoardName {
  public:
    /** Construct a non-empty board name. */
    explicit BoardName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Board name must not be empty"};
        }
    }

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
    BoardPoint(double x_mm, double y_mm) : x_mm_{x_mm}, y_mm_{y_mm} {
        if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
            throw std::invalid_argument{"Board point coordinates must be finite"};
        }
    }

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
    BoardSize(double width_mm, double height_mm) : width_mm_{width_mm}, height_mm_{height_mm} {
        if (!std::isfinite(width_mm_) || !std::isfinite(height_mm_)) {
            throw std::invalid_argument{"Board size dimensions must be finite"};
        }
        if (width_mm_ <= 0.0 || height_mm_ <= 0.0) {
            throw std::invalid_argument{"Board size dimensions must be positive"};
        }
    }

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
    explicit BoardRotation(double degrees) : degrees_{degrees} {
        if (!std::isfinite(degrees_)) {
            throw std::invalid_argument{"Board rotation must be finite"};
        }
    }

    double degrees_;
};

/** Component placement side. */
enum class BoardSide {
    Top,
    Bottom,
};

/** Broad role of a PCB board layer. */
enum class BoardLayerRole {
    Copper,
    SolderMask,
    Paste,
    Silkscreen,
    Fabrication,
    EdgeCuts,
    Drill,
    Mechanical,
    Courtyard,
    Keepout,
};

/** Physical side or scope of a board layer. */
enum class BoardLayerSide {
    Top,
    Bottom,
    Inner,
    Both,
    None,
};

/** Layer metadata owned by a board projection. */
class BoardLayer {
  public:
    /** Construct a board layer with deterministic metadata. */
    BoardLayer(std::string name, BoardLayerRole role, BoardLayerSide side,
               double thickness_mm = 0.0, bool enabled = true)
        : name_{std::move(name)}, role_{role}, side_{side}, thickness_mm_{thickness_mm},
          enabled_{enabled} {
        if (name_.empty()) {
            throw std::invalid_argument{"Board layer name must not be empty"};
        }
        if (!std::isfinite(thickness_mm_)) {
            throw std::invalid_argument{"Board layer thickness must be finite"};
        }
        if (thickness_mm_ < 0.0) {
            throw std::invalid_argument{"Board layer thickness must not be negative"};
        }
    }

    /** Return the board-local layer name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the broad layer role. */
    [[nodiscard]] BoardLayerRole role() const noexcept { return role_; }

    /** Return the physical layer side. */
    [[nodiscard]] BoardLayerSide side() const noexcept { return side_; }

    /** Return optional physical thickness metadata in millimeters. */
    [[nodiscard]] double thickness_mm() const noexcept { return thickness_mm_; }

    /** Return whether this layer participates in board output. */
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  private:
    std::string name_;
    BoardLayerRole role_;
    BoardLayerSide side_;
    double thickness_mm_;
    bool enabled_;
};

/** Ordered stack of board layers plus total board thickness metadata. */
class LayerStack {
  public:
    /** Construct a deterministic non-empty layer stack. */
    LayerStack(std::vector<BoardLayerId> layers, double board_thickness_mm)
        : layers_{std::move(layers)}, board_thickness_mm_{board_thickness_mm} {
        if (layers_.empty()) {
            throw std::invalid_argument{"Layer stack must contain at least one layer"};
        }
        if (!std::isfinite(board_thickness_mm_)) {
            throw std::invalid_argument{"Board thickness must be finite"};
        }
        if (board_thickness_mm_ <= 0.0) {
            throw std::invalid_argument{"Board thickness must be positive"};
        }

        auto sorted = layers_;
        std::sort(sorted.begin(), sorted.end(),
                  [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
        const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
        if (duplicate != sorted.end()) {
            throw std::invalid_argument{"Layer stack must not contain duplicate layers"};
        }
    }

    /** Return the ordered board layer IDs. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return total board thickness metadata in millimeters. */
    [[nodiscard]] double board_thickness_mm() const noexcept { return board_thickness_mm_; }

  private:
    std::vector<BoardLayerId> layers_;
    double board_thickness_mm_;
};

/** Closed polygonal board outline in board coordinates. */
class BoardOutline {
  public:
    /** Construct a closed outline from polygon vertices. */
    explicit BoardOutline(std::vector<BoardPoint> vertices) : vertices_{std::move(vertices)} {
        drop_duplicate_closing_vertex();
        if (vertices_.size() < 3) {
            throw std::invalid_argument{"Board outline must contain at least three vertices"};
        }
        if (std::abs(signed_area_twice(vertices_)) <= kGeometryEpsilon) {
            throw std::invalid_argument{"Board outline area must be positive"};
        }
        if (has_self_intersection(vertices_)) {
            throw std::invalid_argument{"Board outline must not self-intersect"};
        }
    }

    /** Construct a rectangular board outline from origin and positive size. */
    [[nodiscard]] static BoardOutline rectangle(BoardPoint origin, BoardSize size) {
        return BoardOutline{std::vector{
            origin,
            BoardPoint{origin.x_mm() + size.width_mm(), origin.y_mm()},
            BoardPoint{origin.x_mm() + size.width_mm(), origin.y_mm() + size.height_mm()},
            BoardPoint{origin.x_mm(), origin.y_mm() + size.height_mm()},
        }};
    }

    /** Return outline vertices in deterministic boundary order. */
    [[nodiscard]] const std::vector<BoardPoint> &vertices() const noexcept { return vertices_; }

    /** Return whether the point is inside or on the board outline. */
    [[nodiscard]] bool contains(BoardPoint point) const noexcept {
        bool inside = false;
        std::size_t previous = vertices_.size() - 1U;
        for (std::size_t current = 0; current < vertices_.size(); ++current) {
            const auto &a = vertices_[previous];
            const auto &b = vertices_[current];
            if (point_on_segment(point, a, b)) {
                return true;
            }

            const auto crosses_y = (a.y_mm() > point.y_mm()) != (b.y_mm() > point.y_mm());
            if (crosses_y) {
                const auto intersection_x =
                    ((b.x_mm() - a.x_mm()) * (point.y_mm() - a.y_mm()) / (b.y_mm() - a.y_mm())) +
                    a.x_mm();
                if (point.x_mm() < intersection_x) {
                    inside = !inside;
                }
            }

            previous = current;
        }

        return inside;
    }

  private:
    static constexpr double kGeometryEpsilon = 1.0e-9;

    void drop_duplicate_closing_vertex() {
        if (vertices_.size() > 1 && vertices_.front() == vertices_.back()) {
            vertices_.pop_back();
        }
    }

    [[nodiscard]] static double signed_area_twice(const std::vector<BoardPoint> &vertices) {
        double area = 0.0;
        std::size_t previous = vertices.size() - 1U;
        for (std::size_t current = 0; current < vertices.size(); ++current) {
            area += vertices[previous].x_mm() * vertices[current].y_mm();
            area -= vertices[current].x_mm() * vertices[previous].y_mm();
            previous = current;
        }
        return area;
    }

    [[nodiscard]] static bool point_on_segment(BoardPoint point, BoardPoint a,
                                               BoardPoint b) noexcept {
        const auto cross = ((point.y_mm() - a.y_mm()) * (b.x_mm() - a.x_mm())) -
                           ((point.x_mm() - a.x_mm()) * (b.y_mm() - a.y_mm()));
        if (std::abs(cross) > kGeometryEpsilon) {
            return false;
        }

        const auto dot = ((point.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                         ((point.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
        if (dot < -kGeometryEpsilon) {
            return false;
        }

        const auto length_squared = ((b.x_mm() - a.x_mm()) * (b.x_mm() - a.x_mm())) +
                                    ((b.y_mm() - a.y_mm()) * (b.y_mm() - a.y_mm()));
        return dot <= length_squared + kGeometryEpsilon;
    }

    [[nodiscard]] static double orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept {
        return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
               ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
    }

    [[nodiscard]] static bool segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                                 BoardPoint d) noexcept {
        const auto ab_c = orientation(a, b, c);
        const auto ab_d = orientation(a, b, d);
        const auto cd_a = orientation(c, d, a);
        const auto cd_b = orientation(c, d, b);

        if (std::abs(ab_c) <= kGeometryEpsilon && point_on_segment(c, a, b)) {
            return true;
        }
        if (std::abs(ab_d) <= kGeometryEpsilon && point_on_segment(d, a, b)) {
            return true;
        }
        if (std::abs(cd_a) <= kGeometryEpsilon && point_on_segment(a, c, d)) {
            return true;
        }
        if (std::abs(cd_b) <= kGeometryEpsilon && point_on_segment(b, c, d)) {
            return true;
        }

        return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
    }

    [[nodiscard]] static bool has_self_intersection(const std::vector<BoardPoint> &vertices) {
        for (std::size_t first = 0; first < vertices.size(); ++first) {
            const auto first_next = (first + 1U) % vertices.size();
            for (std::size_t second = first + 1U; second < vertices.size(); ++second) {
                const auto second_next = (second + 1U) % vertices.size();
                const auto adjacent = first == second || first_next == second ||
                                      second_next == first || (first == 0U && second_next == 0U);
                if (adjacent) {
                    continue;
                }

                if (segments_intersect(vertices[first], vertices[first_next], vertices[second],
                                       vertices[second_next])) {
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<BoardPoint> vertices_;
};

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
                                                    double drill_diameter_mm) {
        return BoardFeature{BoardFeatureKind::MountingHole, std::move(label), center,
                            drill_diameter_mm};
    }

    /** Return the feature kind. */
    [[nodiscard]] BoardFeatureKind kind() const noexcept { return kind_; }

    /** Return the optional human-facing feature label. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the feature center or anchor point. */
    [[nodiscard]] BoardPoint position() const noexcept { return position_; }

    /** Return the feature primary diameter in millimeters. */
    [[nodiscard]] double diameter_mm() const noexcept { return diameter_mm_; }

  private:
    BoardFeature(BoardFeatureKind kind, std::string label, BoardPoint position, double diameter_mm)
        : kind_{kind}, label_{std::move(label)}, position_{position}, diameter_mm_{diameter_mm} {
        if (!std::isfinite(diameter_mm_)) {
            throw std::invalid_argument{"Board feature diameter must be finite"};
        }
        if (diameter_mm_ <= 0.0) {
            throw std::invalid_argument{"Board feature diameter must be positive"};
        }
    }

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
                       BoardSide side = BoardSide::Top, bool locked = false)
        : component_{component}, position_{position}, rotation_{rotation}, side_{side},
          locked_{locked} {}

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
                  std::optional<NetId> net, PadResolutionStatus status)
        : placement_{placement}, component_{component}, pad_{pad}, pad_label_{std::move(pad_label)},
          position_{position}, pin_{pin}, net_{net}, status_{status} {}

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

class Board;

namespace detail {

[[nodiscard]] inline FootprintLibrary
board_resolution_footprints(const Board &board, const FootprintLibrary &footprints);

[[nodiscard]] inline BoardPoint transform_footprint_point(const ComponentPlacement &placement,
                                                          FootprintPoint point) {
    constexpr double pi = 3.14159265358979323846264338327950288;
    const auto radians = placement.rotation().degrees() * pi / 180.0;
    auto local_x = point.x_mm();
    const auto local_y = point.y_mm();
    if (placement.side() == BoardSide::Bottom) {
        local_x = -local_x;
    }

    const auto rotated_x = (std::cos(radians) * local_x) - (std::sin(radians) * local_y);
    const auto rotated_y = (std::sin(radians) * local_x) + (std::cos(radians) * local_y);
    return BoardPoint{placement.position().x_mm() + rotated_x,
                      placement.position().y_mm() + rotated_y};
}

[[nodiscard]] inline std::vector<BoardPoint>
transformed_pad_body_corners(const ComponentPlacement &placement, const FootprintPad &pad) {
    const auto half_width = pad.size().width_mm() / 2.0;
    const auto half_height = pad.size().height_mm() / 2.0;
    const auto center = pad.position();
    return std::vector{
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() - half_width, center.y_mm() - half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() + half_width, center.y_mm() - half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() + half_width, center.y_mm() + half_height}),
        transform_footprint_point(
            placement, FootprintPoint{center.x_mm() - half_width, center.y_mm() + half_height}),
    };
}

[[nodiscard]] inline double board_orientation(BoardPoint a, BoardPoint b, BoardPoint c) noexcept {
    return ((b.x_mm() - a.x_mm()) * (c.y_mm() - a.y_mm())) -
           ((b.y_mm() - a.y_mm()) * (c.x_mm() - a.x_mm()));
}

[[nodiscard]] inline bool segments_cross_properly(BoardPoint a, BoardPoint b, BoardPoint c,
                                                  BoardPoint d) noexcept {
    constexpr double geometry_epsilon = 1.0e-9;
    const auto ab_c = board_orientation(a, b, c);
    const auto ab_d = board_orientation(a, b, d);
    const auto cd_a = board_orientation(c, d, a);
    const auto cd_b = board_orientation(c, d, b);

    if (std::abs(ab_c) <= geometry_epsilon || std::abs(ab_d) <= geometry_epsilon ||
        std::abs(cd_a) <= geometry_epsilon || std::abs(cd_b) <= geometry_epsilon) {
        return false;
    }

    return ((ab_c > 0.0) != (ab_d > 0.0)) && ((cd_a > 0.0) != (cd_b > 0.0));
}

[[nodiscard]] inline BoardPoint segment_midpoint(BoardPoint a, BoardPoint b) {
    return BoardPoint{(a.x_mm() + b.x_mm()) / 2.0, (a.y_mm() + b.y_mm()) / 2.0};
}

[[nodiscard]] inline bool pad_body_exits_outline(const BoardOutline &outline,
                                                 const std::vector<BoardPoint> &pad_corners) {
    for (const auto point : pad_corners) {
        if (!outline.contains(point)) {
            return true;
        }
    }

    const auto &outline_vertices = outline.vertices();
    for (std::size_t pad_index = 0; pad_index < pad_corners.size(); ++pad_index) {
        const auto pad_next = (pad_index + 1U) % pad_corners.size();
        if (!outline.contains(segment_midpoint(pad_corners[pad_index], pad_corners[pad_next]))) {
            return true;
        }

        for (std::size_t outline_index = 0; outline_index < outline_vertices.size();
             ++outline_index) {
            const auto outline_next = (outline_index + 1U) % outline_vertices.size();
            if (segments_cross_properly(pad_corners[pad_index], pad_corners[pad_next],
                                        outline_vertices[outline_index],
                                        outline_vertices[outline_next])) {
                return true;
            }
        }
    }

    return false;
}

} // namespace detail

/** Placement-only PCB projection over a logical circuit. */
class Board {
  public:
    /** Construct a board projection over one logical circuit. */
    explicit Board(const Circuit &circuit, BoardName name = BoardName{"Main"})
        : circuit_{&circuit}, name_{std::move(name)} {}

    /** Return the board name. */
    [[nodiscard]] const BoardName &name() const noexcept { return name_; }

    /** Return the geometry units used by the board model. */
    [[nodiscard]] BoardUnits units() const noexcept { return units_; }

    /** Return the logical circuit this board projects. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return *circuit_; }

    /** Add a board layer, rejecting duplicate board-local layer names. */
    [[nodiscard]] BoardLayerId add_layer(BoardLayer layer) {
        if (layer_by_name(layer.name()).has_value()) {
            throw std::logic_error{"Board layer name already exists"};
        }

        return layers_.insert(std::move(layer));
    }

    /** Set the board layer stack, rejecting layer IDs not owned by this board. */
    void set_layer_stack(LayerStack stack) {
        for (const auto layer : stack.layers()) {
            require_layer(layer);
        }
        layer_stack_ = std::move(stack);
    }

    /** Set the board outline. */
    void set_outline(BoardOutline outline) { outline_ = std::move(outline); }

    /** Add a physical board feature. */
    [[nodiscard]] BoardFeatureId add_feature(BoardFeature feature) {
        return features_.insert(std::move(feature));
    }

    /** Cache a resolved footprint definition snapshot, rejecting duplicate footprint refs. */
    [[nodiscard]] FootprintDefId cache_footprint_definition(FootprintDefinition footprint) {
        if (footprint_definition_id(footprint.ref()).has_value()) {
            throw std::logic_error{"Board footprint definition already exists"};
        }

        return footprint_definitions_.insert(std::move(footprint));
    }

    /** Place an existing logical component once on this board. */
    [[nodiscard]] ComponentPlacementId place_component(ComponentPlacement placement) {
        static_cast<void>(circuit().component(placement.component()));
        if (placement_for_component(placement.component()).has_value()) {
            throw std::logic_error{"Component already has a board placement"};
        }

        return placements_.insert(std::move(placement));
    }

    /** Return a board layer by board-local ID. */
    [[nodiscard]] const BoardLayer &layer(BoardLayerId id) const { return layers_.get(id); }

    /** Return the number of board layers. */
    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }

    /** Return the current layer stack, if assigned. */
    [[nodiscard]] const std::optional<LayerStack> &layer_stack() const noexcept {
        return layer_stack_;
    }

    /** Return the board outline, if assigned. */
    [[nodiscard]] const std::optional<BoardOutline> &outline() const noexcept { return outline_; }

    /** Return a board feature by ID. */
    [[nodiscard]] const BoardFeature &feature(BoardFeatureId id) const { return features_.get(id); }

    /** Return the number of stored board features. */
    [[nodiscard]] std::size_t feature_count() const noexcept { return features_.size(); }

    /** Return a cached footprint definition by board-local ID. */
    [[nodiscard]] const FootprintDefinition &footprint_definition(FootprintDefId id) const {
        return footprint_definitions_.get(id);
    }

    /** Return the number of cached footprint definitions. */
    [[nodiscard]] std::size_t footprint_definition_count() const noexcept {
        return footprint_definitions_.size();
    }

    /** Return a cached footprint definition ID for a library-qualified reference, if present. */
    [[nodiscard]] std::optional<FootprintDefId>
    footprint_definition_id(const FootprintRef &ref) const noexcept {
        for (std::size_t index = 0; index < footprint_definitions_.size(); ++index) {
            const auto id = FootprintDefId{index};
            if (footprint_definitions_.get(id).ref() == ref) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return a placement by board-local ID. */
    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const {
        return placements_.get(id);
    }

    /** Return the number of component placements. */
    [[nodiscard]] std::size_t placement_count() const noexcept { return placements_.size(); }

    /** Return the placement ID for a component, if present. */
    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept {
        for (std::size_t index = 0; index < placements_.size(); ++index) {
            const auto id = ComponentPlacementId{index};
            if (placements_.get(id).component() == component) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Derive pad-to-pin/net resolution for all placed components. */
    [[nodiscard]] std::vector<PadResolution>
    resolve_pads(const FootprintLibrary &footprints) const {
        auto resolutions = std::vector<PadResolution>{};
        const auto resolution_footprints = detail::board_resolution_footprints(*this, footprints);
        for (std::size_t index = 0; index < placements_.size(); ++index) {
            const auto placement_id = ComponentPlacementId{index};
            const auto &component_placement = placement(placement_id);
            const auto &selected_part =
                circuit().selected_physical_part(component_placement.component());
            if (!selected_part.has_value()) {
                continue;
            }

            const auto footprint_resolution =
                resolve_footprint(selected_part.value(), resolution_footprints);
            const auto *definition = footprint_resolution.definition();
            if (definition == nullptr) {
                continue;
            }

            append_pad_resolutions(placement_id, component_placement, *definition,
                                   footprint_resolution.pad_bindings(), resolutions);
        }

        return resolutions;
    }

  private:
    void require_layer(BoardLayerId layer) const {
        if (!layers_.contains(layer)) {
            throw std::out_of_range{"Board layer ID does not belong to this board"};
        }
    }

    [[nodiscard]] std::optional<BoardLayerId> layer_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            const auto id = BoardLayerId{index};
            if (layers_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    void append_pad_resolutions(ComponentPlacementId placement_id,
                                const ComponentPlacement &component_placement,
                                const FootprintDefinition &definition,
                                const std::vector<FootprintPadBinding> &bindings,
                                std::vector<PadResolution> &resolutions) const {
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto &pad = definition.pad(pad_id);
            const auto position =
                detail::transform_footprint_point(component_placement, pad.position());
            if (!pad.requires_pin_mapping()) {
                resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                         pad.label(), position, std::nullopt, std::nullopt,
                                         PadResolutionStatus::NonElectrical);
                continue;
            }

            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                                              [pad_id](const FootprintPadBinding &candidate) {
                                                  return candidate.pad() == pad_id;
                                              });
            if (binding == bindings.end()) {
                resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                         pad.label(), position, std::nullopt, std::nullopt,
                                         PadResolutionStatus::Invalid);
                continue;
            }

            const auto pin =
                circuit().pin_by_definition(component_placement.component(), binding->pin());
            if (!pin.has_value()) {
                resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                         pad.label(), position, std::nullopt, std::nullopt,
                                         PadResolutionStatus::Invalid);
                continue;
            }

            const auto net = circuit().net_of(pin.value());
            const auto status =
                net.has_value() ? PadResolutionStatus::Connected : PadResolutionStatus::Unconnected;
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, pin, net, status);
        }
    }

    const Circuit *circuit_;
    BoardName name_;
    BoardUnits units_{BoardUnits::Millimeters};
    EntityTable<BoardLayer, BoardLayerId> layers_;
    std::optional<LayerStack> layer_stack_;
    std::optional<BoardOutline> outline_;
    EntityTable<BoardFeature, BoardFeatureId> features_;
    EntityTable<FootprintDefinition, FootprintDefId> footprint_definitions_;
    EntityTable<ComponentPlacement, ComponentPlacementId> placements_;
};

namespace detail {

[[nodiscard]] inline FootprintLibrary
board_resolution_footprints(const Board &board, const FootprintLibrary &footprints) {
    auto library = FootprintLibrary{};
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        library.add(board.footprint_definition(FootprintDefId{index}));
    }
    for (const auto &definition : footprints.definitions()) {
        if (library.find(definition.ref()) == nullptr) {
            library.add(definition);
        }
    }
    return library;
}

[[nodiscard]] inline Diagnostic board_diagnostic(DiagnosticCode code, std::string message,
                                                 std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, std::move(code), std::move(message), std::move(entities)};
}

[[nodiscard]] inline Diagnostic board_component_diagnostic(DiagnosticCode code, std::string message,
                                                           ComponentId component) {
    return board_diagnostic(std::move(code), std::move(message),
                            std::vector{EntityRef::component(component)});
}

[[nodiscard]] inline Diagnostic board_placement_diagnostic(DiagnosticCode code, std::string message,
                                                           ComponentId component,
                                                           ComponentPlacementId placement) {
    return board_diagnostic(
        std::move(code), std::move(message),
        std::vector{EntityRef::component(component), EntityRef::component_placement(placement)});
}

} // namespace detail

/** Validate placement-only board design issues against circuit and footprint context. */
[[nodiscard]] inline DiagnosticReport validate_board(const Board &board,
                                                     const FootprintLibrary &footprints) {
    auto report = DiagnosticReport{};
    const auto resolution_footprints = detail::board_resolution_footprints(board, footprints);

    if (!board.outline().has_value()) {
        report.add(detail::board_diagnostic(DiagnosticCode{"PCB_BOARD_OUTLINE_MISSING"},
                                            "Board has no outline"));
    }

    for (std::size_t index = 0; index < board.circuit().component_count(); ++index) {
        const auto component = ComponentId{index};
        if (!board.placement_for_component(component).has_value()) {
            report.add(
                detail::board_component_diagnostic(DiagnosticCode{"PCB_COMPONENT_NOT_PLACED"},
                                                   "Component has no board placement", component));
        }

        if (!board.circuit().selected_physical_part(component).has_value()) {
            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_COMPONENT_MISSING_SELECTED_PART"},
                "Component requires a selected physical part for board placement", component));
        }
    }

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        for (const auto &diagnostic : footprint_resolution.diagnostics().diagnostics()) {
            report.add(Diagnostic{diagnostic.severity(), diagnostic.code(), diagnostic.message(),
                                  std::vector{EntityRef::component(placement.component()),
                                              EntityRef::component_placement(placement_id)}});
        }

        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (const auto &binding : footprint_resolution.pad_bindings()) {
            if (board.circuit()
                    .pin_by_definition(placement.component(), binding.pin())
                    .has_value()) {
                continue;
            }

            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_PIN_PAD_MISMATCH"},
                "Selected part pin-pad mapping does not resolve to a concrete component pin",
                placement.component()));
        }

        if (!board.outline().has_value()) {
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto &pad = definition->pad(FootprintPadId{pad_index});
            const auto pad_corners = detail::transformed_pad_body_corners(placement, pad);
            if (detail::pad_body_exits_outline(board.outline().value(), pad_corners)) {
                report.add(detail::board_placement_diagnostic(
                    DiagnosticCode{"PCB_PLACEMENT_OUTSIDE_OUTLINE"},
                    "Placement pad '" + pad.label() + "' is outside the board outline",
                    placement.component(), placement_id));
            }
        }
    }

    return report;
}

} // namespace volt
