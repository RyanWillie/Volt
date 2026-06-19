#pragma once

#include "py_circuit.hpp"

#include <optional>

namespace volt::python {

namespace {

[[nodiscard]] inline std::optional<std::size_t> optional_index_from_py(py::handle value,
                                                                       const char *message) {
    if (value.is_none()) {
        return std::nullopt;
    }
    try {
        return py::cast<std::size_t>(value);
    } catch (const py::cast_error &) {
        throw py::type_error{message};
    }
}

} // namespace

} // namespace volt::python
