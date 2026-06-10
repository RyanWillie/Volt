#include <volt/circuit/net_classes.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volt {

NetClassName::NetClassName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Net class name must not be empty"};
    }
}

NetClass::NetClass(NetClassName name) : name_{std::move(name)} {}

void NetClass::set_maximum_net_voltage(Quantity voltage) {
    if (voltage.dimension() != UnitDimension::Voltage) {
        throw std::invalid_argument{"Net class maximum net voltage must use voltage units"};
    }
    if (voltage.value() < 0.0) {
        throw std::invalid_argument{"Net class maximum net voltage must not be negative"};
    }

    maximum_net_voltage_ = voltage;
}

void NetClass::set_copper_clearance_mm(double clearance_mm) {
    if (!std::isfinite(clearance_mm)) {
        throw std::invalid_argument{"Net class copper clearance must be finite"};
    }
    if (clearance_mm < 0.0) {
        throw std::invalid_argument{"Net class copper clearance must not be negative"};
    }

    copper_clearance_mm_ = clearance_mm;
}

[[nodiscard]] NetClassId NetClasses::add_net_class(NetClass net_class) {
    if (net_class_by_name(net_class.name()).has_value()) {
        throw std::logic_error{"Net class name already exists"};
    }

    return net_classes_.insert(std::move(net_class));
}

[[nodiscard]] bool NetClasses::assign_net_class(NetId net, NetClassId net_class) {
    require_net_class(net_class);
    const auto existing =
        std::find_if(net_class_assignments_.begin(), net_class_assignments_.end(),
                     [net](const auto &assignment) { return assignment.first == net; });
    if (existing == net_class_assignments_.end()) {
        net_class_assignments_.emplace_back(net, net_class);
        return true;
    }
    if (existing->second == net_class) {
        return false;
    }

    existing->second = net_class;
    return true;
}

[[nodiscard]] const NetClass &NetClasses::net_class(NetClassId id) const {
    return net_classes_.get(id);
}

[[nodiscard]] std::optional<NetClassId>
NetClasses::net_class_by_name(const NetClassName &name) const {
    for (std::size_t index = 0; index < net_classes_.size(); ++index) {
        const auto id = NetClassId{index};
        if (net_classes_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetClassId> NetClasses::net_class_for_net(NetId net) const noexcept {
    const auto match =
        std::find_if(net_class_assignments_.begin(), net_class_assignments_.end(),
                     [net](const auto &assignment) { return assignment.first == net; });
    if (match == net_class_assignments_.end()) {
        return std::nullopt;
    }

    return match->second;
}

[[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &
NetClasses::net_class_assignments() const noexcept {
    return net_class_assignments_;
}

[[nodiscard]] std::size_t NetClasses::net_class_count() const noexcept {
    return net_classes_.size();
}

void NetClasses::require_net_class(NetClassId net_class) const {
    if (!net_classes_.contains(net_class)) {
        throw std::out_of_range{"Net class ID is out of range"};
    }
}

} // namespace volt
