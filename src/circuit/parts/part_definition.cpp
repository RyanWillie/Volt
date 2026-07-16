#include <volt/circuit/parts/part_definition.hpp>

#include <volt/core/errors.hpp>

#include "../../detail/footprint_polygon_validation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace volt {

namespace {

[[nodiscard]] bool is_supported_model_format(const std::string &format) noexcept {
    return format == "glb" || format == "step";
}

[[nodiscard]] bool is_artifact_file_name(const std::string &file_name) noexcept {
    return !file_name.empty() && file_name != "." && file_name != ".." &&
           file_name.find('/') == std::string::npos && file_name.find('\\') == std::string::npos;
}

[[nodiscard]] bool is_positive_finite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool almost_equal(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) <= 1.0e-6;
}

void append_string(std::ostringstream &out, std::string_view value) {
    out << value.size() << ':' << value << '\n';
}

void append_number(std::ostringstream &out, double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Failed to encode canonical exact-part number"};
    }
    append_string(out, std::string_view{buffer.data(), result.ptr});
}

template <typename Enum> void append_enum(std::ostringstream &out, Enum value) {
    static_assert(std::is_enum_v<Enum>);
    append_string(out, std::to_string(static_cast<std::underlying_type_t<Enum>>(value)));
}

void append_quantity(std::ostringstream &out, const Quantity &quantity) {
    append_enum(out, quantity.dimension());
    append_number(out, quantity.value());
}

void append_range(std::ostringstream &out, const QuantityRange &range) {
    append_enum(out, range.dimension());
    append_string(out, range.minimum().has_value() ? "minimum" : "no-minimum");
    if (range.minimum().has_value()) {
        append_quantity(out, *range.minimum());
    }
    append_string(out, range.maximum().has_value() ? "maximum" : "no-maximum");
    if (range.maximum().has_value()) {
        append_quantity(out, *range.maximum());
    }
}

void append_electrical_value(std::ostringstream &out, const ElectricalValue &value) {
    append_enum(out, value.kind());
    switch (value.kind()) {
    case ElectricalValueKind::Unknown:
        return;
    case ElectricalValueKind::Quantity:
        append_quantity(out, value.as_quantity());
        return;
    case ElectricalValueKind::CharacteristicEnvelope: {
        const auto &envelope = value.as_characteristic_envelope();
        append_quantity(out, envelope.minimum());
        append_quantity(out, envelope.typical());
        append_quantity(out, envelope.maximum());
        return;
    }
    case ElectricalValueKind::Range:
        append_range(out, value.as_range());
        return;
    case ElectricalValueKind::ContinuousCurrent:
        append_quantity(out, value.as_continuous_current().value());
        return;
    case ElectricalValueKind::TolerancedQuantity:
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Exact part contains an unnormalized toleranced P1 record"};
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled exact-part P1 value"};
}

void append_electrical_records(std::ostringstream &out, const ElectricalRecordSet &records) {
    append_string(out, std::to_string(records.pin_count()));
    append_string(out, std::to_string(records.records().size()));
    for (const auto &record : records.records()) {
        append_string(out, record.semantic_key().hash().value());
        append_electrical_value(out, record.value());
        append_string(out, std::to_string(record.evidence().size()));
        for (const auto &evidence : record.evidence()) {
            append_string(out, evidence.value());
        }
    }
}

void append_polygon(std::ostringstream &out, const std::optional<PartFootprintPolygon> &polygon) {
    append_string(out, polygon.has_value() ? "polygon" : "no-polygon");
    if (!polygon.has_value()) {
        return;
    }
    append_string(out, std::to_string(polygon->vertices().size()));
    for (const auto &point : polygon->vertices()) {
        append_number(out, point.x_mm());
        append_number(out, point.y_mm());
    }
}

[[nodiscard]] ContentHash part_content_identity(const PartDefinition &part) {
    auto out = std::ostringstream{};
    append_string(out, "volt.part-definition");
    append_string(out, "1");
    append_string(out, part.identity().namespace_name());
    append_string(out, part.identity().name());
    append_string(out, part.identity().version());
    append_string(out, part.implemented_component().value());
    append_electrical_records(out, part.electrical_records());

    append_string(out, std::to_string(part.pin_terminal_mappings().size()));
    for (const auto &mapping : part.pin_terminal_mappings()) {
        append_string(out, mapping.pin().value());
        append_string(out, std::to_string(mapping.terminals().size()));
        for (const auto &terminal : mapping.terminals()) {
            append_string(out, terminal.value());
        }
    }
    append_string(out, std::to_string(part.terminal_dispositions().size()));
    for (const auto &terminal : part.terminal_dispositions()) {
        append_string(out, terminal.terminal().value());
        append_enum(out, terminal.disposition());
    }

    append_string(out, part.provenance().datasheet());
    append_string(out, part.provenance().authored_by());
    append_string(out, part.provenance().derived_from());

    append_string(out, std::to_string(part.schematic_assets().size()));
    for (const auto &asset : part.schematic_assets()) {
        append_string(out, asset.name());
        append_string(out, asset.variant());
        append_string(out, asset.hash().value());
    }

    const auto &physical = part.orderable_part();
    append_string(out, physical.manufacturer_part().manufacturer());
    append_string(out, physical.manufacturer_part().part_number());
    append_string(out, physical.package().value());
    append_string(out, physical.footprint().footprint().library());
    append_string(out, physical.footprint().footprint().name());
    append_string(out, physical.footprint().hash().value());

    append_string(out, std::to_string(physical.footprint_pads().size()));
    for (const auto &pad : physical.footprint_pads()) {
        append_string(out, pad.label());
        append_number(out, pad.x_mm());
        append_number(out, pad.y_mm());
        append_number(out, pad.width_mm());
        append_number(out, pad.height_mm());
        append_string(out, pad.role().has_value() ? "role" : "no-role");
        if (pad.role().has_value()) {
            append_enum(out, *pad.role());
        }
    }

    append_string(out, std::to_string(physical.terminal_pad_mappings().size()));
    for (const auto &mapping : physical.terminal_pad_mappings()) {
        append_string(out, mapping.terminal().value());
        append_string(out, std::to_string(mapping.pads().size()));
        for (const auto &pad : mapping.pads()) {
            append_string(out, pad.value());
        }
    }

    append_polygon(out, physical.footprint_courtyard());
    append_polygon(out, physical.footprint_body());
    append_polygon(out, physical.footprint_fabrication_outline());
    append_polygon(out, physical.footprint_assembly_outline());
    append_string(out, std::to_string(physical.footprint_markings().size()));
    for (const auto &marking : physical.footprint_markings()) {
        append_enum(out, marking.kind());
        append_string(out, std::to_string(marking.polygon().vertices().size()));
        for (const auto &point : marking.polygon().vertices()) {
            append_number(out, point.x_mm());
            append_number(out, point.y_mm());
        }
    }

    append_string(out, std::to_string(physical.approved_alternate_mpns().size()));
    for (const auto &alternate : physical.approved_alternate_mpns()) {
        append_string(out, alternate);
    }
    append_string(out, physical.model_3d().has_value() ? "model-3d" : "no-model-3d");
    if (physical.model_3d().has_value()) {
        const auto &model = *physical.model_3d();
        append_string(out, model.format());
        append_string(out, model.file_name());
        append_string(out, model.hash().value());
        for (const auto coordinate : model.translation_mm()) {
            append_number(out, coordinate);
        }
        append_number(out, model.rotation_deg());
    }
    return sha256_content_hash(out.str());
}

[[nodiscard]] std::string part_identity_label(const PartDefinition &part) {
    return part.identity().namespace_name() + ":" + part.identity().name() + "@" +
           part.identity().version();
}

[[nodiscard]] const PartFootprintPad *find_pad_by_key(const std::vector<PartFootprintPad> &pads,
                                                      const FootprintPadKey &key) noexcept {
    const auto match = std::ranges::find(pads, key.value(), &PartFootprintPad::label);
    return match == pads.end() ? nullptr : &*match;
}

[[nodiscard]] bool pads_overlap(const PartFootprintPad &lhs, const PartFootprintPad &rhs) noexcept {
    const auto lhs_left = lhs.x_mm() - lhs.width_mm() / 2.0;
    const auto lhs_right = lhs.x_mm() + lhs.width_mm() / 2.0;
    const auto lhs_top = lhs.y_mm() - lhs.height_mm() / 2.0;
    const auto lhs_bottom = lhs.y_mm() + lhs.height_mm() / 2.0;

    const auto rhs_left = rhs.x_mm() - rhs.width_mm() / 2.0;
    const auto rhs_right = rhs.x_mm() + rhs.width_mm() / 2.0;
    const auto rhs_top = rhs.y_mm() - rhs.height_mm() / 2.0;
    const auto rhs_bottom = rhs.y_mm() + rhs.height_mm() / 2.0;

    return lhs_left < rhs_right && rhs_left < lhs_right && lhs_top < rhs_bottom &&
           rhs_top < lhs_bottom;
}

[[nodiscard]] Diagnostic part_lineup_diagnostic(std::string_view code, std::string message) {
    return Diagnostic{Severity::Warning, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::PartLineup}, std::move(message),
                      std::vector{EntityRef::part_definition()}};
}

void validate_part_footprint_polygon_vertices(const std::vector<PartFootprintPoint> &vertices) {
    try {
        detail::validate_footprint_polygon_vertices(vertices, "Part footprint polygon");
    } catch (const std::invalid_argument &error) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, error.what()};
    }
}

[[nodiscard]] std::size_t pin_index(const ComponentContract &contract, const PinKey &key) {
    const auto match = std::ranges::find(contract.pin_keys(), key);
    if (match == contract.pin_keys().end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Exact part references a foreign component PinKey"};
    }
    return static_cast<std::size_t>(match - contract.pin_keys().begin());
}

[[nodiscard]] ElectricalSubject record_subject(const ComponentContract &contract,
                                               const ComponentSubjectRef &reference) {
    switch (reference.kind()) {
    case ElectricalSubjectKind::FramedPin: {
        const auto subject = std::ranges::find(contract.framed_pins(), reference.as_framed_pin(),
                                               &ContractFramedPin::key);
        if (subject == contract.framed_pins().end()) {
            break;
        }
        return ElectricalSubject::framed_pin(
            ElectricalPinIndex{pin_index(contract, subject->pin())},
            ElectricalPinIndex{pin_index(contract, subject->reference())});
    }
    case ElectricalSubjectKind::DirectedRelation: {
        const auto subject = std::ranges::find(
            contract.relations(), reference.as_directed_relation(), &ContractDirectedRelation::key);
        if (subject == contract.relations().end()) {
            break;
        }
        return ElectricalSubject::directed_relation(
            ElectricalPinIndex{pin_index(contract, subject->from())},
            ElectricalPinIndex{pin_index(contract, subject->to())});
    }
    case ElectricalSubjectKind::SupplyDomain: {
        const auto subject = std::ranges::find(
            contract.supply_domains(), reference.as_supply_domain(), &ContractSupplyDomain::key);
        if (subject == contract.supply_domains().end()) {
            break;
        }
        auto positives = std::vector<ElectricalPinIndex>{};
        auto returns = std::vector<ElectricalPinIndex>{};
        positives.reserve(subject->positive_pins().size());
        returns.reserve(subject->return_pins().size());
        for (const auto &pin : subject->positive_pins()) {
            positives.emplace_back(pin_index(contract, pin));
        }
        for (const auto &pin : subject->return_pins()) {
            returns.emplace_back(pin_index(contract, pin));
        }
        return ElectricalSubject::supply_domain(std::move(positives), std::move(returns));
    }
    }
    throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                           "Exact part references a foreign component subject"};
}

} // namespace

PartIdentity::PartIdentity(std::string namespace_name, std::string name, std::string version)
    : namespace_{std::move(namespace_name)}, name_{std::move(name)}, version_{std::move(version)} {
    if (namespace_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part namespace must not be empty"};
    }
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part name must not be empty"};
    }
    if (version_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part version must not be empty"};
    }
}

PartProvenance::PartProvenance(std::string datasheet, std::string authored_by,
                               std::string derived_from)
    : datasheet_{std::move(datasheet)}, authored_by_{std::move(authored_by)},
      derived_from_{std::move(derived_from)} {}

bool PartProvenance::empty() const noexcept {
    return datasheet_.empty() && authored_by_.empty() && derived_from_.empty();
}

PartSchematicAssetReference::PartSchematicAssetReference(std::string name, std::string variant,
                                                         ContentHash hash)
    : name_{std::move(name)}, variant_{std::move(variant)}, hash_{std::move(hash)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part schematic asset name must not be empty"};
    }
    if (variant_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part schematic asset variant must not be empty"};
    }
}

PackageTerminalKey::PackageTerminalKey(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Package terminal key must not be empty"};
    }
}

FootprintPadKey::FootprintPadKey(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint pad key must not be empty"};
    }
}

PinPackageTerminalMapping::PinPackageTerminalMapping(PinKey pin,
                                                     std::vector<PackageTerminalKey> terminals)
    : pin_{std::move(pin)}, terminals_{std::move(terminals)} {
    if (terminals_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Logical pin must map to at least one package terminal"};
    }
    std::ranges::sort(terminals_);
    if (std::adjacent_find(terminals_.begin(), terminals_.end()) != terminals_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Logical pin mapping contains a duplicate package terminal"};
    }
}

DisposedPackageTerminal::DisposedPackageTerminal(PackageTerminalKey terminal,
                                                 PackageTerminalDisposition disposition)
    : terminal_{std::move(terminal)}, disposition_{disposition} {}

PackageTerminalPadMapping::PackageTerminalPadMapping(PackageTerminalKey terminal,
                                                     std::vector<FootprintPadKey> pads)
    : terminal_{std::move(terminal)}, pads_{std::move(pads)} {
    if (pads_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Package terminal must map to at least one footprint pad"};
    }
    std::ranges::sort(pads_);
    if (std::adjacent_find(pads_.begin(), pads_.end()) != pads_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Package terminal mapping contains a duplicate footprint pad"};
    }
}

PartFootprintPad::PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm,
                                   double height_mm)
    : PartFootprintPad{std::move(label), x_mm, y_mm, width_mm, height_mm, std::nullopt} {}

PartFootprintPad::PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm,
                                   double height_mm, PartFootprintPadRole role)
    : PartFootprintPad{std::move(label), x_mm,      y_mm,
                       width_mm,         height_mm, std::optional<PartFootprintPadRole>{role}} {}

PartFootprintPad::PartFootprintPad(std::string label, double x_mm, double y_mm, double width_mm,
                                   double height_mm, std::optional<PartFootprintPadRole> role)
    : label_{std::move(label)}, x_mm_{x_mm}, y_mm_{y_mm}, width_mm_{width_mm},
      height_mm_{height_mm}, role_{role} {
    if (label_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint pad label must not be empty"};
    }
    if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint pad position must be finite"};
    }
    if (!is_positive_finite(width_mm_) || !is_positive_finite(height_mm_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint pad size must be positive and finite"};
    }
}

bool PartFootprintPad::requires_terminal_mapping() const noexcept {
    return !role_.has_value() || *role_ == PartFootprintPadRole::Thermal;
}

PartFootprintPoint::PartFootprintPoint(double x_mm, double y_mm) : x_mm_{x_mm}, y_mm_{y_mm} {
    if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part footprint polygon coordinates must be finite"};
    }
}

PartFootprintPolygon::PartFootprintPolygon(std::vector<PartFootprintPoint> vertices)
    : vertices_{std::move(vertices)} {
    validate_part_footprint_polygon_vertices(vertices_);
}

PartFootprintMarking::PartFootprintMarking(PartFootprintMarkingKind kind,
                                           PartFootprintPolygon polygon)
    : kind_{kind}, polygon_{std::move(polygon)} {}

HashedFootprintReference::HashedFootprintReference(FootprintRef footprint, ContentHash hash)
    : footprint_{std::move(footprint)}, hash_{std::move(hash)} {}

PartModel3DReference::PartModel3DReference(std::string format, std::string file_name,
                                           ContentHash hash, std::array<double, 3> translation_mm,
                                           double rotation_deg)
    : format_{std::move(format)}, file_name_{std::move(file_name)}, hash_{std::move(hash)},
      translation_mm_{translation_mm}, rotation_deg_{rotation_deg} {
    if (!is_supported_model_format(format_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "3D model reference format must be glb or step"};
    }
    if (!is_artifact_file_name(file_name_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "3D model reference file name must be a basename"};
    }
    if (!std::isfinite(rotation_deg_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "3D model reference rotation must be finite"};
    }
    for (const auto value : translation_mm_) {
        if (!std::isfinite(value)) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "3D model reference translation must be finite"};
        }
    }
}

OrderablePart::OrderablePart(ManufacturerPart manufacturer_part, PackageRef package,
                             HashedFootprintReference footprint,
                             std::vector<PartFootprintPad> footprint_pads,
                             std::vector<PackageTerminalPadMapping> terminal_pad_mappings,
                             std::vector<std::string> approved_alternate_mpns,
                             std::optional<PartModel3DReference> model_3d,
                             std::optional<PartFootprintPolygon> footprint_courtyard,
                             std::optional<PartFootprintPolygon> footprint_body,
                             std::optional<PartFootprintPolygon> footprint_fabrication_outline,
                             std::optional<PartFootprintPolygon> footprint_assembly_outline,
                             std::vector<PartFootprintMarking> footprint_markings)
    : manufacturer_part_{std::move(manufacturer_part)}, package_{std::move(package)},
      footprint_{std::move(footprint)}, footprint_pads_{std::move(footprint_pads)},
      terminal_pad_mappings_{std::move(terminal_pad_mappings)},
      footprint_courtyard_{std::move(footprint_courtyard)},
      footprint_body_{std::move(footprint_body)},
      footprint_fabrication_outline_{std::move(footprint_fabrication_outline)},
      footprint_assembly_outline_{std::move(footprint_assembly_outline)},
      footprint_markings_{std::move(footprint_markings)},
      approved_alternate_mpns_{std::move(approved_alternate_mpns)}, model_3d_{std::move(model_3d)} {
    if (footprint_pads_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Orderable part footprint projection must contain pads"};
    }
    std::ranges::sort(footprint_pads_, {}, &PartFootprintPad::label);
    if (std::adjacent_find(footprint_pads_.begin(), footprint_pads_.end(),
                           [](const auto &lhs, const auto &rhs) {
                               return lhs.label() == rhs.label();
                           }) != footprint_pads_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Orderable part footprint pads must have unique labels"};
    }
    std::ranges::sort(terminal_pad_mappings_, {}, &PackageTerminalPadMapping::terminal);
    if (std::adjacent_find(terminal_pad_mappings_.begin(), terminal_pad_mappings_.end(),
                           [](const auto &lhs, const auto &rhs) {
                               return lhs.terminal() == rhs.terminal();
                           }) != terminal_pad_mappings_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Package terminal must have exactly one pad mapping"};
    }
    std::ranges::sort(approved_alternate_mpns_);
    for (const auto &alternate : approved_alternate_mpns_) {
        if (alternate.empty()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Approved alternate MPN must not be empty"};
        }
    }
    if (std::adjacent_find(approved_alternate_mpns_.begin(), approved_alternate_mpns_.end()) !=
        approved_alternate_mpns_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Approved alternate MPNs must be unique"};
    }
}

PartDefinition::PartDefinition(const ComponentDefinition &component, PartIdentity identity,
                               ElectricalRecordSet electrical_records,
                               std::vector<PinPackageTerminalMapping> pin_terminal_mappings,
                               std::vector<DisposedPackageTerminal> terminal_dispositions,
                               PartProvenance provenance,
                               std::vector<PartSchematicAssetReference> schematic_assets,
                               OrderablePart orderable_part)
    : identity_{std::move(identity)}, implemented_component_{component.content_identity()},
      electrical_records_{std::move(electrical_records)},
      pin_terminal_mappings_{std::move(pin_terminal_mappings)},
      terminal_dispositions_{std::move(terminal_dispositions)}, provenance_{std::move(provenance)},
      schematic_assets_{std::move(schematic_assets)}, orderable_part_{std::move(orderable_part)},
      content_identity_{sha256_content_hash("")} {
    std::ranges::sort(pin_terminal_mappings_, {}, &PinPackageTerminalMapping::pin);
    if (std::adjacent_find(pin_terminal_mappings_.begin(), pin_terminal_mappings_.end(),
                           [](const auto &lhs, const auto &rhs) {
                               return lhs.pin() == rhs.pin();
                           }) != pin_terminal_mappings_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Exact part contains duplicate logical PinKey ownership"};
    }
    std::ranges::sort(terminal_dispositions_, {}, &DisposedPackageTerminal::terminal);
    if (std::adjacent_find(terminal_dispositions_.begin(), terminal_dispositions_.end(),
                           [](const auto &lhs, const auto &rhs) {
                               return lhs.terminal() == rhs.terminal();
                           }) != terminal_dispositions_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Exact part contains duplicate package terminal dispositions"};
    }
    std::ranges::sort(schematic_assets_, [](const auto &lhs, const auto &rhs) {
        return std::tuple{lhs.name(), lhs.variant()} < std::tuple{rhs.name(), rhs.variant()};
    });
    if (std::adjacent_find(schematic_assets_.begin(), schematic_assets_.end(),
                           [](const auto &lhs, const auto &rhs) {
                               return lhs.name() == rhs.name() && lhs.variant() == rhs.variant();
                           }) != schematic_assets_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Exact part contains duplicate schematic asset variants"};
    }
    require_complete_component_implementation(component);
    require_complete_physical_mappings(component);
    content_identity_ = part_content_identity(*this);
}

void PartDefinition::require_complete_component_implementation(
    const ComponentDefinition &component) const {
    const auto &contract = component.contract();
    if (electrical_records_.pin_count() != contract.pin_keys().size()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Exact part P1 pin count does not match its component contract"};
    }
    for (const auto &requirement : contract.required_records()) {
        const auto expected_subject = record_subject(contract, requirement.subject);
        const auto found =
            std::ranges::any_of(electrical_records_.records(), [&](const auto &record) {
                return record.subject() == expected_subject &&
                       record.observable() == requirement.observable &&
                       record.meaning() == requirement.meaning;
            });
        if (!found) {
            throw KernelArgumentError{
                ErrorCode::CrossReferenceViolation,
                "Exact part is missing a component-contract required canonical P1 record"};
        }
    }
}

void PartDefinition::require_complete_physical_mappings(
    const ComponentDefinition &component) const {
    const auto &pin_keys = component.contract().pin_keys();
    if (pin_terminal_mappings_.size() != pin_keys.size()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Every component PinKey must map to package terminals"};
    }

    auto owned_terminals = std::set<PackageTerminalKey>{};
    for (const auto &mapping : pin_terminal_mappings_) {
        if (std::ranges::find(pin_keys, mapping.pin()) == pin_keys.end()) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Exact part maps a foreign component PinKey"};
        }
        for (const auto &terminal : mapping.terminals()) {
            if (!owned_terminals.insert(terminal).second) {
                throw KernelArgumentError{ErrorCode::DuplicateName,
                                          "Package terminal has duplicate logical ownership"};
            }
        }
    }
    for (const auto &pin : pin_keys) {
        if (!std::ranges::binary_search(pin_terminal_mappings_, pin, {},
                                        &PinPackageTerminalMapping::pin)) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Exact part is missing a logical PinKey mapping"};
        }
    }
    for (const auto &terminal : terminal_dispositions_) {
        if (!owned_terminals.insert(terminal.terminal()).second) {
            throw KernelArgumentError{ErrorCode::DuplicateName,
                                      "Package terminal has duplicate logical ownership"};
        }
    }

    if (orderable_part_.terminal_pad_mappings().size() != owned_terminals.size()) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Every package terminal must map to footprint pads"};
    }
    auto owned_pads = std::set<FootprintPadKey>{};
    for (const auto &mapping : orderable_part_.terminal_pad_mappings()) {
        if (!owned_terminals.contains(mapping.terminal())) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Footprint mapping references a foreign package terminal"};
        }
        for (const auto &pad : mapping.pads()) {
            const auto *footprint_pad = find_pad_by_key(orderable_part_.footprint_pads(), pad);
            if (footprint_pad == nullptr) {
                throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                          "Terminal mapping references a foreign footprint pad"};
            }
            if (!footprint_pad->requires_terminal_mapping()) {
                throw KernelArgumentError{ErrorCode::DuplicateName,
                                          "Non-terminal footprint pad has terminal ownership"};
            }
            if (!owned_pads.insert(pad).second) {
                throw KernelArgumentError{ErrorCode::DuplicateName,
                                          "Footprint pad has duplicate terminal ownership"};
            }
        }
    }
    for (const auto &terminal : owned_terminals) {
        if (!std::ranges::binary_search(orderable_part_.terminal_pad_mappings(), terminal, {},
                                        &PackageTerminalPadMapping::terminal)) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Exact part is missing a package-terminal pad mapping"};
        }
    }
    for (const auto &pad : orderable_part_.footprint_pads()) {
        const auto mapped = owned_pads.contains(FootprintPadKey{pad.label()});
        if (pad.requires_terminal_mapping() != mapped) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Footprint pad ownership does not match its disposition"};
        }
    }
}

DiagnosticReport validate_part_lineup(const PartDefinition &part) {
    auto report = DiagnosticReport{};
    const auto identity = part_identity_label(part);
    const auto &pads = part.orderable_part().footprint_pads();

    for (auto first = pads.begin(); first != pads.end(); ++first) {
        if (!first->requires_terminal_mapping()) {
            continue;
        }
        for (auto second = std::next(first); second != pads.end(); ++second) {
            if (!second->requires_terminal_mapping()) {
                continue;
            }
            if (pads_overlap(*first, *second)) {
                report.add(part_lineup_diagnostic(
                    part_lineup_diagnostic_codes::PadOverlap,
                    "Part " + identity + " footprint pads " + first->label() + " and " +
                        second->label() + " overlap without a non-terminal disposition"));
            }
        }
    }

    auto row_seen = std::vector<bool>(pads.size(), false);
    for (std::size_t index = 0; index < pads.size(); ++index) {
        if (row_seen[index] || !pads[index].requires_terminal_mapping()) {
            continue;
        }
        auto row = std::vector<const PartFootprintPad *>{};
        for (std::size_t candidate = index; candidate < pads.size(); ++candidate) {
            if (!row_seen[candidate] && pads[candidate].requires_terminal_mapping() &&
                almost_equal(pads[candidate].y_mm(), pads[index].y_mm())) {
                row_seen[candidate] = true;
                row.push_back(&pads[candidate]);
            }
        }
        if (row.size() < 3U) {
            continue;
        }
        std::ranges::sort(row, {}, &PartFootprintPad::x_mm);
        const auto expected_pitch = row[1]->x_mm() - row[0]->x_mm();
        auto inconsistent = false;
        for (std::size_t row_index = 2; row_index < row.size(); ++row_index) {
            if (!almost_equal(row[row_index]->x_mm() - row[row_index - 1U]->x_mm(),
                              expected_pitch)) {
                inconsistent = true;
                break;
            }
        }
        if (inconsistent) {
            auto labels = std::ostringstream{};
            for (std::size_t row_index = 0; row_index < row.size(); ++row_index) {
                if (row_index != 0U) {
                    labels << ", ";
                }
                labels << row[row_index]->label();
            }
            report.add(part_lineup_diagnostic(
                part_lineup_diagnostic_codes::PadRowPitchInconsistent,
                "Part " + identity + " footprint row has inconsistent pad pitch: " + labels.str()));
        }
    }

    return report;
}

} // namespace volt
