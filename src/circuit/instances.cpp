#include <volt/circuit/instances.hpp>

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
void ComponentInstance::set_property(PropertyKey key, PropertyValue value) {
    properties_.set(std::move(key), std::move(value));
}
PinInstance::PinInstance(ComponentId component, PinDefId definition)
    : component_{component}, definition_{definition} {}

} // namespace volt
