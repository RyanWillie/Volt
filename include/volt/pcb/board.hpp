#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board_copper.hpp>
#include <volt/pcb/board_copper_model.hpp>
#include <volt/pcb/board_features.hpp>
#include <volt/pcb/board_footprint_model.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_layers.hpp>
#include <volt/pcb/board_outline.hpp>
#include <volt/pcb/board_placement_model.hpp>
#include <volt/pcb/board_structure_model.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** PCB board projection over a logical circuit. */
class Board {
  public:
    /** Construct a board projection over one logical circuit. */
    explicit Board(const Circuit &circuit, BoardName name = BoardName{"Main"});

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

    /** Add board text on an existing board layer. */
    [[nodiscard]] BoardTextId add_text(BoardText text);

    /** Return a board layer by board-local ID. */
    [[nodiscard]] const BoardLayer &layer(BoardLayerId id) const { return structure_.layer(id); }

    /** Return the number of board layers. */
    [[nodiscard]] std::size_t layer_count() const noexcept { return structure_.layer_count(); }

    /** Return the current layer stack, if assigned. */
    [[nodiscard]] const std::optional<LayerStack> &layer_stack() const noexcept;

    /** Return the board outline, if assigned. */
    [[nodiscard]] const std::optional<BoardOutline> &outline() const noexcept {
        return structure_.outline();
    }

    /** Return board-owned design rules used by DRC validation. */
    [[nodiscard]] const BoardDesignRules &design_rules() const noexcept {
        return structure_.design_rules();
    }

    /** Return a board feature by ID. */
    [[nodiscard]] const BoardFeature &feature(BoardFeatureId id) const {
        return structure_.feature(id);
    }

    /** Return the number of stored board features. */
    [[nodiscard]] std::size_t feature_count() const noexcept { return structure_.feature_count(); }

    /** Return a cached footprint definition by board-local ID. */
    [[nodiscard]] const FootprintDefinition &footprint_definition(FootprintDefId id) const;

    /** Return the number of cached footprint definitions. */
    [[nodiscard]] std::size_t footprint_definition_count() const noexcept;

    /** Return a cached footprint definition ID for a library-qualified reference, if present. */
    [[nodiscard]] std::optional<FootprintDefId>
    footprint_definition_id(const FootprintRef &ref) const noexcept;

    /** Return a placement by board-local ID. */
    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const;

    /** Return the number of component placements. */
    [[nodiscard]] std::size_t placement_count() const noexcept {
        return placements_.placement_count();
    }

    /** Return a routed copper track by board-local ID. */
    [[nodiscard]] const BoardTrack &track(BoardTrackId id) const { return copper_.track(id); }

    /** Return the number of routed copper tracks. */
    [[nodiscard]] std::size_t track_count() const noexcept { return copper_.track_count(); }

    /** Return a routed copper via by board-local ID. */
    [[nodiscard]] const BoardVia &via(BoardViaId id) const { return copper_.via(id); }

    /** Return the number of routed copper vias. */
    [[nodiscard]] std::size_t via_count() const noexcept { return copper_.via_count(); }

    /** Return a copper zone by board-local ID. */
    [[nodiscard]] const BoardZone &zone(BoardZoneId id) const { return copper_.zone(id); }

    /** Return the number of copper zones. */
    [[nodiscard]] std::size_t zone_count() const noexcept { return copper_.zone_count(); }

    /** Return a keepout by board-local ID. */
    [[nodiscard]] const BoardKeepout &keepout(BoardKeepoutId id) const {
        return copper_.keepout(id);
    }

    /** Return the number of keepouts. */
    [[nodiscard]] std::size_t keepout_count() const noexcept { return copper_.keepout_count(); }

    /** Return board text by board-local ID. */
    [[nodiscard]] const BoardText &text(BoardTextId id) const { return copper_.text(id); }

    /** Return the number of board text primitives. */
    [[nodiscard]] std::size_t text_count() const noexcept { return copper_.text_count(); }

    /** Return the placement ID for a component, if present. */
    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept;

    /** Derive pad-to-pin/net resolution for all placed components. */
    [[nodiscard]] std::vector<PadResolution> resolve_pads(const FootprintLibrary &footprints) const;

    /** Derive deterministic unrouted ratsnest edges for all placed multi-pad nets. */
    [[nodiscard]] std::vector<RatsnestEdge>
    ratsnest_edges(const FootprintLibrary &footprints) const;

  private:
    void require_layer(BoardLayerId layer) const;

    void require_net(NetId net) const;

    void require_copper_layer(BoardLayerId layer_id) const;

    void append_pad_resolutions(ComponentPlacementId placement_id,
                                const ComponentPlacement &component_placement,
                                const FootprintDefinition &definition,
                                const std::vector<FootprintPadBinding> &bindings,
                                std::vector<PadResolution> &resolutions) const;

    const Circuit *circuit_;
    BoardName name_;
    BoardUnits units_{BoardUnits::Millimeters};
    BoardStructureModel structure_;
    BoardFootprintModel footprint_cache_;
    BoardPlacementModel placements_;
    BoardCopperModel copper_;
};

namespace detail {

[[nodiscard]] FootprintLibrary board_resolution_footprints(const Board &board,
                                                           const FootprintLibrary &footprints);

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

enum class BoardCopperShapeKind {
    Disc,
    Segment,
    Polygon,
};

struct BoardPadShapeKey {
    ComponentPlacementId placement;
    FootprintPadId pad;
};

struct BoardCopperShape {
    BoardCopperShapeKind kind;
    NetId net;
    std::vector<BoardLayerId> layers;
    std::vector<EntityRef> primary_entities;
    std::vector<BoardPoint> points;
    double radius_mm;
    std::optional<BoardPadShapeKey> pad;
};

[[nodiscard]] double shape_distance(const BoardCopperShape &lhs, const BoardCopperShape &rhs);

[[nodiscard]] std::optional<BoardLayerId> first_common_layer(const BoardCopperShape &lhs,
                                                             const BoardCopperShape &rhs);

[[nodiscard]] bool layers_overlap(const BoardCopperShape &lhs, const BoardCopperShape &rhs);

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

[[nodiscard]] bool polygon_satisfies_outline(const std::vector<BoardPoint> &polygon,
                                             const BoardOutline &outline, double clearance_mm);

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

[[nodiscard]] std::size_t connectivity_root(std::vector<std::size_t> &parents, std::size_t index);

[[nodiscard]] std::optional<std::size_t>
shape_index_for_pad(const std::vector<BoardCopperShape> &shapes, ComponentPlacementId placement,
                    FootprintPadId pad);

void validate_unrouted_nets(const std::vector<PadResolution> &resolutions,
                            const std::vector<BoardCopperShape> &shapes, DiagnosticReport &report);

void validate_board_drc(const Board &board, const FootprintLibrary &footprints,
                        const std::vector<PadResolution> &pad_resolutions,
                        DiagnosticReport &report);

} // namespace detail

/** Validate placement-only board design issues against circuit and footprint context. */
[[nodiscard]] DiagnosticReport validate_board(const Board &board,
                                              const FootprintLibrary &footprints);

} // namespace volt
