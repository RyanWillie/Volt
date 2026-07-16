#pragma once

#include "py_schematic.hpp"

#include "py_circuit_py_helpers.hpp"

#include <stdexcept>
#include <vector>

namespace volt::python {

namespace {

[[nodiscard]] inline volt::SchematicEndpoint
schematic_endpoint_from_tuple(const py::tuple &endpoint) {
    if (py::len(endpoint) != 4U) {
        throw py::value_error{"Schematic endpoint payloads must contain x, y, pin, and port net"};
    }

    const auto x = py::cast<double>(endpoint[0]);
    const auto y = py::cast<double>(endpoint[1]);
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    const auto pin = optional_index_from_py(endpoint[2], "Schematic endpoint pins must be indexes");
    const auto port_net =
        optional_index_from_py(endpoint[3], "Schematic endpoint port nets must be indexes");
    if (pin.has_value() && port_net.has_value()) {
        throw py::value_error{"Schematic endpoints cannot reference both a pin and a port net"};
    }

    const auto point = volt::Point{x, y};
    if (pin.has_value()) {
        return volt::SchematicEndpoint{point, pin_id(pin.value())};
    }
    if (port_net.has_value()) {
        return volt::SchematicEndpoint::port(point, net_id(port_net.value()));
    }
    return volt::SchematicEndpoint{point};
}

[[nodiscard]] inline std::vector<volt::SchematicEndpoint>
schematic_endpoints_from_list(const py::list &endpoints) {
    auto result = std::vector<volt::SchematicEndpoint>{};
    result.reserve(static_cast<std::size_t>(py::len(endpoints)));
    for (const auto item : endpoints) {
        result.push_back(schematic_endpoint_from_tuple(py::cast<py::tuple>(item)));
    }
    return result;
}

[[nodiscard]] inline py::tuple schematic_entity_result(std::size_t index, volt::NetId net) {
    return py::make_tuple(index, net.index());
}

[[noreturn]] inline void raise_schematic_authoring_error(const std::invalid_argument &error) {
    throw py::value_error{error.what()};
}

} // namespace

} // namespace volt::python
