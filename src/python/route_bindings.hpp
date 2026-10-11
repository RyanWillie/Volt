#pragma once
#include "py_board.hpp"

namespace volt::python {
void bind_route_values(py::module_ &module);
void bind_route_board(py::class_<PyBoard> &board);
} // namespace volt::python
