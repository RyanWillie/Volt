#include <volt/circuit/connectivity/instances.hpp>

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

[[nodiscard]] ComponentInstance ComponentInstance::with_property(PropertyKey key,
                                                                 PropertyValue value) const {
    auto properties = properties_;
    properties.set(std::move(key), std::move(value));
    return ComponentInstance{definition_, reference_, std::move(properties)};
}

PinInstance::PinInstance(ComponentId component, PinDefId definition)
    : component_{component}, definition_{definition} {}

} // namespace volt
