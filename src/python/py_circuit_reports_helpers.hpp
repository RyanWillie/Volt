#pragma once

#include "py_circuit.hpp"

#include <volt/circuit/bom/bom.hpp>

namespace volt::python {

namespace {

[[nodiscard]] inline volt::BomSourcingSnapshot sourcing_snapshot_from_dict(const py::dict &dict) {
    auto snapshot = volt::BomSourcingSnapshot{};
    for (const auto item : dict) {
        const auto mpn = py::cast<std::string>(item.first);
        if (!py::isinstance<py::dict>(item.second)) {
            throw py::type_error{"BOM sourcing snapshot values must be dicts"};
        }
        snapshot.set_mpn_properties(
            mpn, properties_from_dict(py::reinterpret_borrow<py::dict>(item.second)));
    }
    return snapshot;
}

} // namespace

} // namespace volt::python
