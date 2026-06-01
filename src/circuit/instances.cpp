#include <volt/circuit/instances.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace volt {

ReferenceDesignator::ReferenceDesignator(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Reference designator must not be empty"};
    }
}
ComponentInstance::ComponentInstance(ComponentDefId definition, ReferenceDesignator reference,
                                     PropertyMap properties)
    : definition_{definition}, reference_{std::move(reference)},
      properties_{std::move(properties)} {}
[[nodiscard]] const ElectricalAttributeMap &
ComponentInstance::electrical_attributes() const noexcept {
    return electrical_attributes_;
}
[[nodiscard]] const std::optional<PhysicalPart> &
ComponentInstance::selected_physical_part() const noexcept {
    return selected_physical_part_;
}
void ComponentInstance::set_property(PropertyKey key, PropertyValue value) {
    properties_.set(std::move(key), std::move(value));
}
void ComponentInstance::set_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value) {
    electrical_attributes_.set(spec, std::move(value));
}
void ComponentInstance::select_physical_part(PhysicalPart physical_part) {
    selected_physical_part_ = std::move(physical_part);
}
void ComponentInstance::set_selected_part_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                               ElectricalAttributeValue value) {
    if (!selected_physical_part_.has_value()) {
        throw std::logic_error{"Component has no selected physical part"};
    }

    selected_physical_part_->set_electrical_attribute(spec, std::move(value));
}
PinInstance::PinInstance(ComponentId component, PinDefId definition)
    : component_{component}, definition_{definition} {}

} // namespace volt
