#include <volt/circuit/parts/part_definition.hpp>

#include "../../detail/footprint_polygon_validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
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

[[nodiscard]] std::string part_identity_label(const PartDefinition &part) {
    return part.identity().namespace_name() + ":" + part.identity().name() + "@" +
           part.identity().version();
}

[[nodiscard]] const PartPin *find_pin_by_number(const std::vector<PartPin> &pins,
                                                const std::string &number) noexcept {
    const auto match = std::find_if(pins.begin(), pins.end(), [&](const auto &pin) {
        return pin.definition().number() == number;
    });
    return match == pins.end() ? nullptr : &*match;
}

[[nodiscard]] const PartFootprintPad *find_pad_by_label(const std::vector<PartFootprintPad> &pads,
                                                        const std::string &label) noexcept {
    const auto match = std::find_if(pads.begin(), pads.end(),
                                    [&](const auto &pad) { return pad.label() == label; });
    return match == pads.end() ? nullptr : &*match;
}

[[nodiscard]] bool pin_has_mapping(const PartDefinition &part, const PartPin &pin) noexcept {
    return std::any_of(part.orderable_part().pin_pad_mappings().begin(),
                       part.orderable_part().pin_pad_mappings().end(), [&](const auto &mapping) {
                           return mapping.pin_number() == pin.definition().number();
                       });
}

[[nodiscard]] bool pad_has_mapping(const PartDefinition &part,
                                   const PartFootprintPad &pad) noexcept {
    return std::any_of(part.orderable_part().pin_pad_mappings().begin(),
                       part.orderable_part().pin_pad_mappings().end(),
                       [&](const auto &mapping) { return mapping.pad() == pad.label(); });
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

} // namespace

PartIdentity::PartIdentity(std::string namespace_name, std::string name, std::string version)
    : namespace_{std::move(namespace_name)}, name_{std::move(name)}, version_{std::move(version)} {
    if (namespace_.empty()) {
        throw std::invalid_argument{"Part namespace must not be empty"};
    }
    if (name_.empty()) {
        throw std::invalid_argument{"Part name must not be empty"};
    }
    if (version_.empty()) {
        throw std::invalid_argument{"Part version must not be empty"};
    }
}

PartPin::PartPin(PinDefinition definition, ElectricalAttributeMap electrical_attributes)
    : definition_{std::move(definition)}, electrical_attributes_{std::move(electrical_attributes)} {
}

PartProvenance::PartProvenance(std::string datasheet, std::string authored_by,
                               std::string derived_from)
    : datasheet_{std::move(datasheet)}, authored_by_{std::move(authored_by)},
      derived_from_{std::move(derived_from)} {}

[[nodiscard]] bool PartProvenance::empty() const noexcept {
    return datasheet_.empty() && authored_by_.empty() && derived_from_.empty();
}

PartSymbolPin::PartSymbolPin(std::string name, std::string number)
    : name_{std::move(name)}, number_{std::move(number)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Schematic symbol pin name must not be empty"};
    }
    if (number_.empty()) {
        throw std::invalid_argument{"Schematic symbol pin number must not be empty"};
    }
}

HashedSchematicSymbolReference::HashedSchematicSymbolReference(std::string name,
                                                               std::string variant,
                                                               ContentHash hash,
                                                               std::vector<PartSymbolPin> pins)
    : name_{std::move(name)}, variant_{std::move(variant)}, hash_{std::move(hash)},
      pins_{std::move(pins)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Schematic symbol reference name must not be empty"};
    }
    if (variant_.empty()) {
        throw std::invalid_argument{"Schematic symbol reference variant must not be empty"};
    }
    if (pins_.empty()) {
        throw std::invalid_argument{"Schematic symbol reference must contain pin lineup"};
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
        throw std::invalid_argument{"Footprint pad label must not be empty"};
    }
    if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
        throw std::invalid_argument{"Footprint pad position must be finite"};
    }
    if (!is_positive_finite(width_mm_) || !is_positive_finite(height_mm_)) {
        throw std::invalid_argument{"Footprint pad size must be positive and finite"};
    }
}

PartFootprintPoint::PartFootprintPoint(double x_mm, double y_mm) : x_mm_{x_mm}, y_mm_{y_mm} {
    if (!std::isfinite(x_mm_) || !std::isfinite(y_mm_)) {
        throw std::invalid_argument{"Part footprint polygon coordinates must be finite"};
    }
}

PartFootprintPolygon::PartFootprintPolygon(std::vector<PartFootprintPoint> vertices)
    : vertices_{std::move(vertices)} {
    detail::validate_footprint_polygon_vertices(vertices_, "Part footprint polygon");
}

HashedFootprintReference::HashedFootprintReference(FootprintRef footprint, ContentHash hash)
    : footprint_{std::move(footprint)}, hash_{std::move(hash)} {}

OrderablePinPadMapping::OrderablePinPadMapping(std::string pin_number, std::string pad)
    : pin_number_{std::move(pin_number)}, pad_{std::move(pad)} {
    if (pin_number_.empty()) {
        throw std::invalid_argument{"Orderable pin-pad mapping pin number must not be empty"};
    }
    if (pad_.empty()) {
        throw std::invalid_argument{"Orderable pin-pad mapping pad must not be empty"};
    }
}

PartModel3DReference::PartModel3DReference(std::string format, std::string file_name,
                                           ContentHash hash, std::array<double, 3> translation_mm,
                                           double rotation_deg)
    : format_{std::move(format)}, file_name_{std::move(file_name)}, hash_{std::move(hash)},
      translation_mm_{translation_mm}, rotation_deg_{rotation_deg} {
    if (!is_supported_model_format(format_)) {
        throw std::invalid_argument{"3D model reference format must be glb or step"};
    }
    if (!is_artifact_file_name(file_name_)) {
        throw std::invalid_argument{"3D model reference file name must be a basename"};
    }
    if (!std::isfinite(rotation_deg_)) {
        throw std::invalid_argument{"3D model reference rotation must be finite"};
    }
    for (const auto value : translation_mm_) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument{"3D model reference translation must be finite"};
        }
    }
}

OrderablePart::OrderablePart(ManufacturerPart manufacturer_part, PackageRef package,
                             HashedFootprintReference footprint,
                             std::vector<PartFootprintPad> footprint_pads,
                             std::vector<OrderablePinPadMapping> pin_pad_mappings,
                             std::vector<std::string> approved_alternate_mpns,
                             std::optional<PartModel3DReference> model_3d,
                             std::optional<PartFootprintPolygon> footprint_courtyard,
                             std::optional<PartFootprintPolygon> footprint_body)
    : manufacturer_part_{std::move(manufacturer_part)}, package_{std::move(package)},
      footprint_{std::move(footprint)}, footprint_pads_{std::move(footprint_pads)},
      footprint_courtyard_{std::move(footprint_courtyard)},
      footprint_body_{std::move(footprint_body)}, pin_pad_mappings_{std::move(pin_pad_mappings)},
      approved_alternate_mpns_{std::move(approved_alternate_mpns)}, model_3d_{std::move(model_3d)} {
    if (footprint_pads_.empty()) {
        throw std::invalid_argument{"Orderable part footprint projection must contain pads"};
    }
    for (auto current = footprint_pads_.begin(); current != footprint_pads_.end(); ++current) {
        const auto duplicate_label =
            std::any_of(std::next(current), footprint_pads_.end(),
                        [current](const auto &pad) { return pad.label() == current->label(); });
        if (duplicate_label) {
            throw std::invalid_argument{"Orderable part footprint pads must have unique labels"};
        }
    }
    for (auto current = pin_pad_mappings_.begin(); current != pin_pad_mappings_.end(); ++current) {
        const auto duplicate_pad =
            std::any_of(std::next(current), pin_pad_mappings_.end(),
                        [current](const auto &mapping) { return mapping.pad() == current->pad(); });
        if (duplicate_pad) {
            throw std::invalid_argument{"Orderable part contains duplicate pad mappings"};
        }
    }
    for (auto current = approved_alternate_mpns_.begin(); current != approved_alternate_mpns_.end();
         ++current) {
        if (current->empty()) {
            throw std::invalid_argument{"Approved alternate MPN must not be empty"};
        }
        const auto duplicate = std::find(std::next(current), approved_alternate_mpns_.end(),
                                         *current) != approved_alternate_mpns_.end();
        if (duplicate) {
            throw std::invalid_argument{"Approved alternate MPNs must be unique"};
        }
    }
}

PartDefinition::PartDefinition(PartIdentity identity, std::vector<PartPin> pins,
                               ElectricalAttributeMap electrical_attributes,
                               PartProvenance provenance,
                               std::vector<HashedSchematicSymbolReference> symbols,
                               OrderablePart orderable_part)
    : identity_{std::move(identity)}, pins_{std::move(pins)},
      electrical_attributes_{std::move(electrical_attributes)}, provenance_{std::move(provenance)},
      symbols_{std::move(symbols)}, orderable_part_{std::move(orderable_part)} {
    if (pins_.empty()) {
        throw std::invalid_argument{"Part definition must contain at least one pin"};
    }
    for (auto current = pins_.begin(); current != pins_.end(); ++current) {
        const auto duplicate_number =
            std::any_of(std::next(current), pins_.end(), [current](const auto &candidate) {
                return candidate.definition().number() == current->definition().number();
            });
        if (duplicate_number) {
            throw std::invalid_argument{"Part definition pin numbers must be unique"};
        }
    }
    require_symbol_lineup_matches_pins();
    require_orderable_mappings_match_pins();
}

void PartDefinition::require_symbol_lineup_matches_pins() const {
    if (symbols_.empty()) {
        throw std::invalid_argument{
            "Part definition must contain at least one schematic symbol projection"};
    }
    for (const auto &symbol : symbols_) {
        auto counts = std::vector<std::size_t>(pins_.size(), 0U);
        for (const auto &symbol_pin : symbol.pins()) {
            const auto pin = find_pin_by_number(pins_, symbol_pin.number());
            if (pin == nullptr || pin->definition().name() != symbol_pin.name()) {
                throw std::invalid_argument{
                    "Schematic symbol pin is outside the part definition pin map"};
            }
            const auto index = static_cast<std::size_t>(pin - pins_.data());
            ++counts[index];
        }
        const auto exact =
            std::all_of(counts.begin(), counts.end(), [](const auto count) { return count == 1U; });
        if (!exact) {
            throw std::invalid_argument{
                "Schematic symbol must reference every part definition pin exactly once"};
        }
    }
}

void PartDefinition::require_orderable_mappings_match_pins() const {
    for (const auto &mapping : orderable_part_.pin_pad_mappings()) {
        if (find_pin_by_number(pins_, mapping.pin_number()) == nullptr) {
            throw std::invalid_argument{"Orderable part maps a pin outside the part definition"};
        }
        if (mapping.pad().find(',') != std::string::npos) {
            throw std::invalid_argument{
                "Orderable part multi-pad mappings must use one explicit pad entry per pad"};
        }
        if (find_pad_by_label(orderable_part_.footprint_pads(), mapping.pad()) == nullptr) {
            throw std::invalid_argument{
                "Orderable part maps a pad outside the footprint projection"};
        }
    }
}

[[nodiscard]] DiagnosticReport validate_part_lineup(const PartDefinition &part) {
    auto report = DiagnosticReport{};
    const auto identity = part_identity_label(part);

    for (const auto &pin : part.pins()) {
        if (!pin_has_mapping(part, pin)) {
            report.add(part_lineup_diagnostic(
                part_lineup_diagnostic_codes::PinWithoutPad,
                "Part " + identity + " pin " + pin.definition().number() + " (" +
                    pin.definition().name() + ") has no mapped footprint pad"));
        }
    }

    const auto &pads = part.orderable_part().footprint_pads();
    for (const auto &pad : pads) {
        if (pad.requires_pin_mapping() && !pad_has_mapping(part, pad)) {
            report.add(part_lineup_diagnostic(
                part_lineup_diagnostic_codes::PadWithoutPin,
                "Part " + identity + " footprint pad " + pad.label() +
                    " has no mapped pin or declared mechanical/thermal role"));
        }
    }

    for (auto first = pads.begin(); first != pads.end(); ++first) {
        if (!first->requires_pin_mapping()) {
            continue;
        }
        for (auto second = std::next(first); second != pads.end(); ++second) {
            if (!second->requires_pin_mapping()) {
                continue;
            }
            if (pads_overlap(*first, *second)) {
                report.add(part_lineup_diagnostic(
                    part_lineup_diagnostic_codes::PadOverlap,
                    "Part " + identity + " footprint pads " + first->label() + " and " +
                        second->label() + " overlap without a mechanical/thermal declaration"));
            }
        }
    }

    auto row_seen = std::vector<bool>(pads.size(), false);
    for (std::size_t index = 0; index < pads.size(); ++index) {
        if (row_seen[index] || !pads[index].requires_pin_mapping()) {
            continue;
        }
        auto row = std::vector<const PartFootprintPad *>{};
        for (std::size_t candidate = index; candidate < pads.size(); ++candidate) {
            if (!row_seen[candidate] && pads[candidate].requires_pin_mapping() &&
                almost_equal(pads[candidate].y_mm(), pads[index].y_mm())) {
                row_seen[candidate] = true;
                row.push_back(&pads[candidate]);
            }
        }
        if (row.size() < 3U) {
            continue;
        }
        std::sort(row.begin(), row.end(),
                  [](const auto *lhs, const auto *rhs) { return lhs->x_mm() < rhs->x_mm(); });
        const auto expected_pitch = row[1]->x_mm() - row[0]->x_mm();
        const auto inconsistent =
            std::any_of(std::next(row.begin(), 2), row.end(), [&](const auto *pad) {
                const auto previous = *std::prev(std::find(row.begin(), row.end(), pad));
                return !almost_equal(pad->x_mm() - previous->x_mm(), expected_pitch);
            });
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
