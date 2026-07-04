#include <volt/circuit/constraints/net_classes.hpp>

#include "../circuit_storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {
namespace {

constexpr double copper_thickness_mil_per_oz = 1.378;
constexpr double millimeters_per_mil = 0.0254;

void require_finite_positive(double value, const char *message) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument{message};
    }
}

void require_valid_derivation(const DerivedNetClassRuleValue &value, bool allow_zero,
                              const char *value_message) {
    if (!std::isfinite(value.value_mm) || (!allow_zero && value.value_mm <= 0.0) ||
        (allow_zero && value.value_mm < 0.0)) {
        throw std::invalid_argument{value_message};
    }
    if (value.derivation.calculator_id.empty() || value.derivation.calculator_name.empty() ||
        value.derivation.standard.empty() || value.derivation.reference.empty()) {
        throw std::invalid_argument{"Net class derivation provenance must be complete"};
    }
    for (const auto &input : value.derivation.inputs) {
        if (input.name.empty() || input.unit.empty()) {
            throw std::invalid_argument{
                "Net class derivation inputs must be named and unit-tagged"};
        }
        if (input.text_value.empty() && !std::isfinite(input.value)) {
            throw std::invalid_argument{"Net class derivation numeric inputs must be finite"};
        }
    }
}

[[nodiscard]] std::string environment_name(NetClassTraceEnvironment environment) {
    switch (environment) {
    case NetClassTraceEnvironment::External:
        return "external";
    case NetClassTraceEnvironment::Internal:
        return "internal";
    }
    throw std::logic_error{"Unhandled trace environment"};
}

[[nodiscard]] double ipc2221_trace_width_coefficient(NetClassTraceEnvironment environment) {
    switch (environment) {
    case NetClassTraceEnvironment::External:
        return 0.048;
    case NetClassTraceEnvironment::Internal:
        return 0.024;
    }
    throw std::logic_error{"Unhandled trace environment"};
}

[[nodiscard]] NetClassDerivationInput numeric_input(std::string name, double value,
                                                    std::string unit) {
    return NetClassDerivationInput{std::move(name), value, {}, std::move(unit)};
}

[[nodiscard]] NetClassDerivationInput text_input(std::string name, std::string value,
                                                 std::string unit) {
    return NetClassDerivationInput{std::move(name), 0.0, std::move(value), std::move(unit)};
}

} // namespace

NetClassName::NetClassName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Net class name must not be empty"};
    }
}

[[nodiscard]] DerivedNetClassRuleValue
ipc2221_trace_width_from_current_mm(double current_a, double temperature_rise_c,
                                    double copper_weight_oz, NetClassTraceEnvironment environment) {
    require_finite_positive(current_a, "Trace-width current must be finite and positive");
    require_finite_positive(temperature_rise_c,
                            "Trace-width temperature rise must be finite and positive");
    require_finite_positive(copper_weight_oz,
                            "Trace-width copper weight must be finite and positive");

    const auto coefficient = ipc2221_trace_width_coefficient(environment);
    const auto area_mil2 =
        std::pow(current_a / (coefficient * std::pow(temperature_rise_c, 0.44)), 1.0 / 0.725);
    const auto copper_thickness_mil = copper_thickness_mil_per_oz * copper_weight_oz;
    const auto width_mm = area_mil2 / copper_thickness_mil * millimeters_per_mil;

    return DerivedNetClassRuleValue{
        width_mm,
        NetClassRuleDerivation{
            "ipc-2221.trace-width.current",
            "Trace width from current and temperature rise",
            "IPC-2221",
            "I = k * dT^0.44 * A^0.725; width = A / copper_thickness",
            {
                numeric_input("current", current_a, "A"),
                numeric_input("temperature_rise", temperature_rise_c, "C"),
                numeric_input("copper_weight", copper_weight_oz, "oz/ft^2"),
                text_input("environment", environment_name(environment), "enum"),
            },
        },
    };
}

[[nodiscard]] DerivedNetClassRuleValue
dielectric_height_spacing_mm(double dielectric_height_mm, NetClassDielectricSpacingRule rule) {
    require_finite_positive(dielectric_height_mm, "Dielectric height must be finite and positive");

    auto multiplier = 1.0;
    auto calculator_id = std::string{"volt.spacing.stripline-1h"};
    auto calculator_name = std::string{"1H stripline dielectric-height spacing"};
    auto rule_name = std::string{"stripline_1h"};
    if (rule == NetClassDielectricSpacingRule::Microstrip2H) {
        multiplier = 2.0;
        calculator_id = "volt.spacing.microstrip-2h";
        calculator_name = "2H microstrip dielectric-height spacing";
        rule_name = "microstrip_2h";
    }

    return DerivedNetClassRuleValue{
        dielectric_height_mm * multiplier,
        NetClassRuleDerivation{
            calculator_id,
            calculator_name,
            "Volt dielectric-height spacing fixture",
            "deterministic spacing fixture: spacing = dielectric_height for 1H stripline; "
            "spacing = 2 * dielectric_height for 2H microstrip",
            {
                numeric_input("dielectric_height", dielectric_height_mm, "mm"),
                text_input("rule", rule_name, "enum"),
            },
        },
    };
}

[[nodiscard]] DerivedNetClassRuleValue ipc2221_external_voltage_clearance_mm(double voltage_v) {
    require_finite_positive(voltage_v, "Voltage clearance input must be finite and positive");

    auto clearance_mm = 0.13;
    if (voltage_v > 30.0 && voltage_v <= 100.0) {
        clearance_mm = 0.5;
    } else if (voltage_v > 100.0 && voltage_v <= 500.0) {
        clearance_mm = 0.8;
    } else if (voltage_v > 500.0) {
        clearance_mm = 0.8 + ((voltage_v - 500.0) * 0.005);
    } else if (voltage_v > 15.0) {
        clearance_mm = 0.25;
    }

    return DerivedNetClassRuleValue{
        clearance_mm,
        NetClassRuleDerivation{
            "ipc-2221.clearance.external-voltage",
            "External conductor clearance from voltage",
            "IPC-2221",
            "deterministic IPC-2221 external-conductor voltage-clearance fixture",
            {
                numeric_input("voltage", voltage_v, "V"),
            },
        },
    };
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

void NetClass::derive_copper_clearance(DerivedNetClassRuleValue clearance) {
    require_valid_derivation(clearance, true,
                             "Derived net class copper clearance must be finite and non-negative");
    derived_copper_clearance_ = std::move(clearance);
}

void NetClass::set_track_width_mm(double width_mm) {
    if (!std::isfinite(width_mm) || width_mm <= 0.0) {
        throw std::invalid_argument{"Net class track width must be finite and positive"};
    }

    track_width_mm_ = width_mm;
}

void NetClass::derive_track_width(DerivedNetClassRuleValue width) {
    require_valid_derivation(width, false,
                             "Derived net class track width must be finite and positive");
    derived_track_width_ = std::move(width);
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

[[nodiscard]] std::optional<double> NetClass::copper_clearance_mm() const noexcept {
    if (copper_clearance_mm_.has_value()) {
        return copper_clearance_mm_;
    }
    if (derived_copper_clearance_.has_value()) {
        return derived_copper_clearance_->value_mm;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> NetClass::track_width_mm() const noexcept {
    if (track_width_mm_.has_value()) {
        return track_width_mm_;
    }
    if (derived_track_width_.has_value()) {
        return derived_track_width_->value_mm;
    }
    return std::nullopt;
}

NetClasses::NetClasses() : NetClasses{std::make_shared<detail::NetClassesState>()} {}

NetClasses::NetClasses(std::shared_ptr<const detail::NetClassesState> state)
    : state_{std::move(state)} {}

NetClasses::NetClasses(const NetClasses &other)
    : NetClasses{std::make_shared<detail::NetClassesState>(other.state())} {}

NetClasses::NetClasses(NetClasses &&other) noexcept = default;

NetClasses &NetClasses::operator=(const NetClasses &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::NetClassesState>(other.state());
    }
    return *this;
}

NetClasses &NetClasses::operator=(NetClasses &&other) noexcept = default;

NetClasses::~NetClasses() = default;

[[nodiscard]] NetClassId Circuit::NetClassStorage::add_net_class(NetClass net_class) {
    if (net_class_by_name(net_class.name()).has_value()) {
        throw std::logic_error{"Net class name already exists"};
    }

    return mutable_state().net_classes.insert(std::move(net_class));
}

[[nodiscard]] bool Circuit::NetClassStorage::assign_net_class(NetId net, NetClassId net_class) {
    require_net_class(net_class);
    const auto existing = std::find_if(
        mutable_state().net_class_assignments.begin(), mutable_state().net_class_assignments.end(),
        [net](const auto &assignment) { return assignment.first == net; });
    if (existing == mutable_state().net_class_assignments.end()) {
        mutable_state().net_class_assignments.emplace_back(net, net_class);
        return true;
    }
    if (existing->second == net_class) {
        return false;
    }

    existing->second = net_class;
    return true;
}

[[nodiscard]] const NetClass &NetClasses::net_class(NetClassId id) const {
    return state().net_classes.get(id);
}

[[nodiscard]] std::optional<NetClassId>
NetClasses::net_class_by_name(const NetClassName &name) const {
    for (std::size_t index = 0; index < state().net_classes.size(); ++index) {
        const auto id = NetClassId{index};
        if (state().net_classes.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetClassId> NetClasses::net_class_for_net(NetId net) const noexcept {
    const auto match =
        std::find_if(state().net_class_assignments.begin(), state().net_class_assignments.end(),
                     [net](const auto &assignment) { return assignment.first == net; });
    if (match == state().net_class_assignments.end()) {
        return std::nullopt;
    }

    return match->second;
}

[[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &
NetClasses::net_class_assignments() const noexcept {
    return state().net_class_assignments;
}

[[nodiscard]] std::size_t NetClasses::net_class_count() const noexcept {
    return state().net_classes.size();
}

void NetClasses::require_net_class(NetClassId net_class) const {
    if (!state().net_classes.contains(net_class)) {
        throw std::out_of_range{"Net class ID is out of range"};
    }
}

[[nodiscard]] const detail::NetClassesState &NetClasses::state() const noexcept { return *state_; }

} // namespace volt
