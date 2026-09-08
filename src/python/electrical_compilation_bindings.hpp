#pragma once

#include <pybind11/pybind11.h>

namespace volt::python {

/** Bind immutable native electrical compilation results. */
void bind_electrical_compilation(pybind11::module_ &module);

} // namespace volt::python
