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
    FootprintPoint(double x_mm, double y_mm) : x_mm_{x_mm}, y_mm_{y_mm} {
        if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
            throw std::invalid_argument{"Footprint point coordinates must be finite"};
        }
    }

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
    FootprintSize(double width_mm, double height_mm) : width_mm_{width_mm}, height_mm_{height_mm} {
        if (!std::isfinite(width_mm_) || !std::isfinite(height_mm_)) {
            throw std::invalid_argument{"Footprint size dimensions must be finite"};
        }
        if (width_mm_ <= 0.0 || height_mm_ <= 0.0) {
            throw std::invalid_argument{"Footprint size dimensions must be positive"};
        }
    }

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
    explicit FootprintLayerSet(std::vector<FootprintLayer> layers) : layers_{std::move(layers)} {
        if (layers_.empty()) {
            throw std::invalid_argument{"Footprint layer set must not be empty"};
        }

        std::sort(layers_.begin(), layers_.end());
        const auto duplicate = std::adjacent_find(layers_.begin(), layers_.end());
        if (duplicate != layers_.end()) {
            throw std::invalid_argument{"Footprint layer set must not contain duplicate layers"};
        }
    }

    /** Return a front-side surface-mount copper/mask/paste layer set. */
    [[nodiscard]] static FootprintLayerSet front_smd() {
        return FootprintLayerSet{std::vector{FootprintLayer::FrontCopper,
                                             FootprintLayer::FrontSolderMask,
                                             FootprintLayer::FrontPaste}};
    }

    /** Return a back-side surface-mount copper/mask/paste layer set. */
    [[nodiscard]] static FootprintLayerSet back_smd() {
        return FootprintLayerSet{std::vector{
            FootprintLayer::BackCopper, FootprintLayer::BackSolderMask, FootprintLayer::BackPaste}};
    }

    /** Return a through-hole copper/mask layer set spanning both board sides. */
    [[nodiscard]] static FootprintLayerSet through_hole() {
        return FootprintLayerSet{
            std::vector{FootprintLayer::FrontCopper, FootprintLayer::BackCopper,
                        FootprintLayer::FrontSolderMask, FootprintLayer::BackSolderMask}};
    }

    /** Return a mask-only layer set for non-plated mechanical holes. */
    [[nodiscard]] static FootprintLayerSet mechanical_hole() {
        return FootprintLayerSet{
            std::vector{FootprintLayer::FrontSolderMask, FootprintLayer::BackSolderMask}};
    }

    /** Return all layers in deterministic order. */
    [[nodiscard]] const std::vector<FootprintLayer> &layers() const noexcept { return layers_; }

    /** Return whether this set includes the requested layer. */
    [[nodiscard]] bool contains(FootprintLayer layer) const noexcept {
        return std::find(layers_.begin(), layers_.end(), layer) != layers_.end();
    }

    /** Return whether this set is a front-side surface-mount stack. */
    [[nodiscard]] bool is_front_smd() const noexcept {
        return layers_.size() == 3 && layers_[0] == FootprintLayer::FrontCopper &&
               layers_[1] == FootprintLayer::FrontSolderMask &&
               layers_[2] == FootprintLayer::FrontPaste;
    }

    /** Return whether this set is a back-side surface-mount stack. */
    [[nodiscard]] bool is_back_smd() const noexcept {
        return layers_.size() == 3 && layers_[0] == FootprintLayer::BackCopper &&
               layers_[1] == FootprintLayer::BackSolderMask &&
               layers_[2] == FootprintLayer::BackPaste;
    }

    /** Return whether this set is a valid surface-mount stack on either board side. */
    [[nodiscard]] bool is_surface_mount() const noexcept { return is_front_smd() || is_back_smd(); }

    /** Return whether this set is a through-hole stack. */
    [[nodiscard]] bool is_through_hole() const noexcept {
        return layers_.size() == 4 && layers_[0] == FootprintLayer::FrontCopper &&
               layers_[1] == FootprintLayer::BackCopper &&
               layers_[2] == FootprintLayer::FrontSolderMask &&
               layers_[3] == FootprintLayer::BackSolderMask;
    }

    /** Return whether this set is valid for a non-plated mechanical through-hole pad. */
    [[nodiscard]] bool is_mechanical_hole() const noexcept {
        return layers_.size() == 2 && layers_[0] == FootprintLayer::FrontSolderMask &&
               layers_[1] == FootprintLayer::BackSolderMask;
    }

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
    FootprintDrill(double diameter_mm, FootprintPadPlating plating)
        : diameter_mm_{diameter_mm}, plating_{plating} {
        if (!std::isfinite(diameter_mm_)) {
            throw std::invalid_argument{"Footprint drill diameter must be finite"};
        }
        if (diameter_mm_ <= 0.0) {
            throw std::invalid_argument{"Footprint drill diameter must be positive"};
        }
    }

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
                  std::optional<FootprintPadMechanicalRole> mechanical_role = std::nullopt) {
        return FootprintPad{std::move(label),
                            FootprintPadKind::SurfaceMount,
                            shape,
                            position,
                            size,
                            std::move(layers),
                            std::nullopt,
                            mechanical_role};
    }

    /** Construct a through-hole pad with drill and plating data. */
    [[nodiscard]] static FootprintPad
    through_hole(std::string label, FootprintPadShape shape, FootprintPoint position,
                 FootprintSize size, FootprintLayerSet layers, FootprintDrill drill,
                 std::optional<FootprintPadMechanicalRole> mechanical_role = std::nullopt) {
        return FootprintPad{std::move(label),
                            FootprintPadKind::ThroughHole,
                            shape,
                            position,
                            size,
                            std::move(layers),
                            drill,
                            mechanical_role};
    }

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
    [[nodiscard]] const std::optional<FootprintPadMechanicalRole> &
    mechanical_role() const noexcept {
        return mechanical_role_;
    }

    /** Return whether this pad should have a selected-part pin mapping. */
    [[nodiscard]] bool requires_pin_mapping() const noexcept {
        return !mechanical_role_.has_value();
    }

  private:
    FootprintPad(std::string label, FootprintPadKind kind, FootprintPadShape shape,
                 FootprintPoint position, FootprintSize size, FootprintLayerSet layers,
                 std::optional<FootprintDrill> drill,
                 std::optional<FootprintPadMechanicalRole> mechanical_role)
        : label_{std::move(label)}, kind_{kind}, shape_{shape}, position_{position}, size_{size},
          layers_{std::move(layers)}, drill_{drill}, mechanical_role_{mechanical_role} {
        if (label_.empty()) {
            throw std::invalid_argument{"Footprint pad label must not be empty"};
        }
        if (kind_ == FootprintPadKind::SurfaceMount && !layers_.is_surface_mount()) {
            throw std::invalid_argument{"Surface-mount footprint pads must use SMD layers"};
        }
        if (kind_ == FootprintPadKind::ThroughHole && !drill_.has_value()) {
            throw std::invalid_argument{"Through-hole footprint pads must include drill data"};
        }
        if (kind_ == FootprintPadKind::ThroughHole &&
            drill_->plating() == FootprintPadPlating::NonPlated && !mechanical_role_.has_value()) {
            throw std::invalid_argument{
                "Non-plated through-hole footprint pads must declare a mechanical role"};
        }
        if (kind_ == FootprintPadKind::ThroughHole &&
            drill_->plating() == FootprintPadPlating::NonPlated && !layers_.is_mechanical_hole()) {
            throw std::invalid_argument{
                "Non-plated mechanical through-hole footprint pads must use mechanical-hole "
                "layers"};
        }
        if (kind_ == FootprintPadKind::ThroughHole &&
            drill_->plating() == FootprintPadPlating::Plated && !layers_.is_through_hole()) {
            throw std::invalid_argument{"Through-hole footprint pads must use through-hole layers"};
        }
    }

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
    FootprintDefinition(FootprintRef ref, std::vector<FootprintPad> pads)
        : ref_{std::move(ref)}, pads_{std::move(pads)} {
        if (pads_.empty()) {
            throw std::invalid_argument{"Footprint definition must contain at least one pad"};
        }

        for (auto current = pads_.begin(); current != pads_.end(); ++current) {
            const auto duplicate =
                std::any_of(std::next(current), pads_.end(), [current](const FootprintPad &pad) {
                    return pad.label() == current->label();
                });
            if (duplicate) {
                throw std::invalid_argument{"Footprint definition contains duplicate pad labels"};
            }
        }
    }

    /** Return the library-qualified footprint reference. */
    [[nodiscard]] const FootprintRef &ref() const noexcept { return ref_; }

    /** Return all pads in stable definition order. */
    [[nodiscard]] const std::vector<FootprintPad> &pads() const noexcept { return pads_; }

    /** Return the number of pads in the definition. */
    [[nodiscard]] std::size_t pad_count() const noexcept { return pads_.size(); }

    /** Return a pad by stable footprint-local ID. */
    [[nodiscard]] const FootprintPad &pad(FootprintPadId id) const {
        if (id.index() >= pads_.size()) {
            throw std::out_of_range{"Footprint pad id is out of range"};
        }
        return pads_[id.index()];
    }

    /** Return the stable pad ID for a pad label, if present. */
    [[nodiscard]] std::optional<FootprintPadId> pad_id(std::string_view label) const noexcept {
        for (std::size_t index = 0; index < pads_.size(); ++index) {
            if (pads_[index].label() == label) {
                return FootprintPadId{index};
            }
        }
        return std::nullopt;
    }

  private:
    FootprintRef ref_;
    std::vector<FootprintPad> pads_;
};

/** Small in-memory footprint library for built-ins and tests. */
class FootprintLibrary {
  public:
    /** Add a footprint definition, rejecting duplicate footprint references. */
    void add(FootprintDefinition definition) {
        if (find(definition.ref()) != nullptr) {
            throw std::invalid_argument{"Footprint library already contains this footprint"};
        }
        definitions_.push_back(std::move(definition));
    }

    /** Return a footprint definition for a reference, or null when absent. */
    [[nodiscard]] const FootprintDefinition *find(const FootprintRef &ref) const noexcept {
        const auto match = std::find_if(
            definitions_.begin(), definitions_.end(),
            [&ref](const FootprintDefinition &definition) { return definition.ref() == ref; });
        if (match == definitions_.end()) {
            return nullptr;
        }
        return &*match;
    }

    /** Return all footprint definitions in deterministic insertion order. */
    [[nodiscard]] const std::vector<FootprintDefinition> &definitions() const noexcept {
        return definitions_;
    }

    /** Return the number of footprint definitions. */
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

  private:
    std::vector<FootprintDefinition> definitions_;
};

/** Derived binding between a footprint pad and the selected physical part pin mapping. */
class FootprintPadBinding {
  public:
    /** Construct a resolved pad binding. */
    FootprintPadBinding(FootprintPadId pad, PinDefId pin) : pad_{pad}, pin_{pin} {}

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
                        std::vector<FootprintPadBinding> pad_bindings, DiagnosticReport diagnostics)
        : definition_{std::move(definition)}, pad_bindings_{std::move(pad_bindings)},
          diagnostics_{std::move(diagnostics)} {}

    /** Return whether resolution found geometry and emitted no errors. */
    [[nodiscard]] bool ok() const noexcept {
        return definition_.has_value() && !diagnostics_.has_errors();
    }

    /** Return the resolved footprint definition, or null when lookup failed. */
    [[nodiscard]] const FootprintDefinition *definition() const noexcept {
        if (!definition_.has_value()) {
            return nullptr;
        }
        return &definition_.value();
    }

    /** Return derived pad-to-pin bindings in footprint pad order. */
    [[nodiscard]] const std::vector<FootprintPadBinding> &pad_bindings() const noexcept {
        return pad_bindings_;
    }

    /** Return diagnostics produced while resolving the footprint. */
    [[nodiscard]] const DiagnosticReport &diagnostics() const noexcept { return diagnostics_; }

  private:
    std::optional<FootprintDefinition> definition_;
    std::vector<FootprintPadBinding> pad_bindings_;
    DiagnosticReport diagnostics_;
};

namespace detail {

[[nodiscard]] inline Diagnostic footprint_diagnostic(DiagnosticCode code, std::string message) {
    return Diagnostic{Severity::Error, std::move(code), std::move(message)};
}

[[nodiscard]] inline std::string footprint_ref_label(const FootprintRef &ref) {
    return ref.library() + ":" + ref.name();
}

[[nodiscard]] inline std::string pin_def_label(PinDefId pin) {
    return "pin_def:" + std::to_string(pin.index());
}

[[nodiscard]] inline FootprintPad
front_smd_pad(std::string label, double x_mm, double y_mm, double width_mm, double height_mm,
              FootprintPadShape shape = FootprintPadShape::RoundedRectangle) {
    return FootprintPad::surface_mount(std::move(label), shape, FootprintPoint{x_mm, y_mm},
                                       FootprintSize{width_mm, height_mm},
                                       FootprintLayerSet::front_smd());
}

[[nodiscard]] inline FootprintPad plated_header_pad(std::string label, double x_mm, double y_mm) {
    return FootprintPad::through_hole(std::move(label), FootprintPadShape::Circle,
                                      FootprintPoint{x_mm, y_mm}, FootprintSize{1.70, 1.70},
                                      FootprintLayerSet::through_hole(),
                                      FootprintDrill{1.00, FootprintPadPlating::Plated});
}

[[nodiscard]] inline FootprintPad plated_round_pad(std::string label, double x_mm, double y_mm,
                                                   double diameter_mm, double drill_mm) {
    return FootprintPad::through_hole(
        std::move(label), FootprintPadShape::Circle, FootprintPoint{x_mm, y_mm},
        FootprintSize{diameter_mm, diameter_mm}, FootprintLayerSet::through_hole(),
        FootprintDrill{drill_mm, FootprintPadPlating::Plated});
}

[[nodiscard]] inline FootprintPad mechanical_hole_pad(std::string label, double x_mm, double y_mm,
                                                      double diameter_mm, double drill_mm,
                                                      FootprintPadMechanicalRole role) {
    return FootprintPad::through_hole(
        std::move(label), FootprintPadShape::Circle, FootprintPoint{x_mm, y_mm},
        FootprintSize{diameter_mm, diameter_mm}, FootprintLayerSet::mechanical_hole(),
        FootprintDrill{drill_mm, FootprintPadPlating::NonPlated}, role);
}

[[nodiscard]] inline FootprintDefinition two_terminal_smd_footprint(FootprintRef ref,
                                                                    double pad_span_mm,
                                                                    double pad_width_mm,
                                                                    double pad_height_mm) {
    const auto half_span = pad_span_mm / 2.0;
    return FootprintDefinition{
        std::move(ref),
        std::vector{front_smd_pad("1", -half_span, 0.0, pad_width_mm, pad_height_mm),
                    front_smd_pad("2", half_span, 0.0, pad_width_mm, pad_height_mm)}};
}

[[nodiscard]] inline FootprintDefinition
single_row_header_footprint(FootprintRef ref, std::size_t pin_count, double pitch_mm) {
    auto pads = std::vector<FootprintPad>{};
    pads.reserve(pin_count);
    const auto first_y = -static_cast<double>(pin_count - 1U) * pitch_mm / 2.0;
    for (std::size_t index = 0; index < pin_count; ++index) {
        pads.push_back(plated_header_pad(std::to_string(index + 1U), 0.0,
                                         first_y + static_cast<double>(index) * pitch_mm));
    }
    return FootprintDefinition{std::move(ref), std::move(pads)};
}

[[nodiscard]] inline FootprintDefinition dual_row_header_footprint(FootprintRef ref,
                                                                   std::size_t pins_per_row,
                                                                   double row_spacing_mm,
                                                                   double pitch_mm) {
    auto pads = std::vector<FootprintPad>{};
    pads.reserve(pins_per_row * 2U);
    const auto first_y = -static_cast<double>(pins_per_row - 1U) * pitch_mm / 2.0;
    const auto left_x = -row_spacing_mm / 2.0;
    const auto right_x = row_spacing_mm / 2.0;
    for (std::size_t row = 0; row < pins_per_row; ++row) {
        const auto y = first_y + static_cast<double>(row) * pitch_mm;
        pads.push_back(plated_header_pad(std::to_string((row * 2U) + 1U), left_x, y));
        pads.push_back(plated_header_pad(std::to_string((row * 2U) + 2U), right_x, y));
    }
    return FootprintDefinition{std::move(ref), std::move(pads)};
}

[[nodiscard]] inline FootprintDefinition
two_side_smd_package(FootprintRef ref, std::size_t pin_count, double row_center_x_mm,
                     double pitch_mm, double pad_width_mm, double pad_height_mm) {
    if (pin_count == 0U || pin_count % 2U != 0U) {
        throw std::invalid_argument{"Two-side SMD package footprints need an even pin count"};
    }

    auto pads = std::vector<FootprintPad>{};
    pads.reserve(pin_count);
    const auto pins_per_side = pin_count / 2U;
    const auto first_y = static_cast<double>(pins_per_side - 1U) * pitch_mm / 2.0;
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string(index + 1U), -row_center_x_mm,
                                     first_y - static_cast<double>(index) * pitch_mm, pad_width_mm,
                                     pad_height_mm));
    }
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string(pins_per_side + index + 1U), row_center_x_mm,
                                     -first_y + static_cast<double>(index) * pitch_mm, pad_width_mm,
                                     pad_height_mm));
    }
    return FootprintDefinition{std::move(ref), std::move(pads)};
}

[[nodiscard]] inline FootprintDefinition qfp_footprint(FootprintRef ref, std::size_t pin_count,
                                                       double side_center_mm, double pitch_mm,
                                                       double pad_length_mm, double pad_width_mm) {
    if (pin_count == 0U || pin_count % 4U != 0U) {
        throw std::invalid_argument{"QFP footprints need a pin count divisible by four"};
    }

    auto pads = std::vector<FootprintPad>{};
    pads.reserve(pin_count);
    const auto pins_per_side = pin_count / 4U;
    const auto first = static_cast<double>(pins_per_side - 1U) * pitch_mm / 2.0;
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string(index + 1U), -side_center_mm,
                                     first - static_cast<double>(index) * pitch_mm, pad_length_mm,
                                     pad_width_mm));
    }
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string(pins_per_side + index + 1U),
                                     -first + static_cast<double>(index) * pitch_mm,
                                     -side_center_mm, pad_width_mm, pad_length_mm));
    }
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string((pins_per_side * 2U) + index + 1U),
                                     side_center_mm, -first + static_cast<double>(index) * pitch_mm,
                                     pad_length_mm, pad_width_mm));
    }
    for (std::size_t index = 0; index < pins_per_side; ++index) {
        pads.push_back(front_smd_pad(std::to_string((pins_per_side * 3U) + index + 1U),
                                     first - static_cast<double>(index) * pitch_mm, side_center_mm,
                                     pad_width_mm, pad_length_mm));
    }
    return FootprintDefinition{std::move(ref), std::move(pads)};
}

} // namespace detail

/** Return a simple 0603 resistor footprint fixture. */
[[nodiscard]] inline FootprintDefinition passive_0603_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"passives", "R_0603_1608Metric"}, 1.50,
                                              0.80, 0.95);
}

/** Return a simple 0402 resistor footprint fixture. */
[[nodiscard]] inline FootprintDefinition resistor_0402_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"passives", "R_0402_1005Metric"}, 1.00,
                                              0.55, 0.60);
}

/** Return a simple 0805 resistor footprint fixture. */
[[nodiscard]] inline FootprintDefinition resistor_0805_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"passives", "R_0805_2012Metric"}, 2.00,
                                              1.05, 1.20);
}

/** Return a simple 0603 capacitor footprint fixture. */
[[nodiscard]] inline FootprintDefinition capacitor_0603_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"passives", "C_0603_1608Metric"}, 1.50,
                                              0.80, 0.95);
}

/** Return a simple 0603 inductor/ferrite footprint fixture. */
[[nodiscard]] inline FootprintDefinition inductor_0603_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"passives", "L_0603_1608Metric"}, 1.50,
                                              0.80, 0.95);
}

/** Return a simple 0603 LED footprint fixture. */
[[nodiscard]] inline FootprintDefinition led_0603_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"leds", "LED_0603_1608Metric"}, 1.50,
                                              0.80, 0.95);
}

/** Return a simple 0603 diode footprint fixture. */
[[nodiscard]] inline FootprintDefinition diode_0603_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"diodes", "D_0603_1608Metric"}, 1.50,
                                              0.80, 0.95);
}

/** Return a simple SOD-123 diode footprint fixture. */
[[nodiscard]] inline FootprintDefinition diode_sod_123_footprint() {
    return detail::two_terminal_smd_footprint(FootprintRef{"diodes", "D_SOD-123"}, 3.70, 1.20,
                                              1.35);
}

/** Return a simple 1x02 2.54 mm through-hole header footprint fixture. */
[[nodiscard]] inline FootprintDefinition header_1x02_footprint() {
    return detail::single_row_header_footprint(
        FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"}, 2U, 2.54);
}

/** Return a simple 1x04 2.54 mm through-hole header footprint fixture. */
[[nodiscard]] inline FootprintDefinition header_1x04_footprint() {
    return detail::single_row_header_footprint(
        FootprintRef{"connectors", "PinHeader_1x04_P2.54mm_Vertical"}, 4U, 2.54);
}

/** Return a compact 2x05 1.27 mm through-hole programming header footprint fixture. */
[[nodiscard]] inline FootprintDefinition header_2x05_127_footprint() {
    return detail::dual_row_header_footprint(
        FootprintRef{"connectors", "PinHeader_2x05_P1.27mm_Vertical"}, 5U, 1.27, 1.27);
}

/** Return a simple two-pin 5.08 mm power terminal block footprint fixture. */
[[nodiscard]] inline FootprintDefinition terminal_block_1x02_footprint() {
    return FootprintDefinition{FootprintRef{"connectors", "TerminalBlock_1x02_P5.08mm"},
                               std::vector{detail::plated_round_pad("1", 0.0, -2.54, 2.20, 1.30),
                                           detail::plated_round_pad("2", 0.0, 2.54, 2.20, 1.30)}};
}

/** Return a practical micro USB-B receptacle footprint fixture with mechanical holes. */
[[nodiscard]] inline FootprintDefinition micro_usb_b_footprint() {
    return FootprintDefinition{
        FootprintRef{"connectors", "USB_Micro-B_Receptacle"},
        std::vector{
            detail::front_smd_pad("1", -1.30, -1.70, 0.40, 1.35, FootprintPadShape::Rectangle),
            detail::front_smd_pad("2", -0.65, -1.70, 0.40, 1.35, FootprintPadShape::Rectangle),
            detail::front_smd_pad("3", 0.00, -1.70, 0.40, 1.35, FootprintPadShape::Rectangle),
            detail::front_smd_pad("4", 0.65, -1.70, 0.40, 1.35, FootprintPadShape::Rectangle),
            detail::front_smd_pad("5", 1.30, -1.70, 0.40, 1.35, FootprintPadShape::Rectangle),
            detail::front_smd_pad("6", 0.00, 1.70, 3.20, 1.10, FootprintPadShape::Rectangle),
            detail::mechanical_hole_pad("M1", -2.15, 0.35, 1.00, 0.70,
                                        FootprintPadMechanicalRole::MechanicalSupport),
            detail::mechanical_hole_pad("M2", 2.15, 0.35, 1.00, 0.70,
                                        FootprintPadMechanicalRole::MechanicalSupport),
        }};
}

/** Return a simple SOT-23 footprint fixture. */
[[nodiscard]] inline FootprintDefinition sot_23_footprint() {
    return FootprintDefinition{FootprintRef{"Package_TO_SOT_SMD", "SOT-23"},
                               std::vector{
                                   detail::front_smd_pad("1", -1.00, 0.95, 0.65, 0.95),
                                   detail::front_smd_pad("2", -1.00, -0.95, 0.65, 0.95),
                                   detail::front_smd_pad("3", 1.00, 0.0, 0.65, 0.95),
                               }};
}

/** Return a simple SOT-23-6 footprint fixture. */
[[nodiscard]] inline FootprintDefinition sot_23_6_footprint() {
    return detail::two_side_smd_package(FootprintRef{"Package_TO_SOT_SMD", "SOT-23-6"}, 6U, 1.25,
                                        0.95, 0.60, 0.80);
}

/** Return a simplified SOT-223-3 footprint fixture for first power-regulator boards. */
[[nodiscard]] inline FootprintDefinition sot_223_3_footprint() {
    return FootprintDefinition{FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"},
                               std::vector{
                                   detail::front_smd_pad("1", -2.30, -1.50, 1.05, 1.80),
                                   detail::front_smd_pad("2", 0.00, -1.50, 1.05, 1.80),
                                   detail::front_smd_pad("3", 2.30, -1.50, 1.05, 1.80),
                                   detail::front_smd_pad("4", 0.00, 2.05, 3.80, 2.20),
                               }};
}

/** Return a small IC-style SOIC-8 footprint fixture for first placement tests. */
[[nodiscard]] inline FootprintDefinition soic_8_footprint() {
    return detail::two_side_smd_package(FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"}, 8U, 2.70,
                                        1.27, 0.60, 1.55);
}

/** Return a Package_SO alias for SOIC-8 selected-part references. */
[[nodiscard]] inline FootprintDefinition package_so_soic_8_footprint() {
    return detail::two_side_smd_package(FootprintRef{"Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"}, 8U,
                                        2.70, 1.27, 0.60, 1.55);
}

/** Return a simple TSSOP-14 footprint fixture. */
[[nodiscard]] inline FootprintDefinition tssop_14_footprint() {
    return detail::two_side_smd_package(FootprintRef{"Package_SO", "TSSOP-14_4.4x5mm_P0.65mm"}, 14U,
                                        3.05, 0.65, 0.45, 1.10);
}

/** Return the LQFP-64 MCU footprint used by the STM32 benchmark library. */
[[nodiscard]] inline FootprintDefinition lqfp_64_footprint() {
    return detail::qfp_footprint(FootprintRef{"Package_QFP", "LQFP-64_10x10mm_P0.5mm"}, 64U, 5.80,
                                 0.50, 1.35, 0.30);
}

/** Return the built-in footprint library used by first real-board examples and tests. */
[[nodiscard]] inline FootprintLibrary builtin_footprint_library() {
    auto library = FootprintLibrary{};
    library.add(resistor_0402_footprint());
    library.add(passive_0603_footprint());
    library.add(resistor_0805_footprint());
    library.add(capacitor_0603_footprint());
    library.add(inductor_0603_footprint());
    library.add(led_0603_footprint());
    library.add(diode_0603_footprint());
    library.add(diode_sod_123_footprint());
    library.add(header_1x02_footprint());
    library.add(header_1x04_footprint());
    library.add(header_2x05_127_footprint());
    library.add(terminal_block_1x02_footprint());
    library.add(micro_usb_b_footprint());
    library.add(sot_23_footprint());
    library.add(sot_23_6_footprint());
    library.add(sot_223_3_footprint());
    library.add(soic_8_footprint());
    library.add(package_so_soic_8_footprint());
    library.add(tssop_14_footprint());
    library.add(lqfp_64_footprint());
    return library;
}

/** Resolve a selected physical part's footprint reference and pin-pad mappings. */
[[nodiscard]] inline FootprintResolution resolve_footprint(const PhysicalPart &part,
                                                           const FootprintLibrary &library) {
    auto diagnostics = DiagnosticReport{};
    const auto *definition = library.find(part.footprint());
    if (definition == nullptr) {
        diagnostics.add(
            detail::footprint_diagnostic(DiagnosticCode{"PCB_FOOTPRINT_UNRESOLVED"},
                                         "Selected physical part footprint cannot be resolved: " +
                                             detail::footprint_ref_label(part.footprint())));
        return FootprintResolution{std::nullopt, {}, std::move(diagnostics)};
    }

    auto bindings = std::vector<FootprintPadBinding>{};
    bindings.reserve(part.pin_pad_mappings().size());

    for (const auto &mapping : part.pin_pad_mappings()) {
        const auto pad_id = definition->pad_id(mapping.pad());
        if (!pad_id.has_value()) {
            diagnostics.add(detail::footprint_diagnostic(
                DiagnosticCode{"PCB_PAD_MAPPING_UNKNOWN_PAD"},
                "Selected physical part maps " + detail::pin_def_label(mapping.pin()) +
                    " to unknown footprint pad '" + mapping.pad() + "'"));
            continue;
        }

        if (!definition->pad(pad_id.value()).requires_pin_mapping()) {
            diagnostics.add(detail::footprint_diagnostic(
                DiagnosticCode{"PCB_PAD_MAPPING_NON_ELECTRICAL"},
                "Selected physical part maps " + detail::pin_def_label(mapping.pin()) +
                    " to non-electrical footprint pad '" + mapping.pad() + "'"));
            continue;
        }

        bindings.emplace_back(pad_id.value(), mapping.pin());
    }

    for (std::size_t index = 0; index < definition->pad_count(); ++index) {
        const auto pad_id = FootprintPadId{index};
        if (!definition->pad(pad_id).requires_pin_mapping()) {
            continue;
        }

        const auto bound = std::any_of(
            bindings.begin(), bindings.end(),
            [pad_id](const FootprintPadBinding &binding) { return binding.pad() == pad_id; });
        if (!bound) {
            diagnostics.add(detail::footprint_diagnostic(
                DiagnosticCode{"PCB_PAD_MAPPING_MISSING_PIN"},
                "Footprint electrical pad '" + definition->pad(pad_id).label() +
                    "' has no selected-part pin mapping"));
            break;
        }
    }

    std::sort(bindings.begin(), bindings.end(),
              [](const FootprintPadBinding &lhs, const FootprintPadBinding &rhs) {
                  return lhs.pad().index() < rhs.pad().index();
              });

    return FootprintResolution{std::optional<FootprintDefinition>{*definition}, std::move(bindings),
                               std::move(diagnostics)};
}

} // namespace volt
