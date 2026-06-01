#include <volt/circuit/parts.hpp>

namespace volt {

ManufacturerPart::ManufacturerPart(std::string manufacturer, std::string part_number)
    : manufacturer_{std::move(manufacturer)}, part_number_{std::move(part_number)} {
    if (manufacturer_.empty()) {
        throw std::invalid_argument{"Manufacturer name must not be empty"};
    }
    if (part_number_.empty()) {
        throw std::invalid_argument{"Manufacturer part number must not be empty"};
    }
}
PackageRef::PackageRef(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Package reference must not be empty"};
    }
}
FootprintRef::FootprintRef(std::string library, std::string name)
    : library_{std::move(library)}, name_{std::move(name)} {
    if (library_.empty()) {
        throw std::invalid_argument{"Footprint library must not be empty"};
    }
    if (name_.empty()) {
        throw std::invalid_argument{"Footprint name must not be empty"};
    }
}
PinPadMapping::PinPadMapping(PinDefId pin, std::string pad) : pin_{pin}, pad_{std::move(pad)} {
    if (pad_.empty()) {
        throw std::invalid_argument{"Pad label must not be empty"};
    }
}
PhysicalPart::PhysicalPart(ManufacturerPart manufacturer_part, PackageRef package,
                           FootprintRef footprint, std::vector<PinPadMapping> pin_pad_mappings,
                           PropertyMap properties)
    : manufacturer_part_{std::move(manufacturer_part)}, package_{std::move(package)},
      footprint_{std::move(footprint)}, pin_pad_mappings_{std::move(pin_pad_mappings)},
      properties_{std::move(properties)} {
    if (pin_pad_mappings_.empty()) {
        throw std::invalid_argument{"Physical part must contain at least one pin-pad mapping"};
    }

    for (auto current = pin_pad_mappings_.begin(); current != pin_pad_mappings_.end(); ++current) {
        const auto duplicate_pad = std::any_of(
            std::next(current), pin_pad_mappings_.end(),
            [current](const PinPadMapping &mapping) { return mapping.pad() == current->pad(); });
        if (duplicate_pad) {
            throw std::invalid_argument{"Physical part contains duplicate physical pad mapping"};
        }
    }
}
[[nodiscard]] const ManufacturerPart &PhysicalPart::manufacturer_part() const noexcept {
    return manufacturer_part_;
}
[[nodiscard]] const std::vector<PinPadMapping> &PhysicalPart::pin_pad_mappings() const noexcept {
    return pin_pad_mappings_;
}
[[nodiscard]] const ElectricalAttributeMap &PhysicalPart::electrical_attributes() const noexcept {
    return electrical_attributes_;
}
void PhysicalPart::set_electrical_attribute(const ElectricalAttributeSpec &spec,
                                            ElectricalAttributeValue value) {
    electrical_attributes_.set(spec, value);
}

} // namespace volt
