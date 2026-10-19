#pragma once
#include <pybind11/pybind11.h>

namespace volt::python {
/** Bind finite native manufacturing target/coverage data and source-free IO. */
void bind_manufacturing(pybind11::module_ &module);
} // namespace volt::python
