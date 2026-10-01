#include "dc_solve_bindings.hpp"

#include "binding_diagnostic_conversions.hpp"

#include <cstddef>
#include <optional>
#include <string>

#include <pybind11/stl.h>

#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/ngspice_dc.hpp>
#include <volt/io/electrical/dc_solve_io.hpp>
#include <volt/io/electrical/ngspice_dc_io.hpp>

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

void bind_dc_solve(pybind11::module_ &module) {
    py::class_<DcSolveProvenance>(module, "DcSolveProvenance")
        .def_readonly("backend", &DcSolveProvenance::backend)
        .def_readonly("backend_version", &DcSolveProvenance::backend_version)
        .def_readonly("adapter", &DcSolveProvenance::adapter)
        .def_readonly("adapter_contract_version", &DcSolveProvenance::adapter_contract_version)
        .def_readonly("effective_settings", &DcSolveProvenance::effective_settings)
        .def_readonly("acceptance_policy", &DcSolveProvenance::acceptance_policy)
        .def_readonly("validation_backend", &DcSolveProvenance::validation_backend)
        .def_property_readonly(
            "deck_identity",
            [](const DcSolveProvenance &value) { return copied_optional(value.deck_identity); })
        .def_property_readonly("mapping_identity", [](const DcSolveProvenance &value) {
            return copied_optional(value.mapping_identity);
        });

    py::class_<NgspiceDcAnalysis>(module, "NgspiceDcAnalysis")
        .def_property_readonly("model", &NgspiceDcAnalysis::model, py::return_value_policy::copy)
        .def_property_readonly("complete", &NgspiceDcAnalysis::complete)
        .def_property_readonly("deck", &NgspiceDcAnalysis::deck)
        .def_property_readonly("deck_identity", &NgspiceDcAnalysis::deck_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("mapping_identity", &NgspiceDcAnalysis::mapping_identity,
                               py::return_value_policy::copy)
        .def_property_readonly(
            "maximum_output_bytes",
            [](const NgspiceDcAnalysis &) { return NgspiceDcAnalysis::maximum_output_bytes(); })
        .def_property_readonly(
            "diagnostics",
            [](const NgspiceDcAnalysis &value) { return diagnostic_list(value.diagnostics()); })
        .def("to_json", &io::write_ngspice_dc_analysis);

    py::class_<DcSolveOptions>(module, "DcSolveOptions")
        .def(py::init<double, double, double, Quantity, Quantity>(),
             py::arg("relative_rank_threshold") = 1e-12,
             py::arg("minimum_reciprocal_condition") = 1e-12,
             py::arg("relative_residual_tolerance") = 1e-9,
             py::arg("absolute_voltage_tolerance") = Quantity{UnitDimension::Voltage, 1e-9},
             py::arg("absolute_current_tolerance") = Quantity{UnitDimension::Current, 1e-12})
        .def_property_readonly("relative_rank_threshold", &DcSolveOptions::relative_rank_threshold)
        .def_property_readonly("minimum_reciprocal_condition",
                               &DcSolveOptions::minimum_reciprocal_condition)
        .def_property_readonly("relative_residual_tolerance",
                               &DcSolveOptions::relative_residual_tolerance)
        .def_property_readonly("absolute_voltage_tolerance",
                               &DcSolveOptions::absolute_voltage_tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("absolute_current_tolerance",
                               &DcSolveOptions::absolute_current_tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("scaling", [](const DcSolveOptions &) {
            return std::string{DcSolveOptions::scaling()};
        });

    py::class_<NonlinearDcSolveOptions>(module, "NonlinearDcSolveOptions")
        .def(py::init<DcSolveOptions, std::size_t, std::size_t, std::size_t, std::size_t>(),
             py::arg("acceptance") = DcSolveOptions{}, py::arg("max_iterations") = 80,
             py::arg("max_backtracks") = 24, py::arg("max_residual_evaluations") = 2048,
             py::arg("max_jacobian_evaluations") = 81)
        .def_property_readonly("acceptance", &NonlinearDcSolveOptions::acceptance,
                               py::return_value_policy::copy)
        .def_property_readonly("max_iterations", &NonlinearDcSolveOptions::max_iterations)
        .def_property_readonly("max_backtracks", &NonlinearDcSolveOptions::max_backtracks)
        .def_property_readonly("max_residual_evaluations",
                               &NonlinearDcSolveOptions::max_residual_evaluations)
        .def_property_readonly("max_jacobian_evaluations",
                               &NonlinearDcSolveOptions::max_jacobian_evaluations);

    py::enum_<DcSolveOutcome>(module, "DcSolveOutcome")
        .value("CONVERGED", DcSolveOutcome::Converged)
        .value("UNSUPPORTED_MODEL", DcSolveOutcome::UnsupportedModel)
        .value("JACOBIAN_SINGULAR", DcSolveOutcome::JacobianSingular)
        .value("DOMAIN_LIMITED", DcSolveOutcome::DomainLimited)
        .value("LINE_SEARCH_FAILED", DcSolveOutcome::LineSearchFailed)
        .value("ITERATION_LIMIT", DcSolveOutcome::IterationLimit)
        .value("EVALUATION_LIMIT", DcSolveOutcome::EvaluationLimit)
        .value("SUCCESS", DcSolveOutcome::Success)
        .value("RANK_DEFICIENT", DcSolveOutcome::RankDeficient)
        .value("INCONSISTENT", DcSolveOutcome::Inconsistent)
        .value("ILL_CONDITIONED", DcSolveOutcome::IllConditioned)
        .value("NUMERICAL_FAILURE", DcSolveOutcome::NumericalFailure)
        .value("RESIDUAL_FAILURE", DcSolveOutcome::ResidualFailure);

    py::class_<DcSolveMetrics>(module, "DcSolveMetrics")
        .def_property_readonly("coordinate_count",
                               [](const DcSolveMetrics &value) { return value.coordinate_count; })
        .def_property_readonly("rank", [](const DcSolveMetrics &value) { return value.rank; })
        .def_property_readonly("augmented_rank",
                               [](const DcSolveMetrics &value) { return value.augmented_rank; })
        .def_property_readonly(
            "reciprocal_condition",
            [](const DcSolveMetrics &value) { return value.reciprocal_condition; })
        .def_property_readonly("scaled_residual",
                               [](const DcSolveMetrics &value) { return value.scaled_residual; })
        .def_property_readonly(
            "voltage_residual",
            [](const DcSolveMetrics &value) { return copied_optional(value.voltage_residual); })
        .def_property_readonly(
            "current_residual",
            [](const DcSolveMetrics &value) { return copied_optional(value.current_residual); })
        .def_property_readonly(
            "voltage_error_ratio",
            [](const DcSolveMetrics &value) { return value.voltage_error_ratio; })
        .def_property_readonly(
            "current_error_ratio",
            [](const DcSolveMetrics &value) { return value.current_error_ratio; })
        .def_readonly("iterations", &DcSolveMetrics::iterations)
        .def_readonly("residual_evaluations", &DcSolveMetrics::residual_evaluations)
        .def_readonly("jacobian_evaluations", &DcSolveMetrics::jacobian_evaluations)
        .def_readonly("backtracks", &DcSolveMetrics::backtracks)
        .def_readonly("domain_rejections", &DcSolveMetrics::domain_rejections)
        .def_readonly("nonfinite_rejections", &DcSolveMetrics::nonfinite_rejections)
        .def_readonly("correction_error_ratio", &DcSolveMetrics::correction_error_ratio)
        .def_readonly("merit", &DcSolveMetrics::merit)
        .def_property_readonly("residual_weights", [](const DcSolveMetrics &value) {
            return copied_tuple(value.residual_weights);
        });

    py::class_<DcNodeResult>(module, "DcNodeResult")
        .def_property_readonly("node", [](const DcNodeResult &value) { return value.node.index(); })
        .def_property_readonly(
            "potential", [](const DcNodeResult &value) { return value.potential; },
            py::return_value_policy::copy);
    py::class_<DcBranchResult>(module, "DcBranchResult")
        .def_property_readonly("branch",
                               [](const DcBranchResult &value) { return value.branch.index(); })
        .def_property_readonly(
            "voltage", [](const DcBranchResult &value) { return value.voltage; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "current", [](const DcBranchResult &value) { return value.current; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "power", [](const DcBranchResult &value) { return value.power; },
            py::return_value_policy::copy);
    py::class_<DcProbeResult>(module, "DcProbeResult")
        .def_property_readonly(
            "key", [](const DcProbeResult &value) { return value.key; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "value", [](const DcProbeResult &value) { return value.value; },
            py::return_value_policy::copy);

    py::class_<DcSolution>(module, "DcSolution")
        .def_property_readonly("provenance", &DcSolution::provenance, py::return_value_policy::copy)
        .def_property_readonly("analysis_identity", &DcSolution::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &DcSolution::model, py::return_value_policy::copy)
        .def_property_readonly("options", &DcSolution::options, py::return_value_policy::copy)
        .def_property_readonly("nonlinear_options",
                               [](const DcSolution &value) -> py::object {
                                   const auto *options = value.nonlinear_options();
                                   return options == nullptr
                                              ? py::none{}
                                              : py::cast(*options, py::return_value_policy::copy);
                               })
        .def_property_readonly("nodes",
                               [](const DcSolution &value) { return copied_tuple(value.nodes()); })
        .def_property_readonly(
            "branches", [](const DcSolution &value) { return copied_tuple(value.branches()); })
        .def_property_readonly("probes",
                               [](const DcSolution &value) { return copied_tuple(value.probes()); })
        .def("to_json", &io::write_dc_solution);

    py::class_<DcSolveReport>(module, "DcSolveReport")
        .def_property_readonly(
            "contract_version",
            [](const DcSolveReport &) { return DcSolveReport::contract_version(); })
        .def_property_readonly(
            "backend", [](const DcSolveReport &value) { return std::string{value.backend()}; })
        .def_property_readonly("provenance", &DcSolveReport::provenance,
                               py::return_value_policy::copy)
        .def_property_readonly("analysis_identity", &DcSolveReport::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &DcSolveReport::model, py::return_value_policy::copy)
        .def_property_readonly("options", &DcSolveReport::options, py::return_value_policy::copy)
        .def_property_readonly("nonlinear_options",
                               [](const DcSolveReport &value) -> py::object {
                                   const auto *options = value.nonlinear_options();
                                   return options == nullptr
                                              ? py::none{}
                                              : py::cast(*options, py::return_value_policy::copy);
                               })
        .def_property_readonly("outcome", &DcSolveReport::outcome)
        .def_property_readonly("success", &DcSolveReport::success)
        .def_property_readonly("metrics", &DcSolveReport::metrics, py::return_value_policy::copy)
        .def_property_readonly(
            "diagnostics",
            [](const DcSolveReport &report) { return diagnostic_list(report.diagnostics()); })
        .def_property_readonly("solution",
                               [](const DcSolveReport &report) -> py::object {
                                   const auto *solution = report.solution();
                                   return solution == nullptr
                                              ? py::none{}
                                              : py::cast(*solution, py::return_value_policy::copy);
                               })
        .def("to_json", &io::write_dc_solve_report);

    module.def(
        "solve_dc",
        py::overload_cast<const CompiledElectricalModel &, const DcSolveOptions &>(&solve_dc),
        py::arg("model"), py::arg("options") = DcSolveOptions{});
    module.def("solve_dc",
               py::overload_cast<const CompiledElectricalModel &, const NonlinearDcSolveOptions &>(
                   &solve_dc),
               py::arg("model"), py::arg("options"));
    module.def("prepare_ngspice_dc", &prepare_ngspice_dc, py::arg("model"));
    module.def(
        "solve_ngspice_dc",
        [](const NgspiceDcAnalysis &analysis, const py::bytes &output,
           const DcSolveOptions &options) {
            const auto bytes = output.cast<std::string>();
            return solve_ngspice_dc(analysis, bytes, options);
        },
        py::arg("analysis"), py::arg("output"), py::arg("options") = DcSolveOptions{});
}

} // namespace volt::python
