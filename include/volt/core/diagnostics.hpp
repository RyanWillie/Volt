#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

namespace diagnostic_categories {

inline constexpr auto General = std::string_view{"general"};
inline constexpr auto Erc = std::string_view{"erc"};
inline constexpr auto Drc = std::string_view{"drc"};
inline constexpr auto PartLineup = std::string_view{"part.lineup"};
inline constexpr auto PcbBoard = std::string_view{"pcb.board"};
inline constexpr auto PcbVisual = std::string_view{"pcb.visual"};
inline constexpr auto PcbFabrication = std::string_view{"pcb.fabrication"};
inline constexpr auto Bom = std::string_view{"bom"};

} // namespace diagnostic_categories

namespace diagnostic_category_catalogs {

inline constexpr auto All =
    std::array{diagnostic_categories::General,       diagnostic_categories::Erc,
               diagnostic_categories::Drc,           diagnostic_categories::PartLineup,
               diagnostic_categories::PcbBoard,      diagnostic_categories::PcbVisual,
               diagnostic_categories::PcbFabrication, diagnostic_categories::Bom};

} // namespace diagnostic_category_catalogs

namespace erc_diagnostic_codes {

inline constexpr auto PinMustNotConnect = std::string_view{"PIN_MUST_NOT_CONNECT"};
inline constexpr auto PinIntentionalNoConnectIsConnected =
    std::string_view{"PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED"};
inline constexpr auto UnconnectedRequiredPin = std::string_view{"UNCONNECTED_REQUIRED_PIN"};
inline constexpr auto EmptyNet = std::string_view{"EMPTY_NET"};
inline constexpr auto SinglePinNet = std::string_view{"SINGLE_PIN_NET"};
inline constexpr auto UnboundRequiredPort = std::string_view{"UNBOUND_REQUIRED_PORT"};
inline constexpr auto PinGroundOnNonGroundNet = std::string_view{"PIN_GROUND_ON_NON_GROUND_NET"};
inline constexpr auto PinPowerOnGroundNet = std::string_view{"PIN_POWER_ON_GROUND_NET"};
inline constexpr auto PowerInputWithoutSource = std::string_view{"POWER_INPUT_WITHOUT_SOURCE"};
inline constexpr auto SelectedPartVoltageRatingExceeded =
    std::string_view{"SELECTED_PART_VOLTAGE_RATING_EXCEEDED"};
inline constexpr auto PinVoltageRangeViolation = std::string_view{"PIN_VOLTAGE_RANGE_VIOLATION"};
inline constexpr auto NetClassVoltageExceeded = std::string_view{"NET_CLASS_VOLTAGE_EXCEEDED"};
inline constexpr auto MultipleOutputsOnNet = std::string_view{"MULTIPLE_OUTPUTS_ON_NET"};
inline constexpr auto InputSignalDomainMismatch = std::string_view{"INPUT_SIGNAL_DOMAIN_MISMATCH"};

} // namespace erc_diagnostic_codes

namespace drc_diagnostic_codes {

inline constexpr auto TrackWidthBelowMinimum = std::string_view{"PCB_TRACK_WIDTH_BELOW_MINIMUM"};
inline constexpr auto ViaDrillBelowMinimum = std::string_view{"PCB_VIA_DRILL_BELOW_MINIMUM"};
inline constexpr auto ViaAnnularBelowMinimum = std::string_view{"PCB_VIA_ANNULAR_BELOW_MINIMUM"};
inline constexpr auto CopperOutsideOutline = std::string_view{"PCB_COPPER_OUTSIDE_OUTLINE"};
inline constexpr auto CopperClearanceViolation = std::string_view{"PCB_COPPER_CLEARANCE_VIOLATION"};
inline constexpr auto KeepoutCopperViolation = std::string_view{"PCB_KEEPOUT_COPPER_VIOLATION"};
inline constexpr auto KeepoutViaViolation = std::string_view{"PCB_KEEPOUT_VIA_VIOLATION"};
inline constexpr auto KeepoutPlacementViolation =
    std::string_view{"PCB_KEEPOUT_PLACEMENT_VIOLATION"};
inline constexpr auto NetUnrouted = std::string_view{"PCB_NET_UNROUTED"};
inline constexpr auto NetClassTrackWidthViolation =
    std::string_view{"PCB_TRACK_WIDTH_BELOW_NET_CLASS"};
inline constexpr auto NetClassViaDrillViolation = std::string_view{"PCB_VIA_DRILL_BELOW_NET_CLASS"};
inline constexpr auto NetClassViaDiameterViolation =
    std::string_view{"PCB_VIA_DIAMETER_BELOW_NET_CLASS"};
inline constexpr auto NetClassDisallowedLayer = std::string_view{"PCB_COPPER_ON_DISALLOWED_LAYER"};
inline constexpr auto RuleBelowCapability = std::string_view{"PCB_RULE_BELOW_CAPABILITY"};
inline constexpr auto RuleAtCapabilityMinimum = std::string_view{"PCB_RULE_AT_CAPABILITY_MINIMUM"};
inline constexpr auto CopperLayerCountOutsideCapability =
    std::string_view{"PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY"};
inline constexpr auto BoardThicknessOutsideCapability =
    std::string_view{"PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY"};
inline constexpr auto BoardThicknessAtCapabilityLimit =
    std::string_view{"PCB_BOARD_THICKNESS_AT_CAPABILITY_LIMIT"};
inline constexpr auto CopperWeightOutsideCapability =
    std::string_view{"PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY"};
inline constexpr auto DrillDiameterOutsideCapability =
    std::string_view{"PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY"};
inline constexpr auto DrillDiameterAtCapabilityLimit =
    std::string_view{"PCB_DRILL_DIAMETER_AT_CAPABILITY_LIMIT"};

} // namespace drc_diagnostic_codes

namespace pcb_visual_diagnostic_codes {

inline constexpr auto PlacementOverlap = std::string_view{"PCB_VISUAL_PLACEMENT_OVERLAP"};
inline constexpr auto PlacementCrowding = std::string_view{"PCB_VISUAL_PLACEMENT_CROWDING"};
inline constexpr auto ReferenceDesignatorHidden =
    std::string_view{"PCB_VISUAL_REFERENCE_DESIGNATOR_HIDDEN"};
inline constexpr auto ReferenceDesignatorUnreadable =
    std::string_view{"PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE"};
inline constexpr auto LabelOverlap = std::string_view{"PCB_VISUAL_LABEL_OVERLAP"};
inline constexpr auto LabelOutsideBoard = std::string_view{"PCB_VISUAL_LABEL_OUTSIDE_BOARD"};
inline constexpr auto RouteReadabilityConflict =
    std::string_view{"PCB_VISUAL_ROUTE_READABILITY_CONFLICT"};
inline constexpr auto BoardFeatureAnnotationMissing =
    std::string_view{"PCB_VISUAL_BOARD_FEATURE_ANNOTATION_MISSING"};

} // namespace pcb_visual_diagnostic_codes

namespace pcb_fabrication_diagnostic_codes {

inline constexpr auto KiCadFabExportLoss = std::string_view{"PCB_KICAD_FAB_EXPORT_LOSS"};

} // namespace pcb_fabrication_diagnostic_codes

namespace part_lineup_diagnostic_codes {

inline constexpr auto PinWithoutPad = std::string_view{"PART_PIN_WITHOUT_PAD"};
inline constexpr auto PadWithoutPin = std::string_view{"PART_PAD_WITHOUT_PIN"};
inline constexpr auto PadOverlap = std::string_view{"PART_PAD_OVERLAP"};
inline constexpr auto PadRowPitchInconsistent = std::string_view{"PART_PAD_ROW_PITCH_INCONSISTENT"};

} // namespace part_lineup_diagnostic_codes

namespace bom_diagnostic_codes {

inline constexpr auto ComponentMissingSelectedPart =
    std::string_view{"BOM_COMPONENT_MISSING_SELECTED_PART"};
inline constexpr auto ComponentImplicitDnp = std::string_view{"BOM_COMPONENT_IMPLICIT_DNP"};
inline constexpr auto ApprovedAlternateDuplicatesPrimary =
    std::string_view{"BOM_APPROVED_ALTERNATE_DUPLICATES_PRIMARY"};

} // namespace bom_diagnostic_codes

namespace diagnostic_code_catalogs {

inline constexpr auto Erc = std::array{erc_diagnostic_codes::PinMustNotConnect,
                                       erc_diagnostic_codes::PinIntentionalNoConnectIsConnected,
                                       erc_diagnostic_codes::UnconnectedRequiredPin,
                                       erc_diagnostic_codes::EmptyNet,
                                       erc_diagnostic_codes::SinglePinNet,
                                       erc_diagnostic_codes::UnboundRequiredPort,
                                       erc_diagnostic_codes::PinGroundOnNonGroundNet,
                                       erc_diagnostic_codes::PinPowerOnGroundNet,
                                       erc_diagnostic_codes::PowerInputWithoutSource,
                                       erc_diagnostic_codes::SelectedPartVoltageRatingExceeded,
                                       erc_diagnostic_codes::PinVoltageRangeViolation,
                                       erc_diagnostic_codes::NetClassVoltageExceeded,
                                       erc_diagnostic_codes::MultipleOutputsOnNet,
                                       erc_diagnostic_codes::InputSignalDomainMismatch};

inline constexpr auto Drc = std::array{drc_diagnostic_codes::TrackWidthBelowMinimum,
                                       drc_diagnostic_codes::ViaDrillBelowMinimum,
                                       drc_diagnostic_codes::ViaAnnularBelowMinimum,
                                       drc_diagnostic_codes::CopperOutsideOutline,
                                       drc_diagnostic_codes::CopperClearanceViolation,
                                       drc_diagnostic_codes::KeepoutCopperViolation,
                                       drc_diagnostic_codes::KeepoutViaViolation,
                                       drc_diagnostic_codes::KeepoutPlacementViolation,
                                       drc_diagnostic_codes::NetUnrouted,
                                       drc_diagnostic_codes::NetClassTrackWidthViolation,
                                       drc_diagnostic_codes::NetClassViaDrillViolation,
                                       drc_diagnostic_codes::NetClassViaDiameterViolation,
                                       drc_diagnostic_codes::NetClassDisallowedLayer,
                                       drc_diagnostic_codes::RuleBelowCapability,
                                       drc_diagnostic_codes::RuleAtCapabilityMinimum,
                                       drc_diagnostic_codes::CopperLayerCountOutsideCapability,
                                       drc_diagnostic_codes::BoardThicknessOutsideCapability,
                                       drc_diagnostic_codes::BoardThicknessAtCapabilityLimit,
                                       drc_diagnostic_codes::CopperWeightOutsideCapability,
                                       drc_diagnostic_codes::DrillDiameterOutsideCapability,
                                       drc_diagnostic_codes::DrillDiameterAtCapabilityLimit};

inline constexpr auto PcbVisual =
    std::array{pcb_visual_diagnostic_codes::PlacementOverlap,
               pcb_visual_diagnostic_codes::PlacementCrowding,
               pcb_visual_diagnostic_codes::ReferenceDesignatorHidden,
               pcb_visual_diagnostic_codes::ReferenceDesignatorUnreadable,
               pcb_visual_diagnostic_codes::LabelOverlap,
               pcb_visual_diagnostic_codes::LabelOutsideBoard,
               pcb_visual_diagnostic_codes::RouteReadabilityConflict,
               pcb_visual_diagnostic_codes::BoardFeatureAnnotationMissing};

inline constexpr auto PcbFabrication =
    std::array{pcb_fabrication_diagnostic_codes::KiCadFabExportLoss};

inline constexpr auto PartLineup = std::array{
    part_lineup_diagnostic_codes::PinWithoutPad, part_lineup_diagnostic_codes::PadWithoutPin,
    part_lineup_diagnostic_codes::PadOverlap,
    part_lineup_diagnostic_codes::PadRowPitchInconsistent};

inline constexpr auto Bom = std::array{bom_diagnostic_codes::ComponentMissingSelectedPart,
                                       bom_diagnostic_codes::ComponentImplicitDnp,
                                       bom_diagnostic_codes::ApprovedAlternateDuplicatesPrimary};

} // namespace diagnostic_code_catalogs

/** Severity level for a diagnostic emitted by the kernel. */
enum class Severity {
    Info,
    Warning,
    Error,
};

/** Machine-readable diagnostic code. */
class DiagnosticCode {
  public:
    /** Construct a non-empty diagnostic code. */
    explicit DiagnosticCode(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Diagnostic code must not be empty"};
        }
    }

    /** Return the stored diagnostic code string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two diagnostic codes carry the same value. */
    [[nodiscard]] friend bool operator==(const DiagnosticCode &lhs,
                                         const DiagnosticCode &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    /** Order diagnostic codes lexicographically by value. */
    [[nodiscard]] friend bool operator<(const DiagnosticCode &lhs,
                                        const DiagnosticCode &rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

  private:
    std::string value_;
};

/** Stable diagnostic category used by report and viewer surfaces. */
class DiagnosticCategory {
  public:
    /** Construct a non-empty diagnostic category. */
    explicit DiagnosticCategory(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Diagnostic category must not be empty"};
        }
    }

    /** Construct a diagnostic category from a stable category constant. */
    explicit DiagnosticCategory(std::string_view value) : DiagnosticCategory{std::string{value}} {}

    /** Construct a diagnostic category from a string literal. */
    explicit DiagnosticCategory(const char *value) : DiagnosticCategory{std::string{value}} {}

    /** Return the stored diagnostic category string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two diagnostic categories carry the same value. */
    [[nodiscard]] friend bool operator==(const DiagnosticCategory &lhs,
                                         const DiagnosticCategory &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Kind of entity referenced by a diagnostic. */
enum class EntityKind {
    Board,
    PartDefinition,
    ComponentDef,
    Component,
    PinDef,
    Pin,
    Net,
    ModuleDef,
    ModuleInstance,
    PortDef,
    SymbolDef,
    Sheet,
    SymbolInstance,
    WireRun,
    NetLabel,
    Junction,
    PowerPort,
    NoConnectMarker,
    SheetPort,
    SymbolField,
    BoardLayer,
    BoardFeature,
    BoardTrack,
    BoardVia,
    BoardZone,
    BoardKeepout,
    BoardRoom,
    BoardText,
    FootprintDef,
    FootprintPad,
    ComponentPlacement,
};

/**
 * Reference to an entity involved in a diagnostic.
 *
 * EntityRef is a reporting type, not the core storage model. It deliberately stores kind
 * plus index so diagnostics can refer to different entity families uniformly.
 */
class EntityRef {
  public:
    /** Create a reference to the board projection root. */
    [[nodiscard]] static EntityRef board() noexcept { return EntityRef{EntityKind::Board, 0U}; }

    /** Create a reference to a standalone part definition artifact. */
    [[nodiscard]] static EntityRef part_definition() noexcept {
        return EntityRef{EntityKind::PartDefinition, 0U};
    }

    /** Create a reference to a component definition. */
    [[nodiscard]] static EntityRef component_def(ComponentDefId id) noexcept {
        return EntityRef{EntityKind::ComponentDef, id.index()};
    }

    /** Create a reference to a component instance. */
    [[nodiscard]] static EntityRef component(ComponentId id) noexcept {
        return EntityRef{EntityKind::Component, id.index()};
    }

    /** Create a reference to a pin definition. */
    [[nodiscard]] static EntityRef pin_def(PinDefId id) noexcept {
        return EntityRef{EntityKind::PinDef, id.index()};
    }

    /** Create a reference to a pin instance. */
    [[nodiscard]] static EntityRef pin(PinId id) noexcept {
        return EntityRef{EntityKind::Pin, id.index()};
    }

    /** Create a reference to a net. */
    [[nodiscard]] static EntityRef net(NetId id) noexcept {
        return EntityRef{EntityKind::Net, id.index()};
    }

    /** Create a reference to a module definition. */
    [[nodiscard]] static EntityRef module_def(ModuleDefId id) noexcept {
        return EntityRef{EntityKind::ModuleDef, id.index()};
    }

    /** Create a reference to a module instance. */
    [[nodiscard]] static EntityRef module_instance(ModuleInstanceId id) noexcept {
        return EntityRef{EntityKind::ModuleInstance, id.index()};
    }

    /** Create a reference to a module port definition. */
    [[nodiscard]] static EntityRef port_def(PortDefId id) noexcept {
        return EntityRef{EntityKind::PortDef, id.index()};
    }

    /** Create a reference to a schematic symbol definition. */
    [[nodiscard]] static EntityRef symbol_def(SymbolDefId id) noexcept {
        return EntityRef{EntityKind::SymbolDef, id.index()};
    }

    /** Create a reference to a schematic sheet. */
    [[nodiscard]] static EntityRef sheet(SheetId id) noexcept {
        return EntityRef{EntityKind::Sheet, id.index()};
    }

    /** Create a reference to a placed schematic symbol instance. */
    [[nodiscard]] static EntityRef symbol_instance(SymbolInstanceId id) noexcept {
        return EntityRef{EntityKind::SymbolInstance, id.index()};
    }

    /** Create a reference to a schematic wire run. */
    [[nodiscard]] static EntityRef wire_run(WireRunId id) noexcept {
        return EntityRef{EntityKind::WireRun, id.index()};
    }

    /** Create a reference to a schematic net label. */
    [[nodiscard]] static EntityRef net_label(NetLabelId id) noexcept {
        return EntityRef{EntityKind::NetLabel, id.index()};
    }

    /** Create a reference to a schematic junction. */
    [[nodiscard]] static EntityRef junction(JunctionId id) noexcept {
        return EntityRef{EntityKind::Junction, id.index()};
    }

    /** Create a reference to a schematic power or ground port. */
    [[nodiscard]] static EntityRef power_port(PowerPortId id) noexcept {
        return EntityRef{EntityKind::PowerPort, id.index()};
    }

    /** Create a reference to a schematic no-connect marker. */
    [[nodiscard]] static EntityRef no_connect_marker(NoConnectMarkerId id) noexcept {
        return EntityRef{EntityKind::NoConnectMarker, id.index()};
    }

    /** Create a reference to a schematic sheet/off-page port. */
    [[nodiscard]] static EntityRef sheet_port(SheetPortId id) noexcept {
        return EntityRef{EntityKind::SheetPort, id.index()};
    }

    /** Create a reference to a schematic symbol field. */
    [[nodiscard]] static EntityRef symbol_field(SymbolFieldId id) noexcept {
        return EntityRef{EntityKind::SymbolField, id.index()};
    }

    /** Create a reference to a PCB board layer. */
    [[nodiscard]] static EntityRef board_layer(BoardLayerId id) noexcept {
        return EntityRef{EntityKind::BoardLayer, id.index()};
    }

    /** Create a reference to a PCB board feature. */
    [[nodiscard]] static EntityRef board_feature(BoardFeatureId id) noexcept {
        return EntityRef{EntityKind::BoardFeature, id.index()};
    }

    /** Create a reference to a PCB track. */
    [[nodiscard]] static EntityRef board_track(BoardTrackId id) noexcept {
        return EntityRef{EntityKind::BoardTrack, id.index()};
    }

    /** Create a reference to a PCB via. */
    [[nodiscard]] static EntityRef board_via(BoardViaId id) noexcept {
        return EntityRef{EntityKind::BoardVia, id.index()};
    }

    /** Create a reference to a PCB zone. */
    [[nodiscard]] static EntityRef board_zone(BoardZoneId id) noexcept {
        return EntityRef{EntityKind::BoardZone, id.index()};
    }

    /** Create a reference to a PCB keepout. */
    [[nodiscard]] static EntityRef board_keepout(BoardKeepoutId id) noexcept {
        return EntityRef{EntityKind::BoardKeepout, id.index()};
    }

    /** Create a reference to a PCB room. */
    [[nodiscard]] static EntityRef board_room(BoardRoomId id) noexcept {
        return EntityRef{EntityKind::BoardRoom, id.index()};
    }

    /** Create a reference to PCB board text. */
    [[nodiscard]] static EntityRef board_text(BoardTextId id) noexcept {
        return EntityRef{EntityKind::BoardText, id.index()};
    }

    /** Create a reference to a cached PCB footprint definition. */
    [[nodiscard]] static EntityRef footprint_def(FootprintDefId id) noexcept {
        return EntityRef{EntityKind::FootprintDef, id.index()};
    }

    /** Create a reference to a PCB footprint pad. */
    [[nodiscard]] static EntityRef footprint_pad(FootprintPadId id) noexcept {
        return EntityRef{EntityKind::FootprintPad, id.index()};
    }

    /** Create a reference to a PCB component placement. */
    [[nodiscard]] static EntityRef component_placement(ComponentPlacementId id) noexcept {
        return EntityRef{EntityKind::ComponentPlacement, id.index()};
    }

    /** Return the kind of entity referenced. */
    [[nodiscard]] EntityKind kind() const noexcept { return kind_; }

    /** Return the entity table index referenced. */
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    /** Return whether two entity references point at the same entity kind and index. */
    [[nodiscard]] friend bool operator==(EntityRef lhs, EntityRef rhs) noexcept = default;

  private:
    constexpr EntityRef(EntityKind kind, std::size_t index) noexcept : kind_{kind}, index_{index} {}

    EntityKind kind_;
    std::size_t index_;
};

/** A board-space point in millimeters for diagnostic overlay geometry. */
struct DiagnosticPoint {
    /** Construct a finite board-space diagnostic point. */
    DiagnosticPoint(double x, double y) : x_mm{x}, y_mm{y} {
        if (!std::isfinite(x_mm) || !std::isfinite(y_mm)) {
            throw std::invalid_argument{"Diagnostic overlay points must be finite"};
        }
    }

    /** X coordinate in board-space millimeters. */
    double x_mm;
    /** Y coordinate in board-space millimeters. */
    double y_mm;

    /** Return whether two points have identical coordinates. */
    [[nodiscard]] friend bool operator==(DiagnosticPoint lhs,
                                         DiagnosticPoint rhs) noexcept = default;
};

/** Typed actual-versus-required measurement attached to one diagnostic. */
struct DiagnosticMeasurement {
    /** Measured value that triggered the diagnostic, in millimeters. */
    double actual_mm;
    /** Required value the measured value must satisfy, in millimeters. */
    double required_mm;

    /** Return whether two measurements report identical actual and required values. */
    [[nodiscard]] friend bool operator==(DiagnosticMeasurement lhs,
                                         DiagnosticMeasurement rhs) noexcept = default;
};

/** Shape category for diagnostic overlay geometry. */
enum class DiagnosticOverlayKind {
    BoundingBox,
    Point,
    Polygon,
    Segment,
};

/** Overlay-ready geometry and references attached to one diagnostic. */
class DiagnosticOverlay {
  public:
    /** Create a bounding box overlay from minimum and maximum board-space corners. */
    [[nodiscard]] static DiagnosticOverlay bounding_box(DiagnosticPoint min, DiagnosticPoint max,
                                                        std::vector<EntityRef> entities = {},
                                                        std::vector<BoardLayerId> layers = {}) {
        return DiagnosticOverlay{DiagnosticOverlayKind::BoundingBox, std::vector{min, max},
                                 std::move(entities), std::move(layers)};
    }

    /** Create a point overlay in board-space coordinates. */
    [[nodiscard]] static DiagnosticOverlay point(DiagnosticPoint point,
                                                 std::vector<EntityRef> entities = {},
                                                 std::vector<BoardLayerId> layers = {}) {
        return DiagnosticOverlay{DiagnosticOverlayKind::Point, std::vector{point},
                                 std::move(entities), std::move(layers)};
    }

    /** Create a polygon overlay from ordered board-space vertices. */
    [[nodiscard]] static DiagnosticOverlay polygon(std::vector<DiagnosticPoint> points,
                                                   std::vector<EntityRef> entities = {},
                                                   std::vector<BoardLayerId> layers = {}) {
        return DiagnosticOverlay{DiagnosticOverlayKind::Polygon, std::move(points),
                                 std::move(entities), std::move(layers)};
    }

    /** Create a line segment overlay from ordered board-space endpoints. */
    [[nodiscard]] static DiagnosticOverlay segment(DiagnosticPoint start, DiagnosticPoint end,
                                                   std::vector<EntityRef> entities = {},
                                                   std::vector<BoardLayerId> layers = {}) {
        return DiagnosticOverlay{DiagnosticOverlayKind::Segment, std::vector{start, end},
                                 std::move(entities), std::move(layers)};
    }

    /** Return the overlay shape category. */
    [[nodiscard]] DiagnosticOverlayKind kind() const noexcept { return kind_; }

    /** Return ordered board-space overlay points. */
    [[nodiscard]] const std::vector<DiagnosticPoint> &points() const noexcept { return points_; }

    /** Return model entities this overlay highlights. */
    [[nodiscard]] const std::vector<EntityRef> &entities() const noexcept { return entities_; }

    /** Return board layer references relevant to this overlay. */
    [[nodiscard]] const std::vector<BoardLayerId> &layers() const noexcept { return layers_; }

  private:
    DiagnosticOverlay(DiagnosticOverlayKind kind, std::vector<DiagnosticPoint> points,
                      std::vector<EntityRef> entities, std::vector<BoardLayerId> layers)
        : kind_{kind}, points_{std::move(points)}, entities_{std::move(entities)},
          layers_{std::move(layers)} {
        validate_shape(kind_, points_);
    }

    static void validate_shape(DiagnosticOverlayKind kind,
                               const std::vector<DiagnosticPoint> &points) {
        switch (kind) {
        case DiagnosticOverlayKind::BoundingBox:
        case DiagnosticOverlayKind::Segment:
            if (points.size() != 2U) {
                throw std::invalid_argument{
                    "Diagnostic overlay bounding boxes and segments require two points"};
            }
            return;
        case DiagnosticOverlayKind::Point:
            if (points.size() != 1U) {
                throw std::invalid_argument{"Diagnostic point overlays require one point"};
            }
            return;
        case DiagnosticOverlayKind::Polygon:
            if (points.size() < 3U) {
                throw std::invalid_argument{
                    "Diagnostic polygon overlays require at least three points"};
            }
            return;
        }
        throw std::logic_error{"Unhandled diagnostic overlay kind"};
    }

    DiagnosticOverlayKind kind_;
    std::vector<DiagnosticPoint> points_;
    std::vector<EntityRef> entities_;
    std::vector<BoardLayerId> layers_;
};

/** Human- and machine-readable diagnostic emitted by kernel checks. */
class Diagnostic {
  public:
    /** Construct a diagnostic with optional related entities. */
    Diagnostic(Severity severity, DiagnosticCode code, std::string message,
               std::vector<EntityRef> entities = {})
        : Diagnostic{severity,
                     std::move(code),
                     DiagnosticCategory{diagnostic_categories::General},
                     std::move(message),
                     std::move(entities),
                     {}} {}

    /**
     * Construct a diagnostic with category, related entities, optional overlay geometry, and an
     * optional typed measurement.
     */
    Diagnostic(Severity severity, DiagnosticCode code, DiagnosticCategory category,
               std::string message, std::vector<EntityRef> entities = {},
               std::vector<DiagnosticOverlay> overlays = {},
               std::optional<DiagnosticMeasurement> measurement = std::nullopt,
               std::optional<std::string> rule = std::nullopt)
        : severity_{severity}, code_{std::move(code)}, category_{std::move(category)},
          message_{std::move(message)}, entities_{std::move(entities)},
          overlays_{std::move(overlays)}, measurement_{measurement}, rule_{std::move(rule)} {
        if (rule_.has_value() && rule_->empty()) {
            throw std::invalid_argument{"Diagnostic rule identity must not be empty"};
        }
    }

    /** Return the diagnostic severity. */
    [[nodiscard]] Severity severity() const noexcept { return severity_; }

    /** Return the machine-readable code. */
    [[nodiscard]] const DiagnosticCode &code() const noexcept { return code_; }

    /** Return the stable diagnostic category. */
    [[nodiscard]] const DiagnosticCategory &category() const noexcept { return category_; }

    /** Return the human-readable message. */
    [[nodiscard]] const std::string &message() const noexcept { return message_; }

    /** Return entities related to this diagnostic. */
    [[nodiscard]] const std::vector<EntityRef> &entities() const noexcept { return entities_; }

    /** Return overlay geometry associated with this diagnostic. */
    [[nodiscard]] const std::vector<DiagnosticOverlay> &overlays() const noexcept {
        return overlays_;
    }

    /** Return the typed actual-versus-required measurement, when one is meaningful. */
    [[nodiscard]] const std::optional<DiagnosticMeasurement> &measurement() const noexcept {
        return measurement_;
    }

    /** Return the stable rule or adapter construct identity, when one is meaningful. */
    [[nodiscard]] const std::optional<std::string> &rule() const noexcept { return rule_; }

  private:
    Severity severity_;
    DiagnosticCode code_;
    DiagnosticCategory category_;
    std::string message_;
    std::vector<EntityRef> entities_;
    std::vector<DiagnosticOverlay> overlays_;
    std::optional<DiagnosticMeasurement> measurement_;
    std::optional<std::string> rule_;
};

/** Ordered collection of diagnostics from one or more kernel checks. */
class DiagnosticReport {
  public:
    /** Append a diagnostic while preserving insertion order. */
    void add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

    /** Return all diagnostics in insertion order. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    /** Return whether the report has no diagnostics. */
    [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }

    /** Return whether any diagnostic has error severity. */
    [[nodiscard]] bool has_errors() const noexcept { return count(Severity::Error) > 0; }

    /** Return the total diagnostic count. */
    [[nodiscard]] std::size_t count() const noexcept { return diagnostics_.size(); }

    /** Return the number of diagnostics with the requested severity. */
    [[nodiscard]] std::size_t count(Severity severity) const noexcept {
        return static_cast<std::size_t>(std::count_if(diagnostics_.begin(), diagnostics_.end(),
                                                      [severity](const Diagnostic &diagnostic) {
                                                          return diagnostic.severity() == severity;
                                                      }));
    }

  private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace volt
