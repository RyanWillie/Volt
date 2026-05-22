#include "circuit_bindings.hpp"

#include <pybind11/pybind11.h>

PYBIND11_MODULE(_volt, module) {
    module.doc() = "Private Volt kernel bindings used by the Python authoring facade.";
    volt::python::bind_circuit(module);
}
