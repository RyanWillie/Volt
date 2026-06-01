#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Local coordinate in a normalized footprint, measured in millimeters. */
class FootprintPoint {
  public:
    /** Construct a finite local footprint point. */
    FootprintPoint(double x_mm, double y_mm);

    /** Return the local X coordinate in millimeters. */
    [[nodiscard]] double x_mm() const noexcept { return x_mm_; }

    /** Return the local Y coordinate in millimeters. */
    [[nodiscard]] double y_mm() const noexcept { return y_mm_; }

    /** Return whether two points have the same coordinates. */
    [[nodiscard]] friend bool operator==(FootprintPoint lhs, FootprintPoint rhs) noexcept = default;

  private:
    double x_mm_;
    double y_mm_;
};

/** Two-dimensional footprint size in millimeters. */
class FootprintSize {
  public:
    /** Construct a positive finite footprint size. */
    FootprintSize(double width_mm, double height_mm);

    /** Return the width in millimeters. */
    [[nodiscard]] double width_mm() const noexcept { return width_mm_; }

    /** Return the height in millimeters. */
    [[nodiscard]] double height_mm() const noexcept { return height_mm_; }

    /** Return whether two sizes have the same dimensions. */
    [[nodiscard]] friend bool operator==(FootprintSize lhs, FootprintSize rhs) noexcept = default;

  private:
    double width_mm_;
    double height_mm_;
};

/** PCB layers a normalized footprint pad may occupy. */
enum class FootprintLayer {
    FrontCopper,
    BackCopper,
    FrontSolderMask,
    BackSolderMask,
    FrontPaste,
    BackPaste,
};

/** Deterministic set of footprint layers. */
class FootprintLayerSet {
  public:
    /** Construct a non-empty set of unique footprint layers. */
    explicit FootprintLayerSet(std::vector<FootprintLayer> layers);

    /** Return a front-side surface-mount copper/mask/paste layer set. */
    [[nodiscard]] static FootprintLayerSet front_smd();

    /** Return a back-side surface-mount copper/mask/paste layer set. */
    [[nodiscard]] static FootprintLayerSet back_smd();

    /** Return a through-hole copper/mask layer set spanning both board sides. */
    [[nodiscard]] static FootprintLayerSet through_hole();

    /** Return a mask-only layer set for non-plated mechanical holes. */
    [[nodiscard]] static FootprintLayerSet mechanical_hole();

    /** Return all layers in deterministic order. */
    [[nodiscard]] const std::vector<FootprintLayer> &layers() const noexcept { return layers_; }

    /** Return whether this set includes the requested layer. */
    [[nodiscard]] bool contains(FootprintLayer layer) const noexcept;

    /** Return whether this set is a front-side surface-mount stack. */
    [[nodiscard]] bool is_front_smd() const noexcept;

    /** Return whether this set is a back-side surface-mount stack. */
    [[nodiscard]] bool is_back_smd() const noexcept;

    /** Return whether this set is a valid surface-mount stack on either board side. */
    [[nodiscard]] bool is_surface_mount() const noexcept { return is_front_smd() || is_back_smd(); }

    /** Return whether this set is a through-hole stack. */
    [[nodiscard]] bool is_through_hole() const noexcept;

    /** Return whether this set is valid for a non-plated mechanical through-hole pad. */
    [[nodiscard]] bool is_mechanical_hole() const noexcept;

    /** Return whether two layer sets contain the same layers. */
    [[nodiscard]] friend bool operator==(const FootprintLayerSet &lhs,
                                         const FootprintLayerSet &rhs) noexcept {
        return lhs.layers_ == rhs.layers_;
    }

  private:
    std::vector<FootprintLayer> layers_;
};

/** Broad PCB pad technology. */
enum class FootprintPadKind {
    SurfaceMount,
    ThroughHole,
};

/** Basic pad shape needed for first placement and board visualization. */
enum class FootprintPadShape {
    Rectangle,
    RoundedRectangle,
    Circle,
    Oval,
};

/** Plating state for a drilled footprint pad. */
enum class FootprintPadPlating {
    Plated,
    NonPlated,
};

/** Optional non-logical mechanical purpose for a footprint pad or hole. */
enum class FootprintPadMechanicalRole {
    Mounting,
    Fiducial,
    MechanicalSupport,
};

/** Drill data for through-hole footprint pads. */
class FootprintDrill {
  public:
    /** Construct positive finite drill data. */
    FootprintDrill(double diameter_mm, FootprintPadPlating plating);

    /** Return the drill diameter in millimeters. */
    [[nodiscard]] double diameter_mm() const noexcept { return diameter_mm_; }

    /** Return whether the drill is plated. */
    [[nodiscard]] FootprintPadPlating plating() const noexcept { return plating_; }

    /** Return whether two drill definitions are the same. */
    [[nodiscard]] friend bool operator==(FootprintDrill lhs, FootprintDrill rhs) noexcept = default;

  private:
    double diameter_mm_;
    FootprintPadPlating plating_;
};

/** Normalized pad geometry inside a footprint definition. */
class FootprintPad {
  public:
    /** Construct a surface-mount pad. */
    [[nodiscard]] static FootprintPad
    surface_mount(std::string label, FootprintPadShape shape, FootprintPoint position,
                  FootprintSize size, FootprintLayerSet layers,
                  std::optional<FootprintPadMechanicalRole> mechanical_role = std::nullopt);

    /** Construct a through-hole pad with drill and plating data. */
    [[nodiscard]] static FootprintPad
    through_hole(std::string label, FootprintPadShape shape, FootprintPoint position,
                 FootprintSize size, FootprintLayerSet layers, FootprintDrill drill,
                 std::optional<FootprintPadMechanicalRole> mechanical_role = std::nullopt);

    /** Return the stable pad label inside its footprint definition. */
    [[nodiscard]] const std::string &label() const noexcept { return label_; }

    /** Return the broad pad technology. */
    [[nodiscard]] FootprintPadKind kind() const noexcept { return kind_; }

    /** Return the pad shape. */
    [[nodiscard]] FootprintPadShape shape() const noexcept { return shape_; }

    /** Return the pad local-center position. */
    [[nodiscard]] FootprintPoint position() const noexcept { return position_; }

    /** Return the pad size. */
    [[nodiscard]] FootprintSize size() const noexcept { return size_; }

    /** Return the pad layer set. */
    [[nodiscard]] const FootprintLayerSet &layers() const noexcept { return layers_; }

    /** Return optional drill and plating data. */
    [[nodiscard]] const std::optional<FootprintDrill> &drill() const noexcept { return drill_; }

    /** Return optional mechanical purpose metadata. */
    [[nodiscard]] const std::optional<FootprintPadMechanicalRole> &mechanical_role() const noexcept;

    /** Return whether this pad should have a selected-part pin mapping. */
    [[nodiscard]] bool requires_pin_mapping() const noexcept;

    /** Return whether two pads describe the same geometry. */
    [[nodiscard]] friend bool operator==(const FootprintPad &lhs,
                                         const FootprintPad &rhs) = default;

  private:
    FootprintPad(std::string label, FootprintPadKind kind, FootprintPadShape shape,
                 FootprintPoint position, FootprintSize size, FootprintLayerSet layers,
                 std::optional<FootprintDrill> drill,
                 std::optional<FootprintPadMechanicalRole> mechanical_role);

    std::string label_;
    FootprintPadKind kind_;
    FootprintPadShape shape_;
    FootprintPoint position_;
    FootprintSize size_;
    FootprintLayerSet layers_;
    std::optional<FootprintDrill> drill_;
    std::optional<FootprintPadMechanicalRole> mechanical_role_;
};

/** Normalized footprint geometry resolved from a selected physical part footprint reference. */
class FootprintDefinition {
  public:
    /** Construct a footprint definition with a library-qualified reference and unique pads. */
    FootprintDefinition(FootprintRef ref, std::vector<FootprintPad> pads);

    /** Return the library-qualified footprint reference. */
    [[nodiscard]] const FootprintRef &ref() const noexcept { return ref_; }

    /** Return all pads in stable definition order. */
    [[nodiscard]] const std::vector<FootprintPad> &pads() const noexcept { return pads_; }

    /** Return the number of pads in the definition. */
    [[nodiscard]] std::size_t pad_count() const noexcept { return pads_.size(); }

    /** Return a pad by stable footprint-local ID. */
    [[nodiscard]] const FootprintPad &pad(FootprintPadId id) const;

    /** Return the stable pad ID for a pad label, if present. */
    [[nodiscard]] std::optional<FootprintPadId> pad_id(std::string_view label) const noexcept;

    /** Return whether two footprint definitions describe the same geometry. */
    [[nodiscard]] friend bool operator==(const FootprintDefinition &lhs,
                                         const FootprintDefinition &rhs) = default;

  private:
    FootprintRef ref_;
    std::vector<FootprintPad> pads_;
};

/** Small in-memory footprint library for built-ins and tests. */
class FootprintLibrary {
  public:
    /** Add a footprint definition, rejecting duplicate footprint references. */
    void add(FootprintDefinition definition);

    /** Return a footprint definition for a reference, or null when absent. */
    [[nodiscard]] const FootprintDefinition *find(const FootprintRef &ref) const noexcept;

    /** Return all footprint definitions in deterministic insertion order. */
    [[nodiscard]] const std::vector<FootprintDefinition> &definitions() const noexcept;

    /** Return the number of footprint definitions. */
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

  private:
    std::vector<FootprintDefinition> definitions_;
};

/** Derived binding between a footprint pad and the selected physical part pin mapping. */
class FootprintPadBinding {
  public:
    /** Construct a resolved pad binding. */
    FootprintPadBinding(FootprintPadId pad, PinDefId pin);

    /** Return the resolved footprint pad ID. */
    [[nodiscard]] FootprintPadId pad() const noexcept { return pad_; }

    /** Return the logical pin definition mapped to the pad. */
    [[nodiscard]] PinDefId pin() const noexcept { return pin_; }

  private:
    FootprintPadId pad_;
    PinDefId pin_;
};

/** Result of resolving selected physical part footprint data against a footprint library. */
class FootprintResolution {
  public:
    /** Construct a footprint resolution result. */
    FootprintResolution(std::optional<FootprintDefinition> definition,
                        std::vector<FootprintPadBinding> pad_bindings,
                        DiagnosticReport diagnostics);

    /** Return whether resolution found geometry and emitted no errors. */
    [[nodiscard]] bool ok() const noexcept;

    /** Return the resolved footprint definition, or null when lookup failed. */
    [[nodiscard]] const FootprintDefinition *definition() const noexcept;

    /** Return derived pad-to-pin bindings in footprint pad order. */
    [[nodiscard]] const std::vector<FootprintPadBinding> &pad_bindings() const noexcept;

    /** Return diagnostics produced while resolving the footprint. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

  private:
    std::optional<FootprintDefinition> definition_;
    std::vector<FootprintPadBinding> pad_bindings_;
    DiagnosticReport diagnostics_;
};

namespace detail {

[[nodiscard]] Diagnostic footprint_diagnostic(DiagnosticCode code, std::string message);

[[nodiscard]] std::string footprint_ref_label(const FootprintRef &ref);

[[nodiscard]] std::string pin_def_label(PinDefId pin);

[[nodiscard]] FootprintPad
front_smd_pad(std::string label, double x_mm, double y_mm, double width_mm, double height_mm,
              FootprintPadShape shape = FootprintPadShape::RoundedRectangle);

[[nodiscard]] FootprintPad plated_header_pad(std::string label, double x_mm, double y_mm);

[[nodiscard]] FootprintPad plated_round_pad(std::string label, double x_mm, double y_mm,
                                            double diameter_mm, double drill_mm);

[[nodiscard]] FootprintPad mechanical_hole_pad(std::string label, double x_mm, double y_mm,
                                               double diameter_mm, double drill_mm,
                                               FootprintPadMechanicalRole role);

[[nodiscard]] FootprintDefinition two_terminal_smd_footprint(FootprintRef ref, double pad_span_mm,
                                                             double pad_width_mm,
                                                             double pad_height_mm);

[[nodiscard]] FootprintDefinition
single_row_header_footprint(FootprintRef ref, std::size_t pin_count, double pitch_mm);

[[nodiscard]] FootprintDefinition dual_row_header_footprint(FootprintRef ref,
                                                            std::size_t pins_per_row,
                                                            double row_spacing_mm, double pitch_mm);

[[nodiscard]] FootprintDefinition two_side_smd_package(FootprintRef ref, std::size_t pin_count,
                                                       double row_center_x_mm, double pitch_mm,
                                                       double pad_width_mm, double pad_height_mm);

[[nodiscard]] FootprintDefinition qfp_footprint(FootprintRef ref, std::size_t pin_count,
                                                double side_center_mm, double pitch_mm,
                                                double pad_length_mm, double pad_width_mm);

} // namespace detail

/** Return a simple 0603 resistor footprint fixture. */
[[nodiscard]] FootprintDefinition passive_0603_footprint();

/** Return a simple 0402 resistor footprint fixture. */
[[nodiscard]] FootprintDefinition resistor_0402_footprint();

/** Return a simple 0805 resistor footprint fixture. */
[[nodiscard]] FootprintDefinition resistor_0805_footprint();

/** Return a simple 0603 capacitor footprint fixture. */
[[nodiscard]] FootprintDefinition capacitor_0603_footprint();

/** Return a simple 0603 inductor/ferrite footprint fixture. */
[[nodiscard]] FootprintDefinition inductor_0603_footprint();

/** Return a simple 0603 LED footprint fixture. */
[[nodiscard]] FootprintDefinition led_0603_footprint();

/** Return a simple 0603 diode footprint fixture. */
[[nodiscard]] FootprintDefinition diode_0603_footprint();

/** Return a simple SOD-123 diode footprint fixture. */
[[nodiscard]] FootprintDefinition diode_sod_123_footprint();

/** Return a simple 1x02 2.54 mm through-hole header footprint fixture. */
[[nodiscard]] FootprintDefinition header_1x02_footprint();

/** Return a simple 1x04 2.54 mm through-hole header footprint fixture. */
[[nodiscard]] FootprintDefinition header_1x04_footprint();

/** Return a compact 2x05 1.27 mm through-hole programming header footprint fixture. */
[[nodiscard]] FootprintDefinition header_2x05_127_footprint();

/** Return a simple two-pin 5.08 mm power terminal block footprint fixture. */
[[nodiscard]] FootprintDefinition terminal_block_1x02_footprint();

/** Return a practical micro USB-B receptacle footprint fixture with mechanical holes. */
[[nodiscard]] FootprintDefinition micro_usb_b_footprint();

/** Return a simple SOT-23 footprint fixture. */
[[nodiscard]] FootprintDefinition sot_23_footprint();

/** Return a simple SOT-23-6 footprint fixture. */
[[nodiscard]] FootprintDefinition sot_23_6_footprint();

/** Return a simplified SOT-223-3 footprint fixture for first power-regulator boards. */
[[nodiscard]] FootprintDefinition sot_223_3_footprint();

/** Return a small IC-style SOIC-8 footprint fixture for first placement tests. */
[[nodiscard]] FootprintDefinition soic_8_footprint();

/** Return a Package_SO alias for SOIC-8 selected-part references. */
[[nodiscard]] FootprintDefinition package_so_soic_8_footprint();

/** Return a simple TSSOP-14 footprint fixture. */
[[nodiscard]] FootprintDefinition tssop_14_footprint();

/** Return the LQFP-64 MCU footprint used by the STM32 benchmark library. */
[[nodiscard]] FootprintDefinition lqfp_64_footprint();

/** Return the built-in footprint library used by first real-board examples and tests. */
[[nodiscard]] FootprintLibrary builtin_footprint_library();

/** Resolve a selected physical part's footprint reference and pin-pad mappings. */
[[nodiscard]] FootprintResolution resolve_footprint(const PhysicalPart &part,
                                                    const FootprintLibrary &library);

} // namespace volt
