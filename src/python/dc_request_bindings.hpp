#pragma once

#include <pybind11/pybind11.h>

namespace volt::python {

/** Bind the immutable native DC input and request surface. */
void bind_dc_request(pybind11::module_ &module);

} // namespace volt::python
