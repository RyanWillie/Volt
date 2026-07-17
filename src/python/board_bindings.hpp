#pragma once

#include "binding_conversions.hpp"

namespace volt::python {

/** Bind the direct Board owner and Design-scoped named Board registry. */
void bind_board(pybind11::module_ &module);

} // namespace volt::python
