#pragma once

#include <pybind11/pybind11.h>

namespace volt::python {

void bind_schematic(pybind11::module_ &module);

} // namespace volt::python
