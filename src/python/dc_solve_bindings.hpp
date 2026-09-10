#pragma once

#include <pybind11/pybind11.h>

namespace volt::python {

/** Bind immutable native linear DC solve options and results. */
void bind_dc_solve(pybind11::module_ &module);

} // namespace volt::python
