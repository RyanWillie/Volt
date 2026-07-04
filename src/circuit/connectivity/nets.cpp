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

Net::Net(NetName name, NetKind kind) : name_{std::move(name)}, kind_{kind} {}

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

} // namespace volt
