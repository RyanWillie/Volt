#include <volt/circuit/connectivity/instances.hpp>

#include <volt/core/errors.hpp>

#include <string>
#include <utility>

namespace volt {

ReferenceDesignator::ReferenceDesignator(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Reference designator must not be empty"};
    }
}

ComponentInstance::ComponentInstance(ComponentDefId definition, ReferenceDesignator reference,
                                     PropertyMap properties,
                                     ElectricalAttributeMap electrical_attributes)
    : definition_{definition}, reference_{std::move(reference)}, properties_{std::move(properties)},
      electrical_attributes_{std::move(electrical_attributes)}, selection_override_{false} {}

[[nodiscard]] ComponentInstance ComponentInstance::with_property(PropertyKey key,
                                                                 PropertyValue value) const {
    auto result = *this;
    result.properties_.set(std::move(key), std::move(value));
    return result;
}

[[nodiscard]] ComponentInstance
ComponentInstance::with_electrical_attribute(const ElectricalAttributeSpec &spec,
                                             ElectricalAttributeValue value) const {
    auto result = *this;
    result.electrical_attributes_.set(spec, value);
    return result;
}

[[nodiscard]] ComponentInstance
ComponentInstance::with_selected_physical_part(PhysicalPart part) const {
    auto result = *this;
    result.selected_physical_part_ = std::move(part);
    return result;
}

[[nodiscard]] ComponentInstance
ComponentInstance::with_selected_part_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                           ElectricalAttributeValue value) const {
    if (!selected_physical_part_.has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState, "Component has no selected physical part"};
    }
    return with_selected_physical_part(
        selected_physical_part_->with_electrical_attribute(spec, value));
}

[[nodiscard]] ComponentInstance
ComponentInstance::with_assembly_intent(std::optional<bool> dnp,
                                        std::optional<bool> selection_override,
                                        std::size_t first_authored_order) const {
    auto result = *this;
    result.dnp_ = dnp.has_value() ? dnp : dnp_;
    result.selection_override_ = selection_override.value_or(selection_override_);
    if (result.dnp_.has_value() || result.selection_override_) {
        if (!result.assembly_intent_order_.has_value()) {
            result.assembly_intent_order_ = first_authored_order;
        }
    } else {
        result.assembly_intent_order_ = std::nullopt;
    }
    return result;
}

PinInstance::PinInstance(ComponentId component, PinDefId definition)
    : component_{component}, definition_{definition}, intentional_no_connect_{false} {}

[[nodiscard]] PinInstance
PinInstance::with_intentional_no_connect(std::size_t first_authored_order) const {
    auto result = *this;
    result.intentional_no_connect_ = true;
    if (!result.intentional_no_connect_order_.has_value()) {
        result.intentional_no_connect_order_ = first_authored_order;
    }
    return result;
}

} // namespace volt
