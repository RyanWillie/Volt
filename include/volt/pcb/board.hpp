#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>
#include <volt/pcb/geometry/board_outline.hpp>
#include <volt/pcb/layers/board_layers.hpp>
#include <volt/pcb/placement/board_placement_updates.hpp>

namespace volt {

class Board;

/// @cond
namespace detail {

enum class BoardEntityRangeState {
    Current,
    AdvancedOnce,
    Stale,
};

using BoardEntityRangeStateCheck =
    BoardEntityRangeState (*)(const void *context, std::size_t expected_generation) noexcept;

template <typename Id> struct BoardEntityDescriptor;
template <typename Id> class BoardEntityRange;

template <> struct BoardEntityDescriptor<BoardLayerId> {
    using type = BoardLayer;
};

template <> struct BoardEntityDescriptor<BoardFeatureId> {
    using type = BoardFeature;
};

template <> struct BoardEntityDescriptor<FootprintDefId> {
    using type = FootprintDefinition;
};

template <> struct BoardEntityDescriptor<ComponentPlacementId> {
    using type = ComponentPlacement;
};

template <> struct BoardEntityDescriptor<BoardTrackId> {
    using type = BoardTrack;
};

template <> struct BoardEntityDescriptor<BoardViaId> {
    using type = BoardVia;
};

template <> struct BoardEntityDescriptor<BoardZoneId> {
    using type = BoardZone;
};

template <> struct BoardEntityDescriptor<BoardKeepoutId> {
    using type = BoardKeepout;
};

template <> struct BoardEntityDescriptor<BoardRoomId> {
    using type = BoardRoom;
};

template <> struct BoardEntityDescriptor<BoardTextId> {
    using type = BoardText;
};

} // namespace detail

/// @endcond

/** True when an ID names one of Board's canonical physical entity tables. */
template <typename Id>
concept BoardEntityId =
    std::same_as<Id, BoardLayerId> || std::same_as<Id, BoardFeatureId> ||
    std::same_as<Id, FootprintDefId> || std::same_as<Id, ComponentPlacementId> ||
    std::same_as<Id, BoardTrackId> || std::same_as<Id, BoardViaId> ||
    std::same_as<Id, BoardZoneId> || std::same_as<Id, BoardKeepoutId> ||
    std::same_as<Id, BoardRoomId> || std::same_as<Id, BoardTextId>;

/** Canonical physical entity type selected by a Board-owned stable ID. */
template <BoardEntityId Id>
using board_entity_type_t = typename detail::BoardEntityDescriptor<Id>::type;

/** Borrowed deterministic range selected by a Board-owned stable ID. */
template <BoardEntityId Id> using board_entity_range_t = detail::BoardEntityRange<Id>;

/**
 * PCB board projection over a logical circuit, and aggregate root of the board model.
 *
 * Responsibility: composes the structure, footprint, placement, and copper subsystems into a
 *   board view; resolves footprint pads back to logical pins/nets.
 * Invariants: implements existing connectivity only — references existing NetIds and never
 *   creates, merges, splits, or renames nets; structural violations throw, while design issues
 *   (missing footprints/placements, unresolved pads, DRC) are reported as diagnostics.
 * Collaborators: read-only consumer of Circuit (holds const Circuit&); composes the Board*Model
 *   subsystems; DRC runs as a RuleSet<Board>. See
 *   docs/superpowers/specs/2026-06-02-volt-kernel-architecture-design.md.
 */
class Board {
  public:
    /** Construct a board projection over one logical circuit. */
    explicit Board(const Circuit &circuit, BoardName name = BoardName{"Main"});

    /** Reject temporary circuit bindings because Board stores a caller-owned circuit pointer. */
    Board(const Circuit &&circuit, BoardName name = BoardName{"Main"}) = delete;

    /** Return the board name. */
    [[nodiscard]] const BoardName &name() const noexcept { return name_; }

    /** Return the geometry units used by the board model. */
    [[nodiscard]] BoardUnits units() const noexcept { return units_; }

    /** Return the logical circuit this board projects. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return *circuit_; }

    /** Add a board layer, rejecting duplicate board-local layer names. */
    [[nodiscard]] BoardLayerId add_layer(BoardLayer layer);

    /** Set the board layer stack, rejecting layer IDs not owned by this board. */
    void set_layer_stack(LayerStack stack);

    /** Set the board outline. */
    void set_outline(BoardOutline outline);

    /** Set board-owned design rules used by DRC validation. */
    void set_design_rules(BoardDesignRules rules);

    /** Store the board capability profile snapshot used by manufacturability lint. */
    void set_capability_profile(BoardCapabilityProfile profile);

    /** Add a physical board feature. */
    [[nodiscard]] BoardFeatureId add_feature(BoardFeature feature);

    /** Cache a resolved footprint definition snapshot, deduping identical footprint refs. */
    [[nodiscard]] FootprintDefId cache_footprint_definition(FootprintDefinition footprint);

    /** Place an existing logical component once on this board. */
    [[nodiscard]] ComponentPlacementId place_component(ComponentPlacement placement);

    /** Add a routed copper track over an existing logical net and board copper layer. */
    [[nodiscard]] BoardTrackId add_track(BoardTrack track);

    /** Add a routed copper via over an existing logical net and board copper layer span. */
    [[nodiscard]] BoardViaId add_via(BoardVia via);

    /** Add a copper zone over existing board copper layers and optional existing net. */
    [[nodiscard]] BoardZoneId add_zone(BoardZone zone);

    /** Add a board keepout over existing board layers. */
    [[nodiscard]] BoardKeepoutId add_keepout(BoardKeepout keepout);

    /** Add a board room over existing board layers. */
    [[nodiscard]] BoardRoomId add_room(BoardRoom room);

    /** Add board text on an existing board layer. */
    [[nodiscard]] BoardTextId add_text(BoardText text);

    /** Move one existing component placement without changing its logical identity. */
    void move(BoardPlacementMove change);

    /** Return the current layer stack, if assigned. */
    [[nodiscard]] const std::optional<LayerStack> &layer_stack() const noexcept {
        return structure_.layer_stack;
    }

    /** Return the board outline, if assigned. */
    [[nodiscard]] const std::optional<BoardOutline> &outline() const noexcept {
        return structure_.outline;
    }

    /** Return board-owned design rules used by DRC validation. */
    [[nodiscard]] const BoardDesignRules &design_rules() const noexcept {
        return structure_.design_rules;
    }

    /** Return the optional capability profile snapshot set on this board. */
    [[nodiscard]] const std::optional<BoardCapabilityProfile> &capability_profile() const noexcept {
        return structure_.capability_profile;
    }

    /** Return a canonical physical entity selected by its strongly typed stable ID. */
    template <BoardEntityId Id> [[nodiscard]] const board_entity_type_t<Id> &get(Id id) const;

    /** Return a borrowed deterministic range over one canonical physical entity family. */
    template <BoardEntityId Id> [[nodiscard]] board_entity_range_t<Id> all() const &;
    template <BoardEntityId Id> [[nodiscard]] board_entity_range_t<Id> all() const && = delete;

  private:
    struct StructureState {
        EntityTable<BoardLayer, BoardLayerId> layers;
        std::optional<LayerStack> layer_stack;
        std::optional<BoardOutline> outline;
        BoardDesignRules design_rules;
        std::optional<BoardCapabilityProfile> capability_profile;
        EntityTable<BoardFeature, BoardFeatureId> features;
    };

    struct FootprintState {
        EntityTable<FootprintDefinition, FootprintDefId> definitions;
    };

    struct PlacementState {
        EntityTable<ComponentPlacement, ComponentPlacementId> placements;
    };

    struct CopperState {
        EntityTable<BoardTrack, BoardTrackId> tracks;
        EntityTable<BoardVia, BoardViaId> vias;
        EntityTable<BoardZone, BoardZoneId> zones;
        EntityTable<BoardKeepout, BoardKeepoutId> keepouts;
        EntityTable<BoardRoom, BoardRoomId> rooms;
        EntityTable<BoardText, BoardTextId> texts;
    };

    void require_layer(BoardLayerId layer) const;

    void require_net(NetId net) const;

    void require_copper_layer(BoardLayerId layer_id) const;

    template <BoardEntityId Id> [[nodiscard]] std::size_t entity_count() const noexcept;

    [[nodiscard]] static detail::BoardEntityRangeState
    entity_range_state(const void *context, std::size_t expected_generation) noexcept {
        const auto current_generation = static_cast<const Board *>(context)->geometry_generation_;
        if (current_generation == expected_generation) {
            return detail::BoardEntityRangeState::Current;
        }
        if (current_generation == expected_generation + 1U) {
            return detail::BoardEntityRangeState::AdvancedOnce;
        }
        return detail::BoardEntityRangeState::Stale;
    }

    const Circuit *circuit_;
    BoardName name_;
    BoardUnits units_{BoardUnits::Millimeters};
    std::size_t geometry_generation_ = 0;
    StructureState structure_;
    FootprintState footprint_cache_;
    PlacementState placements_;
    CopperState copper_;
};

/**
 * Non-owning forward range over one Board physical entity family.
 *
 * Iterators keep a pointer to the Board, so destroying or structurally mutating the Board
 * invalidates the range and its iterators. Creating a range from a temporary Board is deleted.
 */
/// @cond
namespace detail {

template <typename Id> class BoardEntityRange {
  public:
    class iterator {
      public:
        using value_type = board_entity_type_t<Id>;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type &;
        using pointer = const value_type *;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;

        [[nodiscard]] reference operator*() const { return board_->get(Id{index_}); }

        [[nodiscard]] pointer operator->() const { return &**this; }

        iterator &operator++() {
            ++index_;
            return *this;
        }

        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }

        bool operator==(const iterator &) const = default;

        iterator(const Board &board, std::size_t index) noexcept : board_{&board}, index_{index} {}

        iterator(const Board &&, std::size_t) = delete;

      private:
        const Board *board_ = nullptr;
        std::size_t index_ = 0;
    };

    [[nodiscard]] iterator begin() const noexcept { return iterator{*board_, 0}; }

    [[nodiscard]] iterator end() const noexcept { return iterator{*board_, size_}; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] bool is_current() const noexcept {
        return state_check_(state_context_, expected_generation_) == BoardEntityRangeState::Current;
    }

    [[nodiscard]] bool advanced_once() const noexcept {
        return state_check_(state_context_, expected_generation_) ==
               BoardEntityRangeState::AdvancedOnce;
    }

    BoardEntityRange(const Board &board, std::size_t size, const void *state_context,
                     std::size_t expected_generation,
                     BoardEntityRangeStateCheck state_check) noexcept
        : board_{&board}, size_{size}, state_context_{state_context},
          expected_generation_{expected_generation}, state_check_{state_check} {}

  private:
    const Board *board_;
    std::size_t size_ = 0;
    const void *state_context_;
    std::size_t expected_generation_ = 0;
    BoardEntityRangeStateCheck state_check_;
};

} // namespace detail

/// @endcond

template <BoardEntityId Id> [[nodiscard]] const board_entity_type_t<Id> &Board::get(Id id) const {
    if constexpr (std::same_as<Id, BoardLayerId>) {
        return structure_.layers.get(id);
    } else if constexpr (std::same_as<Id, BoardFeatureId>) {
        return structure_.features.get(id);
    } else if constexpr (std::same_as<Id, FootprintDefId>) {
        return footprint_cache_.definitions.get(id);
    } else if constexpr (std::same_as<Id, ComponentPlacementId>) {
        return placements_.placements.get(id);
    } else if constexpr (std::same_as<Id, BoardTrackId>) {
        return copper_.tracks.get(id);
    } else if constexpr (std::same_as<Id, BoardViaId>) {
        return copper_.vias.get(id);
    } else if constexpr (std::same_as<Id, BoardZoneId>) {
        return copper_.zones.get(id);
    } else if constexpr (std::same_as<Id, BoardKeepoutId>) {
        return copper_.keepouts.get(id);
    } else if constexpr (std::same_as<Id, BoardRoomId>) {
        return copper_.rooms.get(id);
    } else {
        return copper_.texts.get(id);
    }
}

template <BoardEntityId Id> [[nodiscard]] std::size_t Board::entity_count() const noexcept {
    std::size_t size = 0;
    if constexpr (std::same_as<Id, BoardLayerId>) {
        size = structure_.layers.size();
    } else if constexpr (std::same_as<Id, BoardFeatureId>) {
        size = structure_.features.size();
    } else if constexpr (std::same_as<Id, FootprintDefId>) {
        size = footprint_cache_.definitions.size();
    } else if constexpr (std::same_as<Id, ComponentPlacementId>) {
        size = placements_.placements.size();
    } else if constexpr (std::same_as<Id, BoardTrackId>) {
        size = copper_.tracks.size();
    } else if constexpr (std::same_as<Id, BoardViaId>) {
        size = copper_.vias.size();
    } else if constexpr (std::same_as<Id, BoardZoneId>) {
        size = copper_.zones.size();
    } else if constexpr (std::same_as<Id, BoardKeepoutId>) {
        size = copper_.keepouts.size();
    } else if constexpr (std::same_as<Id, BoardRoomId>) {
        size = copper_.rooms.size();
    } else {
        size = copper_.texts.size();
    }
    return size;
}

template <BoardEntityId Id> [[nodiscard]] board_entity_range_t<Id> Board::all() const & {
    return board_entity_range_t<Id>{*this, entity_count<Id>(), this, geometry_generation_,
                                    &Board::entity_range_state};
}

namespace detail {

[[nodiscard]] Diagnostic board_diagnostic(DiagnosticCode code, std::string message,
                                          std::vector<EntityRef> entities = {});

[[nodiscard]] Diagnostic board_warning(DiagnosticCode code, std::string message,
                                       std::vector<EntityRef> entities = {});

[[nodiscard]] Diagnostic board_component_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component);

[[nodiscard]] Diagnostic board_placement_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component,
                                                    ComponentPlacementId placement);

inline constexpr double board_drc_epsilon = 1.0e-9;

[[nodiscard]] inline double square(double value) noexcept { return value * value; }

[[nodiscard]] double board_distance(BoardPoint lhs, BoardPoint rhs) noexcept;

[[nodiscard]] double point_segment_distance(BoardPoint point, BoardPoint a, BoardPoint b) noexcept;

[[nodiscard]] bool drc_point_on_segment(BoardPoint point, BoardPoint a, BoardPoint b) noexcept;

[[nodiscard]] bool drc_segments_intersect(BoardPoint a, BoardPoint b, BoardPoint c,
                                          BoardPoint d) noexcept;

[[nodiscard]] double segment_segment_distance(BoardPoint a, BoardPoint b, BoardPoint c,
                                              BoardPoint d) noexcept;

[[nodiscard]] bool polygon_contains_point(const std::vector<BoardPoint> &polygon, BoardPoint point);

[[nodiscard]] double point_polygon_distance(BoardPoint point,
                                            const std::vector<BoardPoint> &polygon);

[[nodiscard]] double segment_polygon_distance(BoardPoint a, BoardPoint b,
                                              const std::vector<BoardPoint> &polygon);

[[nodiscard]] double polygon_polygon_distance(const std::vector<BoardPoint> &lhs,
                                              const std::vector<BoardPoint> &rhs);

[[nodiscard]] double outline_boundary_distance(const BoardOutline &outline, BoardPoint point);

[[nodiscard]] double segment_outline_boundary_distance(const BoardOutline &outline, BoardPoint a,
                                                       BoardPoint b);

[[nodiscard]] double polygon_outline_boundary_distance(const BoardOutline &outline,
                                                       const std::vector<BoardPoint> &polygon);

[[nodiscard]] bool outline_contains_disc(const BoardOutline &outline, BoardPoint center,
                                         double radius_mm, double clearance_mm);

[[nodiscard]] bool outline_contains_segment(const BoardOutline &outline, BoardPoint start,
                                            BoardPoint end, double radius_mm, double clearance_mm);

[[nodiscard]] bool outline_contains_polygon(const BoardOutline &outline,
                                            const std::vector<BoardPoint> &polygon,
                                            double clearance_mm);

/** Geometric primitive category used while checking copper spacing. */
enum class BoardCopperShapeKind {
    /** Circular copper shape. */
    Disc,
    /** Line-segment copper shape with radius. */
    Segment,
    /** Polygon copper shape. */
    Polygon,
};

/** Identifies the placed footprint pad that contributed a copper shape. */
struct BoardPadShapeKey {
    /** Placement containing the footprint pad. */
    ComponentPlacementId placement;
    /** Footprint pad within the placed component. */
    FootprintPadId pad;
};

/** Normalized copper geometry used by board DRC without owning board state. */
struct BoardCopperShape {
    /** Shape geometry category. */
    BoardCopperShapeKind kind;
    /** Logical net owning the copper shape. */
    NetId net;
    /** Copper layers occupied by the shape. */
    std::vector<BoardLayerId> layers;
    /** Entities reported when this shape participates in a diagnostic. */
    std::vector<EntityRef> primary_entities;
    /** Shape points: center, segment endpoints, or polygon vertices. */
    std::vector<BoardPoint> points;
    /** Radius used for circular and segment shapes. */
    double radius_mm;
    /** Source pad, when this shape came from a placed footprint pad. */
    std::optional<BoardPadShapeKey> pad;
};

/** Exact shared copper-clearance predicate result. */
struct BoardCopperClearanceCheck {
    /** Whether the pair participates in copper-clearance checking. */
    bool participates = false;
    /** Whether the pair violates the required clearance. */
    bool violates = false;
    /** First common copper layer used for diagnostics, when participating. */
    std::optional<BoardLayerId> layer;
    /** Measured copper-to-copper clearance in millimeters. */
    double actual_clearance_mm = 0.0;
    /** Required copper clearance in millimeters after rooms, classes, and matrix rules. */
    double required_clearance_mm = 0.0;
    /** Room that supplied an override, if any. */
    std::optional<BoardRoomId> room;
};

[[nodiscard]] double shape_distance(const BoardCopperShape &lhs, const BoardCopperShape &rhs);

[[nodiscard]] std::optional<BoardLayerId> first_common_layer(const BoardCopperShape &lhs,
                                                             const BoardCopperShape &rhs);

[[nodiscard]] bool layers_overlap(const BoardCopperShape &lhs, const BoardCopperShape &rhs);

[[nodiscard]] BoardClearanceKind shape_clearance_kind(const BoardCopperShape &shape);

[[nodiscard]] std::string clearance_pair_message(BoardClearanceKind lhs, BoardClearanceKind rhs);

[[nodiscard]] double maximum_required_copper_clearance(const Board &board);

[[nodiscard]] BoardCopperClearanceCheck check_copper_clearance(const Board &board,
                                                               const BoardCopperShape &lhs,
                                                               const BoardCopperShape &rhs);

[[nodiscard]] BoardCopperClearanceCheck
check_copper_clearance(const Board &board, const BoardCopperShape &lhs, BoardClearanceKind lhs_kind,
                       const BoardCopperShape &rhs, BoardClearanceKind rhs_kind);

void append_unique_layer(std::vector<BoardLayerId> &layers, BoardLayerId layer);

[[nodiscard]] std::vector<BoardLayerId> via_copper_layers(const Board &board, const BoardVia &via);

[[nodiscard]] std::vector<BoardLayerId>
pad_copper_layers(const Board &board, const FootprintPad &pad, BoardSide placement_side);

[[nodiscard]] const PadResolution *
find_board_pad_resolution(const std::vector<PadResolution> &resolutions,
                          ComponentPlacementId placement, FootprintPadId pad);

void append_track_shapes(const Board &board, std::vector<BoardCopperShape> &shapes);

void append_via_shapes(const Board &board, std::vector<BoardCopperShape> &shapes);

void append_zone_shapes(const Board &board, std::vector<BoardCopperShape> &shapes);

void append_pad_shapes(const Board &board, const FootprintLibrary &footprints,
                       const std::vector<PadResolution> &resolutions,
                       std::vector<BoardCopperShape> &shapes);

[[nodiscard]] std::vector<BoardCopperShape>
collect_copper_shapes(const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions);

[[nodiscard]] bool shape_satisfies_outline(const BoardCopperShape &shape,
                                           const BoardOutline &outline, double clearance_mm);

[[nodiscard]] std::vector<EntityRef> copper_shape_entities(const BoardCopperShape &shape, NetId net,
                                                           BoardLayerId layer);

void validate_track_widths(const Board &board, DiagnosticReport &report);

void validate_via_rules(const Board &board, DiagnosticReport &report);

void validate_outline_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                DiagnosticReport &report);

void validate_netless_zone_outline_clearance(const Board &board, DiagnosticReport &report);

void validate_copper_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                               DiagnosticReport &report);

[[nodiscard]] bool keepout_restricts(const BoardKeepout &keepout,
                                     BoardKeepoutRestriction restriction);

[[nodiscard]] bool shape_has_entity_kind(const BoardCopperShape &shape, EntityKind kind);

[[nodiscard]] std::optional<BoardLayerId>
first_common_keepout_layer(const BoardKeepout &keepout, const std::vector<BoardLayerId> &layers);

[[nodiscard]] bool shape_violates_keepout(const BoardCopperShape &shape,
                                          const BoardKeepout &keepout);

[[nodiscard]] std::vector<EntityRef>
keepout_copper_entities(BoardKeepoutId keepout, const BoardCopperShape &shape, BoardLayerId layer);

void validate_keepout_copper_shapes(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                    DiagnosticReport &report);

void validate_keepout_zones(const Board &board, DiagnosticReport &report);

void validate_keepout_vias(const Board &board, DiagnosticReport &report);

void validate_keepout_placements(const Board &board, DiagnosticReport &report);

void validate_board_visual(const Board &board, const FootprintLibrary &footprints,
                           DiagnosticReport &report);

[[nodiscard]] std::size_t connectivity_root(std::vector<std::size_t> &parents, std::size_t index);

[[nodiscard]] std::optional<std::size_t>
shape_index_for_pad(const std::vector<BoardCopperShape> &shapes, ComponentPlacementId placement,
                    FootprintPadId pad);

void validate_unrouted_nets(const Board &board, const std::vector<PadResolution> &resolutions,
                            const std::vector<BoardCopperShape> &shapes, DiagnosticReport &report);

void validate_board_drc(const Board &board, const FootprintLibrary &footprints,
                        const std::vector<PadResolution> &pad_resolutions,
                        DiagnosticReport &report);

} // namespace detail

/** Validate placement-only board design issues against circuit and footprint context. */
[[nodiscard]] DiagnosticReport validate_board(const Board &board,
                                              const FootprintLibrary &footprints);

} // namespace volt
