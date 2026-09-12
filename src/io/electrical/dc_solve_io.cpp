#include <volt/io/electrical/dc_solve_io.hpp>

#include "../detail/entity_ref_format.hpp"

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::ordered_json;

Json quantity_json(const Quantity &quantity) {
    const auto dimension = [&]() -> const char * {
        switch (quantity.dimension()) {
        case UnitDimension::Voltage:
            return "voltage";
        case UnitDimension::Current:
            return "current";
        case UnitDimension::Power:
            return "power";
        default:
            throw KernelLogicError{ErrorCode::InvalidState, "Unexpected DC result dimension"};
        }
    }();
    return Json{{"dimension", dimension}, {"si", quantity.value()}};
}

template <typename T> Json optional_number(const std::optional<T> &value) {
    return value ? Json(*value) : Json(nullptr);
}

Json optional_quantity(const std::optional<Quantity> &value) {
    return value ? quantity_json(*value) : Json(nullptr);
}

Json options_json(const DcSolveOptions &options) {
    return Json{
        {"scaling", DcSolveOptions::scaling()},
        {"relative_rank_threshold", options.relative_rank_threshold()},
        {"minimum_reciprocal_condition", options.minimum_reciprocal_condition()},
        {"relative_residual_tolerance", options.relative_residual_tolerance()},
        {"absolute_voltage_tolerance", quantity_json(options.absolute_voltage_tolerance())},
        {"absolute_current_tolerance", quantity_json(options.absolute_current_tolerance())}};
}

Json observations_json(const DcSolution &solution) {
    auto nodes = Json::array();
    for (const auto &node : solution.nodes()) {
        nodes.push_back(Json{{"node", "node:" + std::to_string(node.node.index())},
                             {"potential", quantity_json(node.potential)}});
    }
    auto branches = Json::array();
    for (const auto &branch : solution.branches()) {
        branches.push_back(Json{{"branch", "branch:" + std::to_string(branch.branch.index())},
                                {"voltage", quantity_json(branch.voltage)},
                                {"current", quantity_json(branch.current)},
                                {"power", quantity_json(branch.power)}});
    }
    auto probes = Json::array();
    for (const auto &probe : solution.probes()) {
        probes.push_back(Json{{"key", probe.key.value()}, {"value", quantity_json(probe.value)}});
    }
    return Json{{"analysis_identity", solution.analysis_identity().value()},
                {"nodes", std::move(nodes)},
                {"branches", std::move(branches)},
                {"probes", std::move(probes)}};
}

const char *outcome_name(DcSolveOutcome outcome) {
    switch (outcome) {
    case DcSolveOutcome::Success:
        return "success";
    case DcSolveOutcome::RankDeficient:
        return "rank_deficient";
    case DcSolveOutcome::Inconsistent:
        return "inconsistent";
    case DcSolveOutcome::IllConditioned:
        return "ill_conditioned";
    case DcSolveOutcome::NumericalFailure:
        return "numerical_failure";
    case DcSolveOutcome::ResidualFailure:
        return "residual_failure";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unexpected DC solve outcome"};
}
} // namespace

std::string write_dc_solution(const DcSolution &solution) {
    return Json{{"format", "volt.dc-solution"},
                {"version", 1},
                {"contract_version", DcSolveReport::contract_version()},
                {"backend", DcSolveReport::backend()},
                {"options", options_json(solution.options())},
                {"model", Json::parse(write_compiled_electrical_model(solution.model()))},
                {"observations", observations_json(solution)}}
               .dump(2) +
           "\n";
}

std::string write_dc_solve_report(const DcSolveReport &report) {
    const auto &metrics = report.metrics();
    auto diagnostics = Json::array();
    for (const auto &diagnostic : report.diagnostics()) {
        auto entities = Json::array();
        for (const auto &entity : diagnostic.entities()) {
            entities.push_back(detail::entity_ref_serialized_id(entity));
        }
        diagnostics.push_back(Json{{"severity", detail::severity_name(diagnostic.severity())},
                                   {"category", diagnostic.category().value()},
                                   {"code", diagnostic.code().value()},
                                   {"message", diagnostic.message()},
                                   {"entities", std::move(entities)}});
    }
    return Json{{"format", "volt.dc-solve-report"},
                {"version", 1},
                {"analysis_identity", report.analysis_identity().value()},
                {"contract_version", DcSolveReport::contract_version()},
                {"backend", DcSolveReport::backend()},
                {"options", options_json(report.options())},
                {"outcome", outcome_name(report.outcome())},
                {"metrics",
                 Json{{"coordinate_count", metrics.coordinate_count},
                      {"rank", optional_number(metrics.rank)},
                      {"augmented_rank", optional_number(metrics.augmented_rank)},
                      {"reciprocal_condition", optional_number(metrics.reciprocal_condition)},
                      {"scaled_residual", optional_number(metrics.scaled_residual)},
                      {"voltage_residual", optional_quantity(metrics.voltage_residual)},
                      {"current_residual", optional_quantity(metrics.current_residual)},
                      {"voltage_error_ratio", optional_number(metrics.voltage_error_ratio)},
                      {"current_error_ratio", optional_number(metrics.current_error_ratio)}}},
                {"diagnostics", std::move(diagnostics)},
                {"model", Json::parse(write_compiled_electrical_model(report.model()))},
                {"solution",
                 report.solution() ? observations_json(*report.solution()) : Json(nullptr)}}
               .dump(2) +
           "\n";
}
} // namespace volt::io
