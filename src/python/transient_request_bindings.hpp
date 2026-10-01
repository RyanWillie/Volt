#pragma once
#include <pybind11/pybind11.h>

namespace volt::python {
void bind_transient_request(pybind11::module_ &module);
}
