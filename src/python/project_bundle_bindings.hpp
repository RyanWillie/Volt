#pragma once

#include "binding_conversions.hpp"

namespace volt::python {

/** Bind the private typed-native ProjectBundle v2 orchestration entrypoint. */
void bind_project_bundle(py::module_ &module);

} // namespace volt::python
