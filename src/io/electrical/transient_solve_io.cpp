#include "../detail/entity_ref_format.hpp"
#include <nlohmann/json.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>
#include <volt/io/electrical/transient_solve_io.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::ordered_json;

Json quantity_json(const Quantity &value) {
    const auto dimension = value.dimension() == UnitDimension::Voltage   ? "voltage"
                           : value.dimension() == UnitDimension::Current ? "current"
                           : value.dimension() == UnitDimension::Power   ? "power"
                                                                         : "time";
    return {{"dimension", dimension}, {"si", value.value()}};
}

template <typename T> Json optional_number(const std::optional<T> &value) {
    return value ? Json(*value) : Json(nullptr);
}

Json options_json(const TransientSolveOptions &options) {
    return {{"integrator", "adaptive_backward_euler_full_vs_two_half"},
            {"scaling", options.scaling()},
            {"relative_rank_threshold", options.relative_rank_threshold()},
            {"minimum_reciprocal_condition", options.minimum_reciprocal_condition()},
            {"relative_residual_tolerance", options.relative_residual_tolerance()},
            {"absolute_voltage_tolerance", quantity_json(options.absolute_voltage_tolerance())},
            {"absolute_current_tolerance", quantity_json(options.absolute_current_tolerance())},
            {"relative_temporal_tolerance", options.relative_temporal_tolerance()},
            {"absolute_temporal_voltage", quantity_json(options.absolute_temporal_voltage())},
            {"absolute_temporal_current", quantity_json(options.absolute_temporal_current())},
            {"minimum_step", quantity_json(options.minimum_step())},
            {"initial_step", quantity_json(options.initial_step())},
            {"maximum_step", quantity_json(options.maximum_step())},
            {"maximum_trials", options.maximum_trials()},
            {"maximum_accepted_steps", options.maximum_accepted_steps()}};
}

Json metrics_json(const TransientAlgebraMetrics &metrics) {
    return {{"coordinate_count", metrics.coordinate_count},
            {"rank", optional_number(metrics.rank)},
            {"augmented_rank", optional_number(metrics.augmented_rank)},
            {"reciprocal_condition", optional_number(metrics.reciprocal_condition)},
            {"scaled_residual", optional_number(metrics.scaled_residual)},
            {"voltage_residual",
             metrics.voltage_residual ? quantity_json(*metrics.voltage_residual) : Json(nullptr)},
            {"current_residual",
             metrics.current_residual ? quantity_json(*metrics.current_residual) : Json(nullptr)},
            {"voltage_error_ratio", optional_number(metrics.voltage_error_ratio)},
            {"current_error_ratio", optional_number(metrics.current_error_ratio)}};
}

Json diagnostics_json(const std::vector<Diagnostic> &values) {
    auto result = Json::array();
    for (const auto &diagnostic : values) {
        auto entities = Json::array();
        for (const auto &entity : diagnostic.entities())
            entities.push_back(detail::entity_ref_serialized_id(entity));
        result.push_back({{"severity", detail::severity_name(diagnostic.severity())},
                          {"category", diagnostic.category().value()},
                          {"code", diagnostic.code().value()},
                          {"message", diagnostic.message()},
                          {"entities", std::move(entities)}});
    }
    return result;
}

const char *outcome_name(TransientSolveOutcome outcome) {
    switch (outcome) {
    case TransientSolveOutcome::Success:
        return "success";
    case TransientSolveOutcome::InconsistentInitialState:
        return "inconsistent_initial_state";
    case TransientSolveOutcome::UnsupportedInitializationTopology:
        return "unsupported_initialization_topology";
    case TransientSolveOutcome::RankDeficient:
        return "rank_deficient";
    case TransientSolveOutcome::Inconsistent:
        return "inconsistent";
    case TransientSolveOutcome::IllConditioned:
        return "ill_conditioned";
    case TransientSolveOutcome::NumericalFailure:
        return "numerical_failure";
    case TransientSolveOutcome::ResidualFailure:
        return "residual_failure";
    case TransientSolveOutcome::StepLimit:
        return "step_limit";
    case TransientSolveOutcome::WorkLimit:
        return "work_limit";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unexpected transient outcome"};
}

const char *kind_name(TransientEvaluationKind kind) {
    switch (kind) {
    case TransientEvaluationKind::Initialization:
        return "initialization";
    case TransientEvaluationKind::AlgebraicSample:
        return "algebraic_sample";
    case TransientEvaluationKind::FullStep:
        return "full_step";
    case TransientEvaluationKind::FirstHalf:
        return "first_half";
    case TransientEvaluationKind::SecondHalf:
        return "second_half";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unexpected transient evaluation kind"};
}

Json observations_json(const TransientSolution &solution) {
    auto samples = Json::array();
    for (const auto &sample : solution.samples()) {
        auto nodes = Json::array(), branches = Json::array(), probes = Json::array();
        for (const auto &node : sample.nodes)
            nodes.push_back({{"node", "node:" + std::to_string(node.node.index())},
                             {"potential", quantity_json(node.potential)}});
        for (const auto &branch : sample.branches)
            branches.push_back({{"branch", "branch:" + std::to_string(branch.branch.index())},
                                {"voltage", quantity_json(branch.voltage)},
                                {"current", quantity_json(branch.current)},
                                {"power", quantity_json(branch.power)}});
        for (const auto &probe : sample.probes)
            probes.push_back({{"key", probe.key.value()}, {"value", quantity_json(probe.value)}});
        samples.push_back({{"time", quantity_json(sample.time)},
                           {"nodes", std::move(nodes)},
                           {"branches", std::move(branches)},
                           {"probes", std::move(probes)}});
    }
    return {{"analysis_identity", solution.analysis_identity().value()},
            {"samples", std::move(samples)}};
}
} // namespace

std::string write_transient_solution(const TransientSolution &solution) {
    return Json{{"format", "volt.transient-solution"},
                {"version", 1},
                {"contract_version", TransientSolveReport::contract_version()},
                {"backend", TransientSolveReport::backend()},
                {"options", options_json(solution.options())},
                {"model", Json::parse(write_compiled_electrical_model(solution.model()))},
                {"observations", observations_json(solution)}}
               .dump(2) +
           "\n";
}

std::string write_transient_solve_report(const TransientSolveReport &report) {
    auto evaluations = Json::array(), half_steps = Json::array(), derivatives = Json::array();
    for (const auto &evaluation : report.evaluations())
        evaluations.push_back({{"time", quantity_json(evaluation.time)},
                               {"duration", quantity_json(evaluation.duration)},
                               {"kind", kind_name(evaluation.kind)},
                               {"outcome", outcome_name(evaluation.outcome)},
                               {"metrics", metrics_json(evaluation.metrics)},
                               {"diagnostics", diagnostics_json(evaluation.diagnostics)},
                               {"solve_count", evaluation.solve_count},
                               {"factorization_count", evaluation.factorization_count},
                               {"normalized_error", optional_number(evaluation.normalized_error)},
                               {"trial_accepted", evaluation.trial_accepted}});
    for (const auto &half : report.accepted_half_steps())
        half_steps.push_back({{"time", quantity_json(half.time)},
                              {"duration", quantity_json(half.duration)},
                              {"normalized_error", half.normalized_error},
                              {"metrics", metrics_json(half.metrics)}});
    for (const auto &derivative : report.initial_derivatives())
        derivatives.push_back({{"branch", "branch:" + std::to_string(derivative.branch.index())},
                               {"derivative_si_per_second", derivative.derivative}});
    return Json{{"format", "volt.transient-solve-report"},
                {"version", 1},
                {"analysis_identity", report.analysis_identity().value()},
                {"contract_version", TransientSolveReport::contract_version()},
                {"backend", report.backend()},
                {"options", options_json(report.options())},
                {"outcome", outcome_name(report.outcome())},
                {"last_accepted_time", quantity_json(report.last_accepted_time())},
                {"trial_count", report.trial_count()},
                {"accepted_step_count", report.accepted_step_count()},
                {"solve_count", report.solve_count()},
                {"factorization_count", report.factorization_count()},
                {"initial_derivatives", std::move(derivatives)},
                {"evaluations", std::move(evaluations)},
                {"accepted_half_steps", std::move(half_steps)},
                {"diagnostics", diagnostics_json(report.diagnostics())},
                {"model", Json::parse(write_compiled_electrical_model(report.model()))},
                {"solution",
                 report.solution() ? observations_json(*report.solution()) : Json(nullptr)}}
               .dump(2) +
           "\n";
}
} // namespace volt::io
