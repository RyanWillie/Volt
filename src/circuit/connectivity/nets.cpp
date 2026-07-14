#include <volt/circuit/connectivity/nets.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace volt {

NetName::NetName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Net name must not be empty"};
    }
}

Net::Net(NetName name, NetKind kind, ElectricalAttributeMap electrical_attributes)
    : name_{std::move(name)}, kind_{kind}, electrical_attributes_{std::move(electrical_attributes)},
      intentional_stub_{false} {}

[[nodiscard]] bool Net::contains(PinId pin) const noexcept {
    return std::find(pins_.begin(), pins_.end(), pin) != pins_.end();
}

bool Net::connect(PinId pin) {
    if (contains(pin)) {
        return false;
    }

    pins_.push_back(pin);
    return true;
}

bool Net::disconnect(PinId pin) {
    const auto it = std::find(pins_.begin(), pins_.end(), pin);
    if (it == pins_.end()) {
        return false;
    }

    pins_.erase(it);
    return true;
}

[[nodiscard]] Net Net::with_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value) const {
    auto result = *this;
    result.electrical_attributes_.set(spec, value);
    return result;
}

[[nodiscard]] Net Net::with_intentional_stub(std::size_t first_authored_order) const {
    auto result = *this;
    result.intentional_stub_ = true;
    if (!result.intentional_stub_order_.has_value()) {
        result.intentional_stub_order_ = first_authored_order;
    }
    return result;
}

[[nodiscard]] Net Net::with_net_class(NetClassId net_class,
                                      std::size_t first_authored_order) const {
    auto result = *this;
    result.net_class_ = net_class;
    if (!result.net_class_assignment_order_.has_value()) {
        result.net_class_assignment_order_ = first_authored_order;
    }
    return result;
}

} // namespace volt
