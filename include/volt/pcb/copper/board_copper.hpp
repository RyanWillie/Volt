#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>
#include <volt/pcb/geometry/board_outline.hpp>
#include <volt/pcb/layers/board_layers.hpp>

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

/** Kernel-owned board area where local routing rule overrides may apply. */
class BoardRoom {
  public:
    /** Construct a board room over one or more board layers. */
    BoardRoom(std::string name, BoardOutline outline, std::vector<BoardLayerId> layers,
              int priority = 0);

    /** Return the non-empty room name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the room outline. */
    [[nodiscard]] const BoardOutline &outline() const noexcept { return outline_; }

    /** Return board layers this room applies to. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

    /** Return deterministic priority/order metadata. */
    [[nodiscard]] int priority() const noexcept { return priority_; }

    /** Return the room copper clearance override, if present. */
    [[nodiscard]] std::optional<double> copper_clearance_mm() const noexcept {
        return copper_clearance_mm_;
    }

    /** Return the room track width override, if present. */
    [[nodiscard]] std::optional<double> track_width_mm() const noexcept { return track_width_mm_; }

    /** Set the room copper clearance override in millimeters. */
    void set_copper_clearance_mm(double value);

    /** Set the room track width override in millimeters. */
    void set_track_width_mm(double value);

  private:
    void validate_layers() const;

    std::string name_;
    BoardOutline outline_;
    std::vector<BoardLayerId> layers_;
    int priority_;
    std::optional<double> copper_clearance_mm_;
    std::optional<double> track_width_mm_;
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

/** Object kind participating in clearance-matrix lookups. */
enum class BoardClearanceKind {
    Track,
    Pad,
    Via,
    Zone,
    BoardEdge,
};

/** One unordered object-kind pair clearance entry in the board clearance matrix. */
struct BoardClearancePair {
    /** Canonically smaller object kind of the pair. */
    BoardClearanceKind first;
    /** Canonically larger object kind of the pair. */
    BoardClearanceKind second;
    /** Required clearance for the pair in millimeters. */
    double clearance_mm;
};

/** Required provenance attached to an ingested manufacturer capability profile. */
struct BoardCapabilityProvenance {
    /** Free-text source for the capability data. */
    std::string source;
    /** Date string describing when the source data was current. */
    std::string as_of;
};

/** Copper-weight-specific capability refinement for etching-dependent limits. */
struct BoardCapabilityCopperWeightRefinement {
    /** Finished copper weight in ounces per square foot. */
    double copper_weight_oz;
    /** Minimum track width in millimeters for this copper weight. */
    double minimum_track_width_mm;
    /** Minimum copper clearance in millimeters for this copper weight. */
    double minimum_clearance_mm;
};

/** Inclusive millimeter range used by fabrication capability profile limits. */
struct BoardCapabilityRange {
    /** Minimum allowed value in millimeters. */
    double minimum_mm;
    /** Maximum allowed value in millimeters. */
    double maximum_mm;
};

/** Kernel-owned manufacturer capability data loaded from project or profile documents. */
class BoardCapabilityProfile {
  public:
    /** Construct a validated capability profile with required provenance and limits. */
    BoardCapabilityProfile(
        std::string name, BoardCapabilityProvenance provenance, double minimum_track_width_mm,
        double minimum_via_drill_mm, double minimum_via_annular_mm,
        std::vector<BoardClearancePair> minimum_clearances,
        std::vector<BoardCapabilityCopperWeightRefinement> copper_weight_refinements = {},
        std::vector<int> supported_copper_layer_counts = {},
        std::optional<BoardCapabilityRange> board_thickness_range_mm = std::nullopt,
        std::vector<double> available_copper_weights_oz = {},
        std::optional<BoardCapabilityRange> drill_diameter_range_mm = std::nullopt);

    /** Return a clearly named conservative fallback profile that callers must apply explicitly. */
    [[nodiscard]] static BoardCapabilityProfile conservative_default();

    /** Return the non-empty profile name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return source and as-of provenance for auditability. */
    [[nodiscard]] const BoardCapabilityProvenance &provenance() const noexcept {
        return provenance_;
    }

    /** Return the base minimum routed track width in millimeters. */
    [[nodiscard]] double minimum_track_width_mm() const noexcept { return minimum_track_width_mm_; }

    /** Return the base minimum via drill diameter in millimeters. */
    [[nodiscard]] double minimum_via_drill_mm() const noexcept { return minimum_via_drill_mm_; }

    /** Return the base minimum via outer annular copper diameter in millimeters. */
    [[nodiscard]] double minimum_via_annular_mm() const noexcept { return minimum_via_annular_mm_; }

    /** Return the minimum clearance for a canonical object-kind pair, if specified. */
    [[nodiscard]] std::optional<double> minimum_clearance(BoardClearanceKind first,
                                                          BoardClearanceKind second) const noexcept;

    /** Return the minimum clearance for a canonical object-kind pair, or zero if unspecified. */
    [[nodiscard]] double minimum_clearance_mm(BoardClearanceKind first,
                                              BoardClearanceKind second) const noexcept;

    /** Return canonical minimum clearance entries in deterministic order. */
    [[nodiscard]] const std::vector<BoardClearancePair> &minimum_clearances() const noexcept {
        return minimum_clearances_;
    }

    /** Return copper-weight refinements in strictly ascending copper-weight order. */
    [[nodiscard]] const std::vector<BoardCapabilityCopperWeightRefinement> &
    copper_weight_refinements() const noexcept {
        return copper_weight_refinements_;
    }

    /** Return supported copper layer counts; empty means unconstrained by this profile. */
    [[nodiscard]] const std::vector<int> &supported_copper_layer_counts() const noexcept {
        return supported_copper_layer_counts_;
    }

    /** Return inclusive finished board thickness range, if constrained by this profile. */
    [[nodiscard]] const std::optional<BoardCapabilityRange> &
    board_thickness_range_mm() const noexcept {
        return board_thickness_range_mm_;
    }

    /** Return available finished copper weights; empty means unconstrained by this profile. */
    [[nodiscard]] const std::vector<double> &available_copper_weights_oz() const noexcept {
        return available_copper_weights_oz_;
    }

    /** Return inclusive drill diameter range, if constrained by this profile. */
    [[nodiscard]] const std::optional<BoardCapabilityRange> &
    drill_diameter_range_mm() const noexcept {
        return drill_diameter_range_mm_;
    }

  private:
    std::string name_;
    BoardCapabilityProvenance provenance_;
    double minimum_track_width_mm_;
    double minimum_via_drill_mm_;
    double minimum_via_annular_mm_;
    std::vector<BoardClearancePair> minimum_clearances_;
    std::vector<BoardCapabilityCopperWeightRefinement> copper_weight_refinements_;
    std::vector<int> supported_copper_layer_counts_;
    std::optional<BoardCapabilityRange> board_thickness_range_mm_;
    std::vector<double> available_copper_weights_oz_;
    std::optional<BoardCapabilityRange> drill_diameter_range_mm_;
};

/** Kernel-owned first PCB design-rule values, expressed in board millimeters. */
class BoardDesignRules {
  public:
    /** Construct first board-level DRC rules. */
    BoardDesignRules(double copper_clearance_mm = 0.15, double minimum_track_width_mm = 0.15,
                     double minimum_via_drill_diameter_mm = 0.20,
                     double minimum_via_annular_diameter_mm = 0.45,
                     double board_outline_clearance_mm = 0.0);

    /** Set the required clearance for one unordered object-kind pair. */
    void set_clearance_mm(BoardClearanceKind first, BoardClearanceKind second, double clearance_mm);

    /** Return the pair clearance: a matrix entry, or the scalar defaults. */
    [[nodiscard]] double clearance_mm(BoardClearanceKind first,
                                      BoardClearanceKind second) const noexcept;

    /** Return clearance-matrix entries in canonical deterministic order. */
    [[nodiscard]] const std::vector<BoardClearancePair> &clearance_matrix() const noexcept {
        return clearance_matrix_;
    }

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
    std::vector<BoardClearancePair> clearance_matrix_;
};

} // namespace volt
