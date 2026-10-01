#include "electrical_compilation_bindings.hpp"

#include "binding_diagnostic_conversions.hpp"

#include <cstddef>

#include <pybind11/stl.h>

#include <volt/electrical/ac_request.hpp>
#include <volt/electrical/compiled_electrical_model.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>

namespace volt::python {
namespace {

template <typename Value> [[nodiscard]] py::tuple copied_tuple(const std::vector<Value> &values) {
    auto result = py::tuple{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

[[nodiscard]] py::list diagnostic_list(const std::vector<Diagnostic> &diagnostics) {
    auto result = py::list{};
    for (const auto &diagnostic : diagnostics) {
        result.append(diagnostic_to_dict(diagnostic));
    }
    return result;
}

} // namespace

void bind_electrical_compilation(pybind11::module_ &module) {
    py::class_<CompiledElectricalModel>(module, "CompiledElectricalModel")
        .def_property_readonly("identity", &CompiledElectricalModel::identity,
                               py::return_value_policy::copy)
        .def_property_readonly("request_identity", &CompiledElectricalModel::request_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("request", &CompiledElectricalModel::request,
                               py::return_value_policy::copy)
        .def_property_readonly("ac_request",
                               [](const CompiledElectricalModel &model) -> py::object {
                                   const auto *request = model.ac_request();
                                   return request == nullptr
                                              ? py::none{}
                                              : py::cast(*request, py::return_value_policy::copy);
                               })
        .def_property_readonly("transient_request",
                               [](const CompiledElectricalModel &model) -> py::object {
                                   const auto *request = model.transient_request();
                                   return request
                                              ? py::cast(*request, py::return_value_policy::copy)
                                              : py::none{};
                               })
        .def("to_json", &io::write_compiled_electrical_model);

    py::class_<ElectricalCompileReport>(module, "ElectricalCompileReport")
        .def_property_readonly("complete", &ElectricalCompileReport::complete)
        .def_property_readonly("model",
                               [](const ElectricalCompileReport &report) -> py::object {
                                   const auto *model = report.model();
                                   return model == nullptr
                                              ? py::none{}
                                              : py::cast(*model, py::return_value_policy::copy);
                               })
        .def_property_readonly(
            "coverage",
            [](const ElectricalCompileReport &report) { return copied_tuple(report.coverage()); })
        .def_property_readonly("diagnostics",
                               [](const ElectricalCompileReport &report) {
                                   return diagnostic_list(report.diagnostics());
                               })
        .def_property_readonly("input", &ElectricalCompileReport::input,
                               py::return_value_policy::copy)
        .def("to_json", &io::write_electrical_compile_report);

    module.def("compile_electrical", py::overload_cast<const DcRequest &>(&compile_electrical),
               py::arg("request"));
    module.def("compile_electrical",
               py::overload_cast<const TransientRequest &>(&compile_electrical),
               py::arg("request"));
    module.def("compile_electrical", py::overload_cast<const AcRequest &>(&compile_electrical),
               py::arg("request"));
}

} // namespace volt::python
