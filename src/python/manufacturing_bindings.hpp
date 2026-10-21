#pragma once
#include <pybind11/pybind11.h>

namespace volt::python {
/** Bind exact native export/evidence composition and strict codecs. */
void bind_manufacturing_evidence(pybind11::module_ &module);
/** Bind finite native manufacturing target/coverage data and source-free IO. */
void bind_manufacturing(pybind11::module_ &module);
} // namespace volt::python
