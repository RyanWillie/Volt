#include <volt/circuit/part_definition.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace volt {

namespace {

[[nodiscard]] bool is_supported_model_format(const std::string &format) noexcept {
    return format == "glb" || format == "step";
}

[[nodiscard]] bool is_artifact_file_name(const std::string &file_name) noexcept {
    return !file_name.empty() && file_name != "." && file_name != ".." &&
           file_name.find('/') == std::string::npos && file_name.find('\\') == std::string::npos;
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

HashedSchematicSymbolReference::HashedSchematicSymbolReference(std::string name,
                                                               std::string variant,
                                                               ContentHash hash)
    : name_{std::move(name)}, variant_{std::move(variant)}, hash_{std::move(hash)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Schematic symbol reference name must not be empty"};
    }
    if (variant_.empty()) {
        throw std::invalid_argument{"Schematic symbol reference variant must not be empty"};
    }
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
                             std::vector<OrderablePinPadMapping> pin_pad_mappings,
                             std::vector<std::string> approved_alternate_mpns,
                             std::optional<PartModel3DReference> model_3d)
    : manufacturer_part_{std::move(manufacturer_part)}, package_{std::move(package)},
      footprint_{std::move(footprint)}, pin_pad_mappings_{std::move(pin_pad_mappings)},
      approved_alternate_mpns_{std::move(approved_alternate_mpns)}, model_3d_{std::move(model_3d)} {
    if (pin_pad_mappings_.empty()) {
        throw std::invalid_argument{"Orderable part must contain at least one pin-pad mapping"};
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
    require_orderable_mappings_match_pins();
}

void PartDefinition::require_orderable_mappings_match_pins() const {
    for (const auto &mapping : orderable_part_.pin_pad_mappings()) {
        const auto pin_exists = std::any_of(pins_.begin(), pins_.end(), [&](const auto &pin) {
            return pin.definition().number() == mapping.pin_number();
        });
        if (!pin_exists) {
            throw std::invalid_argument{"Orderable part maps a pin outside the part definition"};
        }
    }
    for (const auto &pin : pins_) {
        const auto mapped = std::any_of(
            orderable_part_.pin_pad_mappings().begin(), orderable_part_.pin_pad_mappings().end(),
            [&](const auto &mapping) { return mapping.pin_number() == pin.definition().number(); });
        if (!mapped) {
            throw std::invalid_argument{"Orderable part must map every part definition pin"};
        }
    }
}

} // namespace volt
