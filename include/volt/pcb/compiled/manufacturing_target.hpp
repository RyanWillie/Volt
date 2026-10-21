#pragma once
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <volt/circuit/bom/bom.hpp>
#include <volt/library/selected_part_ratings.hpp>
#include <volt/pcb/assembly/cpl.hpp>
#include <volt/pcb/compiled/board_completion.hpp>

namespace volt {
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingGoal { Fabrication, FabricationAndAssembly };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingFamily { RigidFr4, Flex };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingMaterial { Fr4Tg135, OtherFr4 };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingFinish { LeadFreeHasl, Enig };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingMaskSides { Both, SingleSide, NoMask };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingMaskColour { Green, OtherColour };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingViaTreatment { Exposed, Tented, Filled };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingDrilling { MechanicalThrough, Laser };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingHoleTreatment { RoundOnly, BlindBuried };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingSlotTreatment { NoSlots, Routed, Plated };
/** Finite target input; alternatives outside V1 support are reported explicitly. */
enum class ManufacturingAssembly { NoAssembly, SmtAndThroughHole, OtherAssembly };

/** Explicit requested process; absent fields never acquire manufacturing defaults. */
struct ManufacturingProcess {
    /** Explicit family selection. */
    std::optional<ManufacturingFamily> family;
    /** Explicit material selection. */
    std::optional<ManufacturingMaterial> material;
    /** Explicit copper layers selection. */
    std::optional<unsigned int> copper_layers;
    /** Explicit top copper weight oz selection. */
    std::optional<double> top_copper_weight_oz;
    /** Explicit bottom copper weight oz selection. */
    std::optional<double> bottom_copper_weight_oz;
    /** Explicit thickness mm selection. */
    std::optional<double> thickness_mm;
    /** Explicit finish selection. */
    std::optional<ManufacturingFinish> finish;
    /** Explicit mask sides selection. */
    std::optional<ManufacturingMaskSides> mask_sides;
    /** Explicit mask colour selection. */
    std::optional<ManufacturingMaskColour> mask_colour;
    /** Explicit via treatment selection. */
    std::optional<ManufacturingViaTreatment> via_treatment;
    /** Explicit drilling selection. */
    std::optional<ManufacturingDrilling> drilling;
    /** Explicit holes selection. */
    std::optional<ManufacturingHoleTreatment> holes;
    /** Explicit slots selection. */
    std::optional<ManufacturingSlotTreatment> slots;
    /** Explicit assembly selection. */
    std::optional<ManufacturingAssembly> assembly;
    /** Explicit dimension x mm selection. */
    std::optional<double> dimension_x_mm;
    /** Explicit dimension y mm selection. */
    std::optional<double> dimension_y_mm;
};

/** Owned pinned numeric policy data; no runtime source lookup. */
class ManufacturingProfile final {
  public:
    /** Validate finite process data and optional positive inclusive limits. */
    ManufacturingProfile(BoardCapabilityProfile capability, ContentHash source_content,
                         ManufacturingProcess applicability,
                         std::optional<BoardCapabilityRange> dimension_x_mm = std::nullopt,
                         std::optional<BoardCapabilityRange> dimension_y_mm = std::nullopt,
                         std::optional<double> minimum_via_radial_annulus_mm = std::nullopt);

    /** Return the complete immutable native capability snapshot. */
    [[nodiscard]] const BoardCapabilityProfile &capability() const noexcept { return capability_; }

    /** Return pinned captured-source content identity. */
    [[nodiscard]] const ContentHash &source_content() const noexcept { return source_content_; }

    /** Return exact process applicability, including explicit selections. */
    [[nodiscard]] const ManufacturingProcess &applicability() const noexcept {
        return applicability_;
    }

    /** Return inclusive X extent limits in millimetres. */
    [[nodiscard]] std::optional<BoardCapabilityRange> dimension_x_mm() const noexcept {
        return dimension_x_mm_;
    }

    /** Return inclusive Y extent limits in millimetres. */
    [[nodiscard]] std::optional<BoardCapabilityRange> dimension_y_mm() const noexcept {
        return dimension_y_mm_;
    }

    /** Return minimum radial ring width, distinct from outer copper diameter. */
    [[nodiscard]] std::optional<double> minimum_via_radial_annulus_mm() const noexcept {
        return minimum_via_radial_annulus_mm_;
    }

    /** Return domain-separated canonical semantic profile identity. */
    [[nodiscard]] ContentHash digest() const;

  private:
    BoardCapabilityProfile capability_;
    ContentHash source_content_;
    ManufacturingProcess applicability_;
    std::optional<BoardCapabilityRange> dimension_x_mm_;
    std::optional<BoardCapabilityRange> dimension_y_mm_;
    std::optional<double> minimum_via_radial_annulus_mm_;
};

/** Immutable exact-artifact target; canonical native CPL options bind assembly projections. */
class ManufacturingTarget final {
  public:
    /** Validate typed inputs, canonicalize CPL options and reject duplicate footprint keys. */
    ManufacturingTarget(CompiledBoardIdentity source, ContentHash content_digest,
                        ManufacturingGoal goal, ManufacturingProcess process,
                        ManufacturingProfile profile,
                        std::optional<CplProjectionOptions> cpl_options = std::nullopt);

    /** Return the exact artifact identity this target requests. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return the exact artifact byte digest. */
    [[nodiscard]] const ContentHash &content_digest() const noexcept { return content_digest_; }

    /** Return the requested fabrication or assembly goal. */
    [[nodiscard]] ManufacturingGoal goal() const noexcept { return goal_; }

    /** Return explicit requested selections. */
    [[nodiscard]] const ManufacturingProcess &process() const noexcept { return process_; }

    /** Return pinned process/numeric/source profile. */
    [[nodiscard]] const ManufacturingProfile &profile() const noexcept { return profile_; }

    /** Return canonical native CPL options; absent differs from explicitly empty. */
    [[nodiscard]] const std::optional<CplProjectionOptions> &cpl_options() const noexcept {
        return cpl_options_;
    }

    /** Return domain-separated canonical target identity under policy V1. */
    [[nodiscard]] ContentHash digest() const;

  private:
    CompiledBoardIdentity source_;
    ContentHash content_digest_;
    ManufacturingGoal goal_;
    ManufacturingProcess process_;
    ManufacturingProfile profile_;
    std::optional<CplProjectionOptions> cpl_options_;
};

/** Closed required inventory in stable policy order. */
enum class ManufacturingCheck {
    ArtifactIdentity,
    ProcessSelections,
    ProfileApplicability,
    LogicalContinuity,
    RequiredInventory,
    RequiredContacts,
    PhysicalConnectivity,
    UnintendedContacts,
    PhysicalGeometry,
    ExportFidelity,
    CopperWidth,
    TrackTrackSpacing,
    TrackPadSpacing,
    TrackViaSpacing,
    TrackZoneSpacing,
    PadPadSpacing,
    PadViaSpacing,
    PadZoneSpacing,
    ViaViaSpacing,
    ViaZoneSpacing,
    ZoneZoneSpacing,
    ViaDrill,
    ViaCopperDiameter,
    ViaRadialAnnulus,
    PadPthAnnulus,
    PthDrillRange,
    NpthDrillRange,
    DrillSeparation,
    SlotWidth,
    SlotLength,
    SlotTreatment,
    OutlineClearance,
    MechanicalClearance,
    DimensionX,
    DimensionY,
    CopperLayerCount,
    BoardThickness,
    TopCopperWeight,
    BottomCopperWeight,
    MaterialRealization,
    FinishRealization,
    MaskOpenings,
    MaskCopperClearance,
    MaskWeb,
    MaskSliver,
    ViaCovering,
    LegendHeight,
    LegendStroke,
    LegendPadClearance,
    LegendMaskClearance,
    LegendClipping,
    RequiredPinOne,
    RequiredPolarity,
    PartPackageReview,
    LandPatternReview,
    PinMapReview,
    AssemblyOrientation,
    PopulatedQuantities,
    DnpQuantities,
    SelectedAlternates,
    CplIdentity,
    CplSide,
    CplRotation,
    SelectedPartRatings,
    AssemblyProcess,
    Count
};
/** State of one required leaf; reviewed passes require checked native IO composition. */
enum class ManufacturingState { NativePass, Failed, NotEvaluated, NotApplicable, ReviewedPass };
/** Policy V1 deliberately cannot establish aggregate manufacturing readiness. */
enum class ManufacturingStatus { Blocked, UnsupportedTarget };
/** Native enforcement or explicitly deferred owner of a required check. */
enum class ManufacturingOwner {
    NativeTarget,
    NativeCompletion,
    DeferredNative,
    DeferredReview,
    DeferredIO,
    NativeAssembly,
    NativeRatings,
    NativeIO,
    HumanReview
};
/** Stable blocker family, qualified by the closed leaf key and exact scope. */
enum class ManufacturingBlocker {
    None,
    MissingInput,
    MissingCoverage,
    ConstraintFailed,
    UnsupportedSelection,
    IdentityMismatch,
    InvalidEvidence
};
/** Measurement units, never inferred from numeric values. */
enum class ManufacturingUnit { Millimetres, OuncesPerSquareFoot, Count, Degrees };

/** One ordered actual measurement with its exact compound reporting scope. */
struct ManufacturingObservation {
    /** Reporting-only exact identity tuple. */
    std::vector<EntityRef> entities;
    /** Observed value in the declared unit. */
    double value;
    /** Inclusive lower bound if known. */
    std::optional<double> minimum;
    /** Inclusive upper bound if known. */
    std::optional<double> maximum;
    /** Explicit target selection, independently required to match if present. */
    std::optional<double> selected;
    /** Typed unit shared by the values. */
    ManufacturingUnit unit;
    /** Outcome of this observation, independent of any other measurement gap. */
    ManufacturingState state;
};

/** One fixed leaf; public values are returned only through const result spans. */
struct ManufacturingCoverageRow {
    /** Closed policy key. */
    ManufacturingCheck check;
    /** Native-derived state. */
    ManufacturingState state;
    /** Owner of implemented or missing evaluation. */
    ManufacturingOwner owner;
    /** Policy-owned goal applicability. */
    bool applicable;
    /** Stable blocker family. */
    ManufacturingBlocker blocker;
    /** Exact explanation of the state. */
    std::string reason;
    /** Action needed to resolve a blocker. */
    std::string remediation;
    /** Sorted unique row scope; count is entities.size(). */
    std::vector<EntityRef> entities;
    /** Ordered measurements; count is observations.size(). */
    std::vector<ManufacturingObservation> observations;
};

/** Return stable closed key spelling. */
[[nodiscard]] std::string_view manufacturing_check_name(ManufacturingCheck check);

class ManufacturingCoverage;

namespace io {
struct ManufacturingEvidenceInput;
class ManufacturingEvaluation;
[[nodiscard]] ManufacturingEvaluation
evaluate_manufacturing_export(const CompiledBoard &, const ManufacturingTarget &,
                              const ManufacturingEvidenceInput &);
} // namespace io

/** Owned read-only report; required leaves cannot be removed or overwritten. */
class ManufacturingCoverage final {
  public:
    /** Return exact underlying native physical facts and untouched diagnostics/provenance. */
    [[nodiscard]] const BoardCompletion &completion() const noexcept { return completion_; }

    /** Return native BOM evidence when exact supported assembly evaluation ran. */
    [[nodiscard]] const std::optional<Bom> &bom() const noexcept { return bom_; }

    /** Return native CPL evidence using the exact target options. */
    [[nodiscard]] const std::optional<Cpl> &cpl() const noexcept { return cpl_; }

    /** Return explicit native selected-part rating obligations under either goal. */
    [[nodiscard]] const std::optional<SelectedPartRatings> &ratings() const noexcept {
        return ratings_;
    }

    /** Return exact target digest. */
    [[nodiscard]] const ContentHash &target_digest() const noexcept { return target_digest_; }

    /** Return exact pinned profile digest. */
    [[nodiscard]] const ContentHash &profile_digest() const noexcept { return profile_digest_; }

    /** Return the complete immutable target, including profile source provenance. */
    [[nodiscard]] const ManufacturingTarget &target() const noexcept { return target_; }

    /** Return Blocked or UnsupportedTarget; V1 has no Ready value. */
    [[nodiscard]] ManufacturingStatus status() const noexcept { return status_; }

    /** Policy V1 has required unevaluated checks under every goal. */
    [[nodiscard]] bool ready() const noexcept { return false; }

    /** Return every required leaf in closed policy order. */
    [[nodiscard]] std::span<const ManufacturingCoverageRow> coverage() const noexcept {
        return coverage_;
    }

    /** Report contract: native-only V2, explicitly composed evidence V3. */
    [[nodiscard]] unsigned int evaluator_version() const noexcept { return evaluator_version_; }

  private:
    friend io::ManufacturingEvaluation
    io::evaluate_manufacturing_export(const CompiledBoard &, const ManufacturingTarget &,
                                      const io::ManufacturingEvidenceInput &);
    friend ManufacturingCoverage evaluate_manufacturing_coverage(const CompiledBoard &,
                                                                 const ManufacturingTarget &);
    ManufacturingCoverage(const CompiledBoard &compiled, const ManufacturingTarget &target);
    unsigned int evaluator_version_ = 2;
    BoardCompletion completion_;
    std::optional<Bom> bom_;
    std::optional<Cpl> cpl_;
    std::optional<SelectedPartRatings> ratings_;
    ManufacturingTarget target_;
    ContentHash target_digest_;
    ContentHash profile_digest_;
    ManufacturingStatus status_ = ManufacturingStatus::Blocked;
    std::array<ManufacturingCoverageRow, static_cast<std::size_t>(ManufacturingCheck::Count)>
        coverage_;
};

/** Evaluate one immutable artifact without filesystem access, overrides or source callbacks. */
[[nodiscard]] ManufacturingCoverage
evaluate_manufacturing_coverage(const CompiledBoard &compiled, const ManufacturingTarget &target);
} // namespace volt
