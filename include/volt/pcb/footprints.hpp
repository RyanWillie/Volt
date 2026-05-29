#pragma once

#include <algorithm>
#include <cmath>
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

    /** Return all layers in deterministic order. */
    [[nodiscard]] const std::vector<FootprintLayer> &layers() const noexcept { return layers_; }

    /** Return whether this set includes the requested layer. */
    [[nodiscard]] bool contains(FootprintLayer layer) const noexcept {
        return std::find(layers_.begin(), layers_.end(), layer) != layers_.end();
    }

    /** Return whether this set is a front-side surface-mount stack. */
    [[nodiscard]] bool is_front_smd() const noexcept { return *this == front_smd(); }

    /** Return whether this set is a back-side surface-mount stack. */
    [[nodiscard]] bool is_back_smd() const noexcept { return *this == back_smd(); }

    /** Return whether this set is a valid surface-mount stack on either board side. */
    [[nodiscard]] bool is_surface_mount() const noexcept { return is_front_smd() || is_back_smd(); }

    /** Return whether this set is a through-hole stack. */
    [[nodiscard]] bool is_through_hole() const noexcept { return *this == through_hole(); }

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
        if (kind_ == FootprintPadKind::ThroughHole && !layers_.is_through_hole()) {
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

} // namespace detail

/** Return a simple 0603 passive footprint fixture. */
[[nodiscard]] inline FootprintDefinition passive_0603_footprint() {
    return FootprintDefinition{FootprintRef{"passives", "R_0603_1608Metric"},
                               std::vector{detail::front_smd_pad("1", -0.75, 0.0, 0.80, 0.95),
                                           detail::front_smd_pad("2", 0.75, 0.0, 0.80, 0.95)}};
}

/** Return a simple 0603 LED footprint fixture. */
[[nodiscard]] inline FootprintDefinition led_0603_footprint() {
    return FootprintDefinition{FootprintRef{"leds", "LED_0603_1608Metric"},
                               std::vector{detail::front_smd_pad("1", -0.75, 0.0, 0.80, 0.95),
                                           detail::front_smd_pad("2", 0.75, 0.0, 0.80, 0.95)}};
}

/** Return a simple 0603 diode footprint fixture. */
[[nodiscard]] inline FootprintDefinition diode_0603_footprint() {
    return FootprintDefinition{FootprintRef{"diodes", "D_0603_1608Metric"},
                               std::vector{detail::front_smd_pad("1", -0.75, 0.0, 0.80, 0.95),
                                           detail::front_smd_pad("2", 0.75, 0.0, 0.80, 0.95)}};
}

/** Return a simple 1x02 2.54 mm through-hole header footprint fixture. */
[[nodiscard]] inline FootprintDefinition header_1x02_footprint() {
    return FootprintDefinition{FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                               std::vector{detail::plated_header_pad("1", 0.0, -1.27),
                                           detail::plated_header_pad("2", 0.0, 1.27)}};
}

/** Return a small IC-style SOIC-8 footprint fixture for first placement tests. */
[[nodiscard]] inline FootprintDefinition soic_8_footprint() {
    return FootprintDefinition{FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"},
                               std::vector{
                                   detail::front_smd_pad("1", -2.70, 1.905, 0.60, 1.55),
                                   detail::front_smd_pad("2", -2.70, 0.635, 0.60, 1.55),
                                   detail::front_smd_pad("3", -2.70, -0.635, 0.60, 1.55),
                                   detail::front_smd_pad("4", -2.70, -1.905, 0.60, 1.55),
                                   detail::front_smd_pad("5", 2.70, -1.905, 0.60, 1.55),
                                   detail::front_smd_pad("6", 2.70, -0.635, 0.60, 1.55),
                                   detail::front_smd_pad("7", 2.70, 0.635, 0.60, 1.55),
                                   detail::front_smd_pad("8", 2.70, 1.905, 0.60, 1.55),
                               }};
}

/** Return the tiny built-in footprint library needed by first PCB tests. */
[[nodiscard]] inline FootprintLibrary builtin_footprint_library() {
    auto library = FootprintLibrary{};
    library.add(passive_0603_footprint());
    library.add(led_0603_footprint());
    library.add(diode_0603_footprint());
    library.add(header_1x02_footprint());
    library.add(soic_8_footprint());
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
                                         "Selected physical part footprint cannot be resolved"));
        return FootprintResolution{std::nullopt, {}, std::move(diagnostics)};
    }

    auto bindings = std::vector<FootprintPadBinding>{};
    bindings.reserve(part.pin_pad_mappings().size());

    for (const auto &mapping : part.pin_pad_mappings()) {
        const auto pad_id = definition->pad_id(mapping.pad());
        if (!pad_id.has_value()) {
            diagnostics.add(detail::footprint_diagnostic(
                DiagnosticCode{"PCB_PAD_MAPPING_UNKNOWN_PAD"},
                "Selected physical part maps a pin to a footprint pad that does not exist"));
            continue;
        }

        if (!definition->pad(pad_id.value()).requires_pin_mapping()) {
            diagnostics.add(detail::footprint_diagnostic(
                DiagnosticCode{"PCB_PAD_MAPPING_NON_ELECTRICAL"},
                "Selected physical part maps a logical pin to a non-electrical footprint pad"));
            continue;
        }

        bindings.emplace_back(pad_id.value(), mapping.pin());
    }

    auto missing_required_pad_mapping = false;
    for (std::size_t index = 0; index < definition->pad_count(); ++index) {
        const auto pad_id = FootprintPadId{index};
        if (!definition->pad(pad_id).requires_pin_mapping()) {
            continue;
        }

        const auto bound = std::any_of(
            bindings.begin(), bindings.end(),
            [pad_id](const FootprintPadBinding &binding) { return binding.pad() == pad_id; });
        if (!bound) {
            missing_required_pad_mapping = true;
            break;
        }
    }

    if (missing_required_pad_mapping) {
        diagnostics.add(detail::footprint_diagnostic(
            DiagnosticCode{"PCB_PAD_MAPPING_MISSING_PIN"},
            "Footprint contains an electrical pad with no selected-part pin mapping"));
    }

    std::sort(bindings.begin(), bindings.end(),
              [](const FootprintPadBinding &lhs, const FootprintPadBinding &rhs) {
                  return lhs.pad().index() < rhs.pad().index();
              });

    return FootprintResolution{std::optional<FootprintDefinition>{*definition}, std::move(bindings),
                               std::move(diagnostics)};
}

} // namespace volt
