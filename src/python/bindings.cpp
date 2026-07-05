#include "circuit_bindings.hpp"

#include <volt/core/errors.hpp>

#include <exception>
#include <memory>
#include <string>

#include <pybind11/pybind11.h>

namespace {

namespace py = pybind11;

struct PythonKernelExceptions {
    py::object volt_error;
    py::object unknown_entity_error;
    py::object duplicate_name_error;
    py::object cross_reference_error;
    py::object invalid_argument_error;
    py::object invalid_state_error;
};

[[nodiscard]] std::string qualified_name(const py::module_ &module, const char *name) {
    return module.attr("__name__").cast<std::string>() + "." + name;
}

[[nodiscard]] py::object create_exception_class(py::module_ &module, const char *name,
                                                const char *doc, py::handle bases) {
    const auto qualified = qualified_name(module, name);
    auto *type = PyErr_NewExceptionWithDoc(qualified.c_str(), doc, bases.ptr(), nullptr);
    if (type == nullptr) {
        throw py::error_already_set{};
    }

    auto exception = py::reinterpret_steal<py::object>(type);
    module.attr(name) = exception;
    return exception;
}

[[nodiscard]] py::object builtins_exception(PyObject *type) {
    return py::reinterpret_borrow<py::object>(type);
}

PythonKernelExceptions *registered_kernel_exceptions = nullptr;

[[nodiscard]] std::unique_ptr<PythonKernelExceptions>
register_kernel_exceptions(py::module_ &module) {
    auto exceptions = std::make_unique<PythonKernelExceptions>();
    exceptions->volt_error = create_exception_class(
        module, "VoltError", "Base class for typed Volt kernel structural failures.",
        builtins_exception(PyExc_Exception));
    exceptions->unknown_entity_error = create_exception_class(
        module, "UnknownEntityError", "A Volt kernel operation referenced an unknown entity.",
        py::make_tuple(exceptions->volt_error, builtins_exception(PyExc_IndexError),
                       builtins_exception(PyExc_RuntimeError),
                       builtins_exception(PyExc_ValueError)));
    exceptions->duplicate_name_error = create_exception_class(
        module, "DuplicateNameError", "A Volt kernel operation used a duplicate unique name.",
        py::make_tuple(exceptions->volt_error, builtins_exception(PyExc_RuntimeError),
                       builtins_exception(PyExc_ValueError)));
    exceptions->cross_reference_error = create_exception_class(
        module, "CrossReferenceError",
        "A Volt kernel operation crossed model, scope, or ownership boundaries.",
        py::make_tuple(exceptions->volt_error, builtins_exception(PyExc_RuntimeError),
                       builtins_exception(PyExc_ValueError)));
    exceptions->invalid_argument_error = create_exception_class(
        module, "InvalidArgumentError", "A Volt kernel operation received an invalid argument.",
        py::make_tuple(exceptions->volt_error, builtins_exception(PyExc_ValueError)));
    exceptions->invalid_state_error = create_exception_class(
        module, "InvalidStateError",
        "A Volt kernel operation is invalid for the current model state.",
        py::make_tuple(exceptions->volt_error, builtins_exception(PyExc_RuntimeError),
                       builtins_exception(PyExc_ValueError)));

    return exceptions;
}

[[nodiscard]] py::object python_error_type(const volt::KernelError &error,
                                           const PythonKernelExceptions &exceptions) {
    switch (error.code()) {
    case volt::ErrorCode::UnknownEntity:
        return exceptions.unknown_entity_error;
    case volt::ErrorCode::DuplicateName:
        return exceptions.duplicate_name_error;
    case volt::ErrorCode::CrossReferenceViolation:
        return exceptions.cross_reference_error;
    case volt::ErrorCode::InvalidArgument:
        return exceptions.invalid_argument_error;
    case volt::ErrorCode::InvalidState:
        return exceptions.invalid_state_error;
    }
    return exceptions.volt_error;
}

void translate_kernel_error(std::exception_ptr pointer) {
    try {
        if (pointer != nullptr) {
            std::rethrow_exception(pointer);
        }
    } catch (const volt::KernelError &error) {
        if (registered_kernel_exceptions == nullptr) {
            PyErr_SetString(PyExc_RuntimeError, error.what());
            return;
        }
        PyErr_SetString(python_error_type(error, *registered_kernel_exceptions).ptr(),
                        error.what());
    }
}

void register_kernel_error_translator(py::module_ &module) {
    static auto exceptions = register_kernel_exceptions(module);
    registered_kernel_exceptions = exceptions.get();
    py::register_exception_translator(&translate_kernel_error);
}

} // namespace

PYBIND11_MODULE(_volt, module) {
    module.doc() = "Private Volt kernel bindings used by the Python authoring facade.";
    register_kernel_error_translator(module);
    volt::python::bind_circuit(module);
}
