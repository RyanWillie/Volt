#include <volt/circuit/net_classes.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

void NetClass::set_track_width_mm(double width_mm) {
    if (!std::isfinite(width_mm) || width_mm <= 0.0) {
        throw std::invalid_argument{"Net class track width must be finite and positive"};
    }

    track_width_mm_ = width_mm;
}

void NetClass::set_via_size_mm(double drill_mm, double diameter_mm) {
    if (!std::isfinite(drill_mm) || drill_mm <= 0.0 || !std::isfinite(diameter_mm) ||
        diameter_mm <= 0.0) {
        throw std::invalid_argument{"Net class via sizes must be finite and positive"};
    }
    if (diameter_mm <= drill_mm) {
        throw std::invalid_argument{
            "Net class via diameter must be larger than the drill diameter"};
    }

    via_drill_mm_ = drill_mm;
    via_diameter_mm_ = diameter_mm;
}

void NetClass::set_layer_scope(NetClassLayerScope scope) {
    if (scope != NetClassLayerScope::AnyCopper && !allowed_layer_names_.empty()) {
        throw std::logic_error{"Net class layer scope conflicts with explicit layer names"};
    }

    layer_scope_ = scope;
}

void NetClass::set_allowed_layer_names(std::vector<std::string> names) {
    if (layer_scope_ != NetClassLayerScope::AnyCopper) {
        throw std::logic_error{"Net class layer names conflict with a semantic layer scope"};
    }
    if (names.empty()) {
        throw std::invalid_argument{"Net class allowed layers must not be empty"};
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index].empty()) {
            throw std::invalid_argument{"Net class allowed layer names must not be empty"};
        }
        if (std::find(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(index),
                      names[index]) != names.begin() + static_cast<std::ptrdiff_t>(index)) {
            throw std::invalid_argument{"Net class allowed layer names must be unique"};
        }
    }

    allowed_layer_names_ = std::move(names);
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
