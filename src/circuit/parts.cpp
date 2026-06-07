#include <volt/circuit/parts.hpp>

#include <cmath>

namespace volt {

namespace {

[[nodiscard]] bool is_supported_part_model_3d_format(const std::string &format) noexcept {
    return format == "glb" || format == "step";
}

[[nodiscard]] bool is_part_model_3d_file_name(const std::string &file_name) noexcept {
    return !file_name.empty() && file_name != "." && file_name != ".." &&
           file_name.find('/') == std::string::npos && file_name.find('\\') == std::string::npos;
}

} // namespace

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

PartModel3D::PartModel3D(std::string format, std::string file_name,
                         std::array<double, 3> translation_mm, double rotation_deg)
    : format_{std::move(format)}, file_name_{std::move(file_name)}, translation_mm_{translation_mm},
      rotation_deg_{rotation_deg} {
    if (!is_supported_part_model_3d_format(format_)) {
        throw std::invalid_argument{"3D model format must be glb or step"};
    }
    if (!is_part_model_3d_file_name(file_name_)) {
        throw std::invalid_argument{"3D model file name must be a basename"};
    }
    if (!std::isfinite(rotation_deg_)) {
        throw std::invalid_argument{"3D model rotation must be finite"};
    }
    for (const auto value : translation_mm_) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument{"3D model translation must be finite"};
        }
    }
}

PhysicalPart::PhysicalPart(ManufacturerPart manufacturer_part, PackageRef package,
                           FootprintRef footprint, std::vector<PinPadMapping> pin_pad_mappings,
                           PropertyMap properties, std::optional<PartModel3D> model_3d)
    : manufacturer_part_{std::move(manufacturer_part)}, package_{std::move(package)},
      footprint_{std::move(footprint)}, pin_pad_mappings_{std::move(pin_pad_mappings)},
      properties_{std::move(properties)}, model_3d_{std::move(model_3d)} {
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
