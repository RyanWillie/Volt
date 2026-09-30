#include "ac_solve_bindings.hpp"
#include "binding_diagnostic_conversions.hpp"
#include <pybind11/complex.h>
#include <pybind11/stl.h>
#include <volt/electrical/ac_solve.hpp>
#include <volt/io/electrical/ac_solve_io.hpp>

namespace volt::python {
namespace {
template <typename Value> [[nodiscard]] py::tuple copied_tuple(const std::vector<Value> &values) {
    auto result = py::tuple{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

template <typename Value>
[[nodiscard]] py::object copied_optional(const std::optional<Value> &value) {
    return value.has_value() ? py::cast(*value, py::return_value_policy::copy) : py::none{};
}

[[nodiscard]] py::list diagnostic_list(const std::vector<Diagnostic> &diagnostics) {
    auto result = py::list{};
    for (const auto &diagnostic : diagnostics) {
        result.append(diagnostic_to_dict(diagnostic));
    }
    return result;
}

} // namespace

void bind_ac_solve(pybind11::module_ &module) {
    module.attr("AcSolveProvenance") = module.attr("DcSolveProvenance");
    py::class_<AcComplexQuantity>(module, "AcComplexQuantity")
        .def(py::init<UnitDimension, double, double>(), py::arg("dimension"), py::arg("real"),
             py::arg("imaginary"))
        .def_property_readonly("dimension", &AcComplexQuantity::dimension)
        .def_property_readonly("real", &AcComplexQuantity::real)
        .def_property_readonly("imaginary", &AcComplexQuantity::imaginary)
        .def_property_readonly("value", &AcComplexQuantity::value)
        .def_property_readonly("magnitude", &AcComplexQuantity::magnitude)
        .def_property_readonly("phase", &AcComplexQuantity::phase);
    py::class_<AcFrequencyResult>(module, "AcFrequencyResult")
        .def_readonly("frequency", &AcFrequencyResult::frequency)
        .def_property_readonly("nodes",
                               [](const AcFrequencyResult &p) { return copied_tuple(p.nodes); })
        .def_property_readonly("branches",
                               [](const AcFrequencyResult &p) { return copied_tuple(p.branches); })
        .def_property_readonly("probes",
                               [](const AcFrequencyResult &p) { return copied_tuple(p.probes); });
    py::class_<AcPointReport>(module, "AcPointReport")
        .def_readonly("frequency", &AcPointReport::frequency)
        .def_readonly("outcome", &AcPointReport::outcome)
        .def_readonly("metrics", &AcPointReport::metrics)
        .def_property_readonly(
            "diagnostics", [](const AcPointReport &p) { return diagnostic_list(p.diagnostics); });
    py::class_<AcSolveOptions, DcSolveOptions>(module, "AcSolveOptions")
        .def(py::init<double, double, double, Quantity, Quantity>(),
             py::arg("relative_rank_threshold") = 1e-12,
             py::arg("minimum_reciprocal_condition") = 1e-12,
             py::arg("relative_residual_tolerance") = 1e-9,
             py::arg("absolute_voltage_tolerance") = Quantity{UnitDimension::Voltage, 1e-9},
             py::arg("absolute_current_tolerance") = Quantity{UnitDimension::Current, 1e-12})
        .def_property_readonly("relative_rank_threshold", &AcSolveOptions::relative_rank_threshold)
        .def_property_readonly("minimum_reciprocal_condition",
                               &AcSolveOptions::minimum_reciprocal_condition)
        .def_property_readonly("relative_residual_tolerance",
                               &AcSolveOptions::relative_residual_tolerance)
        .def_property_readonly("absolute_voltage_tolerance",
                               &AcSolveOptions::absolute_voltage_tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("absolute_current_tolerance",
                               &AcSolveOptions::absolute_current_tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("scaling", [](const AcSolveOptions &) {
            return std::string{AcSolveOptions::scaling()};
        });

    py::enum_<AcSolveOutcome>(module, "AcSolveOutcome")
        .value("SUCCESS", AcSolveOutcome::Success)
        .value("RANK_DEFICIENT", AcSolveOutcome::RankDeficient)
        .value("INCONSISTENT", AcSolveOutcome::Inconsistent)
        .value("ILL_CONDITIONED", AcSolveOutcome::IllConditioned)
        .value("NUMERICAL_FAILURE", AcSolveOutcome::NumericalFailure)
        .value("RESIDUAL_FAILURE", AcSolveOutcome::ResidualFailure)
        .value("UNDEFINED_MEASUREMENT", AcSolveOutcome::UndefinedMeasurement);

    py::class_<AcSolveMetrics>(module, "AcSolveMetrics")
        .def_property_readonly("coordinate_count",
                               [](const AcSolveMetrics &value) { return value.coordinate_count; })
        .def_property_readonly("rank", [](const AcSolveMetrics &value) { return value.rank; })
        .def_property_readonly("augmented_rank",
                               [](const AcSolveMetrics &value) { return value.augmented_rank; })
        .def_property_readonly(
            "reciprocal_condition",
            [](const AcSolveMetrics &value) { return value.reciprocal_condition; })
        .def_property_readonly("scaled_residual",
                               [](const AcSolveMetrics &value) { return value.scaled_residual; })
        .def_property_readonly(
            "voltage_residual",
            [](const AcSolveMetrics &value) { return copied_optional(value.voltage_residual); })
        .def_property_readonly(
            "current_residual",
            [](const AcSolveMetrics &value) { return copied_optional(value.current_residual); })
        .def_property_readonly(
            "voltage_error_ratio",
            [](const AcSolveMetrics &value) { return value.voltage_error_ratio; })
        .def_property_readonly("current_error_ratio", [](const AcSolveMetrics &value) {
            return value.current_error_ratio;
        });

    py::class_<AcNodeResult>(module, "AcNodeResult")
        .def_property_readonly("node", [](const AcNodeResult &value) { return value.node.index(); })
        .def_property_readonly(
            "potential", [](const AcNodeResult &value) { return value.potential; },
            py::return_value_policy::copy);
    py::class_<AcBranchResult>(module, "AcBranchResult")
        .def_property_readonly("branch",
                               [](const AcBranchResult &value) { return value.branch.index(); })
        .def_property_readonly(
            "voltage", [](const AcBranchResult &value) { return value.voltage; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "current", [](const AcBranchResult &value) { return value.current; },
            py::return_value_policy::copy);
    py::class_<AcProbeResult>(module, "AcProbeResult")
        .def_property_readonly(
            "key", [](const AcProbeResult &value) { return value.key; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "value", [](const AcProbeResult &value) { return value.value; },
            py::return_value_policy::copy);

    py::class_<AcSolution>(module, "AcSolution")
        .def_property_readonly("provenance", &AcSolution::provenance, py::return_value_policy::copy)
        .def_property_readonly("analysis_identity", &AcSolution::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &AcSolution::model, py::return_value_policy::copy)
        .def_property_readonly("options", &AcSolution::options, py::return_value_policy::copy)
        .def_property_readonly("points",
                               [](const AcSolution &value) { return copied_tuple(value.points()); })
        .def("to_json", &io::write_ac_solution);

    py::class_<AcSolveReport>(module, "AcSolveReport")
        .def_property_readonly(
            "contract_version",
            [](const AcSolveReport &) { return AcSolveReport::contract_version(); })
        .def_property_readonly(
            "backend", [](const AcSolveReport &value) { return std::string{value.backend()}; })
        .def_property_readonly("provenance", &AcSolveReport::provenance,
                               py::return_value_policy::copy)
        .def_property_readonly("analysis_identity", &AcSolveReport::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &AcSolveReport::model, py::return_value_policy::copy)
        .def_property_readonly("options", &AcSolveReport::options, py::return_value_policy::copy)
        .def_property_readonly("outcome", &AcSolveReport::outcome)
        .def_property_readonly("success", &AcSolveReport::success)
        .def_property_readonly(
            "points", [](const AcSolveReport &value) { return copied_tuple(value.points()); })
        .def_property_readonly(
            "diagnostics",
            [](const AcSolveReport &report) { return diagnostic_list(report.diagnostics()); })
        .def_property_readonly("solution",
                               [](const AcSolveReport &report) -> py::object {
                                   const auto *solution = report.solution();
                                   return solution == nullptr
                                              ? py::none{}
                                              : py::cast(*solution, py::return_value_policy::copy);
                               })
        .def("to_json", &io::write_ac_solve_report);

    module.def("solve_ac", &solve_ac, py::arg("model"), py::arg("options") = AcSolveOptions{});
}
} // namespace volt::python
