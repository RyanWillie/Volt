#include "manufacturing_target_detail.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <tuple>
#include <type_traits>

namespace volt {
namespace {
/** Fixed semantic token encoding; deliberately independent of JSON and locale. */
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);

class Tokens {
  public:
    void add(std::string_view value) {
        bytes += std::to_string(value.size()) + ":";
        bytes += value;
    }

    void add(double value) {
        constexpr auto digits = std::string_view{"0123456789abcdef"};
        const auto bits = std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
        auto hex = std::string(16, '0');
        for (unsigned int i = 0; i < 16; ++i)
            hex[i] = digits[(bits >> ((15U - i) * 4U)) & 15U];
        add(hex);
    }

    template <typename T>
        requires std::is_integral_v<T>
    void add(T value) {
        add(std::to_string(value));
    }

    template <typename T>
        requires std::is_enum_v<T>
    void add(T value) {
        add(static_cast<unsigned int>(value));
    }

    template <typename T> void add(const std::optional<T> &value) {
        add(value ? "present" : "absent");
        if (value)
            add(*value);
    }

    void add(BoardCapabilityRange range) {
        add(range.minimum_mm);
        add(range.maximum_mm);
    }

    std::string bytes;
};

void positive(std::optional<double> value) {
    if (value && (!std::isfinite(*value) || *value <= 0.0))
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Manufacturing values must be finite and positive"};
}

template <typename T> void valid_enum(std::optional<T> value, T last) {
    if (value && static_cast<unsigned int>(*value) > static_cast<unsigned int>(last))
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Invalid manufacturing enum representation"};
}

void validate_process(const ManufacturingProcess &p) {
    valid_enum(p.family, ManufacturingFamily::Flex);
    valid_enum(p.material, ManufacturingMaterial::OtherFr4);
    positive(p.top_copper_weight_oz);
    positive(p.bottom_copper_weight_oz);
    positive(p.thickness_mm);
    valid_enum(p.finish, ManufacturingFinish::Enig);
    valid_enum(p.mask_sides, ManufacturingMaskSides::NoMask);
    valid_enum(p.mask_colour, ManufacturingMaskColour::OtherColour);
    valid_enum(p.via_treatment, ManufacturingViaTreatment::Filled);
    valid_enum(p.drilling, ManufacturingDrilling::Laser);
    valid_enum(p.holes, ManufacturingHoleTreatment::BlindBuried);
    valid_enum(p.slots, ManufacturingSlotTreatment::Plated);
    valid_enum(p.assembly, ManufacturingAssembly::OtherAssembly);
    positive(p.dimension_x_mm);
    positive(p.dimension_y_mm);
    if (p.copper_layers && *p.copper_layers == 0U)
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Manufacturing copper layer count must be positive"};
}

void process_tokens(Tokens &out, const ManufacturingProcess &p) {
    out.add(p.family);
    out.add(p.material);
    out.add(p.copper_layers);
    out.add(p.top_copper_weight_oz);
    out.add(p.bottom_copper_weight_oz);
    out.add(p.thickness_mm);
    out.add(p.finish);
    out.add(p.mask_sides);
    out.add(p.mask_colour);
    out.add(p.via_treatment);
    out.add(p.drilling);
    out.add(p.holes);
    out.add(p.slots);
    out.add(p.assembly);
    out.add(p.dimension_x_mm);
    out.add(p.dimension_y_mm);
}

void capability_tokens(Tokens &out, const BoardCapabilityProfile &p) {
    out.add(p.name());
    out.add(p.provenance().source);
    out.add(p.provenance().as_of);
    out.add(p.minimum_track_width_mm());
    out.add(p.minimum_via_drill_mm());
    out.add(p.minimum_via_annular_mm());
    out.add(p.minimum_clearances().size());
    for (const auto &entry : p.minimum_clearances()) {
        out.add(entry.first);
        out.add(entry.second);
        out.add(entry.clearance_mm);
    }
    out.add(p.copper_weight_refinements().size());
    for (const auto &entry : p.copper_weight_refinements()) {
        out.add(entry.copper_weight_oz);
        out.add(entry.minimum_track_width_mm);
        out.add(entry.minimum_clearance_mm);
    }
    out.add(p.supported_copper_layer_counts().size());
    for (const auto value : p.supported_copper_layer_counts())
        out.add(value);
    out.add(p.board_thickness_range_mm());
    out.add(p.available_copper_weights_oz().size());
    for (const auto value : p.available_copper_weights_oz())
        out.add(value);
    out.add(p.drill_diameter_range_mm());
}

void validate_range(std::optional<BoardCapabilityRange> range) {
    if (!range)
        return;
    positive(range->minimum_mm);
    positive(range->maximum_mm);
    if (range->minimum_mm > range->maximum_mm)
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Manufacturing range must be ordered"};
}
} // namespace

namespace detail {
std::string manufacturing_process_bytes(const ManufacturingProcess &p) {
    auto out = Tokens{};
    process_tokens(out, p);
    return out.bytes;
}

std::string manufacturing_capability_bytes(const BoardCapabilityProfile &p) {
    auto out = Tokens{};
    capability_tokens(out, p);
    return out.bytes;
}
} // namespace detail

ManufacturingProfile::ManufacturingProfile(BoardCapabilityProfile capability,
                                           ContentHash source_content,
                                           ManufacturingProcess applicability,
                                           std::optional<BoardCapabilityRange> dimension_x_mm,
                                           std::optional<BoardCapabilityRange> dimension_y_mm,
                                           std::optional<double> minimum_via_radial_annulus_mm)
    : capability_{std::move(capability)}, source_content_{std::move(source_content)},
      applicability_{applicability}, dimension_x_mm_{dimension_x_mm},
      dimension_y_mm_{dimension_y_mm},
      minimum_via_radial_annulus_mm_{minimum_via_radial_annulus_mm} {
    validate_process(applicability_);
    validate_range(dimension_x_mm_);
    validate_range(dimension_y_mm_);
    positive(minimum_via_radial_annulus_mm_);
}

ContentHash ManufacturingProfile::digest() const {
    auto out = Tokens{};
    out.add("volt.manufacturing-profile/1");
    capability_tokens(out, capability_);
    out.add(source_content_.value());
    process_tokens(out, applicability_);
    out.add(dimension_x_mm_);
    out.add(dimension_y_mm_);
    out.add(minimum_via_radial_annulus_mm_);
    return sha256_content_hash(out.bytes);
}

ManufacturingTarget::ManufacturingTarget(CompiledBoardIdentity source, ContentHash content_digest,
                                         ManufacturingGoal goal, ManufacturingProcess process,
                                         ManufacturingProfile profile,
                                         std::optional<CplProjectionOptions> cpl_options)
    : source_{std::move(source)}, content_digest_{std::move(content_digest)}, goal_{goal},
      process_{process}, profile_{std::move(profile)}, cpl_options_{std::move(cpl_options)} {
    valid_enum(std::optional{goal_}, ManufacturingGoal::FabricationAndAssembly);
    validate_process(process_);
    if (cpl_options_) {
        auto &offsets = cpl_options_->rotation_offsets;
        std::ranges::sort(offsets, [](const auto &a, const auto &b) {
            return std::tuple{a.footprint().library(), a.footprint().name()} <
                   std::tuple{b.footprint().library(), b.footprint().name()};
        });
        for (std::size_t i = 1; i < offsets.size(); ++i)
            if (offsets[i - 1].footprint() == offsets[i].footprint())
                throw KernelArgumentError{ErrorCode::InvalidArgument,
                                          "Duplicate manufacturing CPL footprint correction"};
    }
}

ContentHash ManufacturingTarget::digest() const {
    auto out = Tokens{};
    out.add("volt.manufacturing-target/1");
    out.add(source_.board().value());
    out.add(source_.provenance_digest().value());
    out.add(content_digest_.value());
    out.add(goal_);
    process_tokens(out, process_);
    out.add(profile_.digest().value());
    out.add(cpl_options_ ? "present" : "absent");
    if (cpl_options_) {
        out.add(cpl_options_->rotation_offsets.size());
        for (const auto &offset : cpl_options_->rotation_offsets) {
            out.add(offset.footprint().library());
            out.add(offset.footprint().name());
            out.add(offset.rotation_deg());
        }
    }
    return sha256_content_hash(out.bytes);
}

std::string_view manufacturing_check_name(ManufacturingCheck check) {
    constexpr auto names = std::array{
        std::string_view{"ArtifactIdentity"},
        std::string_view{"ProcessSelections"},
        std::string_view{"ProfileApplicability"},
        std::string_view{"LogicalContinuity"},
        std::string_view{"RequiredInventory"},
        std::string_view{"RequiredContacts"},
        std::string_view{"PhysicalConnectivity"},
        std::string_view{"UnintendedContacts"},
        std::string_view{"PhysicalGeometry"},
        std::string_view{"ExportFidelity"},
        std::string_view{"CopperWidth"},
        std::string_view{"TrackTrackSpacing"},
        std::string_view{"TrackPadSpacing"},
        std::string_view{"TrackViaSpacing"},
        std::string_view{"TrackZoneSpacing"},
        std::string_view{"PadPadSpacing"},
        std::string_view{"PadViaSpacing"},
        std::string_view{"PadZoneSpacing"},
        std::string_view{"ViaViaSpacing"},
        std::string_view{"ViaZoneSpacing"},
        std::string_view{"ZoneZoneSpacing"},
        std::string_view{"ViaDrill"},
        std::string_view{"ViaCopperDiameter"},
        std::string_view{"ViaRadialAnnulus"},
        std::string_view{"PadPthAnnulus"},
        std::string_view{"PthDrillRange"},
        std::string_view{"NpthDrillRange"},
        std::string_view{"DrillSeparation"},
        std::string_view{"SlotWidth"},
        std::string_view{"SlotLength"},
        std::string_view{"SlotTreatment"},
        std::string_view{"OutlineClearance"},
        std::string_view{"MechanicalClearance"},
        std::string_view{"DimensionX"},
        std::string_view{"DimensionY"},
        std::string_view{"CopperLayerCount"},
        std::string_view{"BoardThickness"},
        std::string_view{"TopCopperWeight"},
        std::string_view{"BottomCopperWeight"},
        std::string_view{"MaterialRealization"},
        std::string_view{"FinishRealization"},
        std::string_view{"MaskOpenings"},
        std::string_view{"MaskCopperClearance"},
        std::string_view{"MaskWeb"},
        std::string_view{"MaskSliver"},
        std::string_view{"ViaCovering"},
        std::string_view{"LegendHeight"},
        std::string_view{"LegendStroke"},
        std::string_view{"LegendPadClearance"},
        std::string_view{"LegendMaskClearance"},
        std::string_view{"LegendClipping"},
        std::string_view{"RequiredPinOne"},
        std::string_view{"RequiredPolarity"},
        std::string_view{"PartPackageReview"},
        std::string_view{"LandPatternReview"},
        std::string_view{"PinMapReview"},
        std::string_view{"AssemblyOrientation"},
        std::string_view{"PopulatedQuantities"},
        std::string_view{"DnpQuantities"},
        std::string_view{"SelectedAlternates"},
        std::string_view{"CplIdentity"},
        std::string_view{"CplSide"},
        std::string_view{"CplRotation"},
        std::string_view{"SelectedPartRatings"},
        std::string_view{"AssemblyProcess"},
    };
    const auto index = static_cast<std::size_t>(check);
    if (index >= names.size())
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Unknown manufacturing requirement key"};
    return names[index];
}
} // namespace volt
