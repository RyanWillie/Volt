#pragma once

#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>

namespace volt::test {

[[nodiscard]] inline PinSpec passive_pin(std::string name, std::string number) {
    return PinSpec{
        .name = std::move(name),
        .number = std::move(number),
        .terminal_kind = ElectricalTerminalKind::Passive,
        .direction = ElectricalDirection::Passive,
        .drive_kind = ElectricalDriveKind::Passive,
    };
}

[[nodiscard]] inline ComponentDefId define_component(Circuit &circuit, std::string name,
                                                     std::vector<PinSpec> pins) {
    return circuit.define_component(
        ComponentSpec{.name = std::move(name), .pins = std::move(pins)});
}

[[nodiscard]] inline ComponentId instantiate_component(Circuit &circuit, ComponentDefId definition,
                                                       std::string reference) {
    return circuit.instantiate_component(
        definition, ComponentInstanceSpec{.reference = ReferenceDesignator{std::move(reference)}});
}

[[nodiscard]] inline NetId add_net(Circuit &circuit, std::string name,
                                   NetKind kind = NetKind::Signal) {
    return circuit.add_net(NetSpec{.name = NetName{std::move(name)}, .kind = kind});
}

} // namespace volt::test
