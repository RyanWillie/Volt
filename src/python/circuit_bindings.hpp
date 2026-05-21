#pragma once

#include <pybind11/pybind11.h>

namespace volt::python {

void bind_circuit(pybind11::module_ &module);

} // namespace volt::python
