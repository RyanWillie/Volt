#include "transient_solve_bindings.hpp"
#include "binding_diagnostic_conversions.hpp"
#include <pybind11/stl.h>
#include <volt/electrical/transient_solve.hpp>
#include <volt/io/electrical/transient_solve_io.hpp>

namespace volt::python {
namespace {
template <typename Value> py::tuple copied_tuple(const std::vector<Value> &values) {
    py::tuple result{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

py::list diagnostic_list(const std::vector<Diagnostic> &values) {
    py::list result;
    for (const auto &value : values)
        result.append(diagnostic_to_dict(value));
    return result;
}
} // namespace

void bind_transient_solve(py::module_ &module) {
    py::class_<TransientSolveOptions>(module, "TransientSolveOptions")
        .def(py::init<Quantity, Quantity, Quantity, std::size_t, std::size_t, double, Quantity,
                      Quantity>(),
             py::arg("minimum_step"), py::arg("initial_step"), py::arg("maximum_step"),
             py::arg("maximum_trials"), py::arg("maximum_accepted_steps"),
             py::arg("relative_temporal_tolerance") = 1e-4,
             py::arg("absolute_temporal_voltage") = Quantity{UnitDimension::Voltage, 1e-6},
             py::arg("absolute_temporal_current") = Quantity{UnitDimension::Current, 1e-9})
        .def_property_readonly("minimum_step", &TransientSolveOptions::minimum_step,
                               py::return_value_policy::copy)
        .def_property_readonly("initial_step", &TransientSolveOptions::initial_step,
                               py::return_value_policy::copy)
        .def_property_readonly("maximum_step", &TransientSolveOptions::maximum_step,
                               py::return_value_policy::copy)
        .def_property_readonly("maximum_trials", &TransientSolveOptions::maximum_trials,
                               py::return_value_policy::copy)
        .def_property_readonly("maximum_accepted_steps",
                               &TransientSolveOptions::maximum_accepted_steps,
                               py::return_value_policy::copy)
        .def_property_readonly("relative_temporal_tolerance",
                               &TransientSolveOptions::relative_temporal_tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("absolute_temporal_voltage",
                               &TransientSolveOptions::absolute_temporal_voltage,
                               py::return_value_policy::copy)
        .def_property_readonly("absolute_temporal_current",
                               &TransientSolveOptions::absolute_temporal_current,
                               py::return_value_policy::copy);
    py::enum_<TransientSolveOutcome>(module, "TransientSolveOutcome")
        .value("SUCCESS", TransientSolveOutcome::Success)
        .value("INCONSISTENT_INITIAL_STATE", TransientSolveOutcome::InconsistentInitialState)
        .value("UNSUPPORTED_INITIALIZATION_TOPOLOGY",
               TransientSolveOutcome::UnsupportedInitializationTopology)
        .value("RANK_DEFICIENT", TransientSolveOutcome::RankDeficient)
        .value("INCONSISTENT", TransientSolveOutcome::Inconsistent)
        .value("ILL_CONDITIONED", TransientSolveOutcome::IllConditioned)
        .value("NUMERICAL_FAILURE", TransientSolveOutcome::NumericalFailure)
        .value("RESIDUAL_FAILURE", TransientSolveOutcome::ResidualFailure)
        .value("STEP_LIMIT", TransientSolveOutcome::StepLimit)
        .value("WORK_LIMIT", TransientSolveOutcome::WorkLimit);
    py::enum_<TransientEvaluationKind>(module, "TransientEvaluationKind")
        .value("INITIALIZATION", TransientEvaluationKind::Initialization)
        .value("ALGEBRAIC_SAMPLE", TransientEvaluationKind::AlgebraicSample)
        .value("FULL_STEP", TransientEvaluationKind::FullStep)
        .value("FIRST_HALF", TransientEvaluationKind::FirstHalf)
        .value("SECOND_HALF", TransientEvaluationKind::SecondHalf);
    py::class_<TransientAlgebraMetrics>(module, "TransientAlgebraMetrics")
        .def_property_readonly(
            "coordinate_count",
            [](const TransientAlgebraMetrics &value) { return value.coordinate_count; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "rank", [](const TransientAlgebraMetrics &value) { return value.rank; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "augmented_rank",
            [](const TransientAlgebraMetrics &value) { return value.augmented_rank; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "reciprocal_condition",
            [](const TransientAlgebraMetrics &value) { return value.reciprocal_condition; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "scaled_residual",
            [](const TransientAlgebraMetrics &value) { return value.scaled_residual; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "voltage_residual",
            [](const TransientAlgebraMetrics &value) { return value.voltage_residual; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "current_residual",
            [](const TransientAlgebraMetrics &value) { return value.current_residual; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "voltage_error_ratio",
            [](const TransientAlgebraMetrics &value) { return value.voltage_error_ratio; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "current_error_ratio",
            [](const TransientAlgebraMetrics &value) { return value.current_error_ratio; },
            py::return_value_policy::copy);
    py::class_<TransientNodeResult>(module, "TransientNodeResult")
        .def_property_readonly(
            "node", [](const TransientNodeResult &value) { return value.node.index(); },
            py::return_value_policy::copy)
        .def_property_readonly(
            "potential", [](const TransientNodeResult &value) { return value.potential; },
            py::return_value_policy::copy);
    py::class_<TransientBranchResult>(module, "TransientBranchResult")
        .def_property_readonly(
            "branch", [](const TransientBranchResult &value) { return value.branch.index(); },
            py::return_value_policy::copy)
        .def_property_readonly(
            "voltage", [](const TransientBranchResult &value) { return value.voltage; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "current", [](const TransientBranchResult &value) { return value.current; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "power", [](const TransientBranchResult &value) { return value.power; },
            py::return_value_policy::copy);
    py::class_<TransientProbeResult>(module, "TransientProbeResult")
        .def_property_readonly(
            "key", [](const TransientProbeResult &value) { return value.key; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "value", [](const TransientProbeResult &value) { return value.value; },
            py::return_value_policy::copy);
    py::class_<TransientStorageDerivative>(module, "TransientStorageDerivative")
        .def_property_readonly(
            "branch", [](const TransientStorageDerivative &value) { return value.branch.index(); },
            py::return_value_policy::copy)
        .def_property_readonly(
            "derivative", [](const TransientStorageDerivative &value) { return value.derivative; },
            py::return_value_policy::copy);
    py::class_<TransientEvaluation>(module, "TransientEvaluation")
        .def_property_readonly(
            "time", [](const TransientEvaluation &value) { return value.time; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "duration", [](const TransientEvaluation &value) { return value.duration; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "kind", [](const TransientEvaluation &value) { return value.kind; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "outcome", [](const TransientEvaluation &value) { return value.outcome; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "metrics", [](const TransientEvaluation &value) { return value.metrics; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "solve_count", [](const TransientEvaluation &value) { return value.solve_count; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "factorization_count",
            [](const TransientEvaluation &value) { return value.factorization_count; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "normalized_error",
            [](const TransientEvaluation &value) { return value.normalized_error; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "trial_accepted", [](const TransientEvaluation &value) { return value.trial_accepted; },
            py::return_value_policy::copy)
        .def_property_readonly("diagnostics", [](const TransientEvaluation &value) {
            return diagnostic_list(value.diagnostics);
        });
    py::class_<TransientAcceptedHalfStep>(module, "TransientAcceptedHalfStep")
        .def_property_readonly(
            "time", [](const TransientAcceptedHalfStep &value) { return value.time; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "duration", [](const TransientAcceptedHalfStep &value) { return value.duration; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "normalized_error",
            [](const TransientAcceptedHalfStep &value) { return value.normalized_error; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "metrics", [](const TransientAcceptedHalfStep &value) { return value.metrics; },
            py::return_value_policy::copy);
    py::class_<TransientSample>(module, "TransientSample")
        .def_property_readonly(
            "time", [](const TransientSample &value) { return value.time; },
            py::return_value_policy::copy)
        .def_property_readonly(
            "nodes", [](const TransientSample &value) { return copied_tuple(value.nodes); })
        .def_property_readonly(
            "branches", [](const TransientSample &value) { return copied_tuple(value.branches); })
        .def_property_readonly(
            "probes", [](const TransientSample &value) { return copied_tuple(value.probes); });
    py::class_<TransientSolution>(module, "TransientSolution")
        .def_property_readonly("analysis_identity", &TransientSolution::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &TransientSolution::model, py::return_value_policy::copy)
        .def_property_readonly("options", &TransientSolution::options,
                               py::return_value_policy::copy)
        .def_property_readonly(
            "samples", [](const TransientSolution &value) { return copied_tuple(value.samples()); })
        .def("to_json", &io::write_transient_solution);
    py::class_<TransientSolveReport>(module, "TransientSolveReport")
        .def_property_readonly("analysis_identity", &TransientSolveReport::analysis_identity,
                               py::return_value_policy::copy)
        .def_property_readonly("model", &TransientSolveReport::model, py::return_value_policy::copy)
        .def_property_readonly("options", &TransientSolveReport::options,
                               py::return_value_policy::copy)
        .def_property_readonly("outcome", &TransientSolveReport::outcome,
                               py::return_value_policy::copy)
        .def_property_readonly("success", &TransientSolveReport::success,
                               py::return_value_policy::copy)
        .def_property_readonly("last_accepted_time", &TransientSolveReport::last_accepted_time,
                               py::return_value_policy::copy)
        .def_property_readonly("trial_count", &TransientSolveReport::trial_count,
                               py::return_value_policy::copy)
        .def_property_readonly("accepted_step_count", &TransientSolveReport::accepted_step_count,
                               py::return_value_policy::copy)
        .def_property_readonly("solve_count", &TransientSolveReport::solve_count,
                               py::return_value_policy::copy)
        .def_property_readonly("factorization_count", &TransientSolveReport::factorization_count,
                               py::return_value_policy::copy)
        .def_property_readonly(
            "evaluations",
            [](const TransientSolveReport &value) { return copied_tuple(value.evaluations()); })
        .def_property_readonly("initial_derivatives",
                               [](const TransientSolveReport &value) {
                                   return copied_tuple(value.initial_derivatives());
                               })
        .def_property_readonly("accepted_half_steps",
                               [](const TransientSolveReport &value) {
                                   return copied_tuple(value.accepted_half_steps());
                               })
        .def_property_readonly("backend",
                               [](const TransientSolveReport &) {
                                   return std::string{TransientSolveReport::backend()};
                               })
        .def_property_readonly(
            "contract_version",
            [](const TransientSolveReport &) { return TransientSolveReport::contract_version(); })
        .def_property_readonly(
            "diagnostics",
            [](const TransientSolveReport &value) { return diagnostic_list(value.diagnostics()); })
        .def_property_readonly("solution",
                               [](const TransientSolveReport &report) -> py::object {
                                   const auto *solution = report.solution();
                                   return solution
                                              ? py::cast(*solution, py::return_value_policy::copy)
                                              : py::none{};
                               })
        .def("to_json", &io::write_transient_solve_report);
    module.def("solve_transient", &solve_transient, py::arg("model"), py::arg("options"));
}
} // namespace volt::python
